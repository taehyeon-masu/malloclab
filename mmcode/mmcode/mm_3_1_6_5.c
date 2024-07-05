#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <time.h>


#include "mm.h"
#include "memlib.h"

#define ALIGNMENT 8
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define MIN_BLOCK_SIZE  16 
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define WSIZE 4             
#define DSIZE 8             
#define CHUNKSIZE (1 << 12) 

#define MAX(x, y) (x > y ? x : y)
#define PACK(size, alloc) (size | alloc)                              
#define GET(p) (*(unsigned int *)(p))                                 
#define PUT(p, val) (*(unsigned int *)(p) = (unsigned int)(val))                    
#define GET_SIZE(p) (GET(p) & ~0x7)                                  
#define GET_ALLOC(p) (GET(p) & 0x1)                                  
#define HDRP(bp) ((char *)(bp)-WSIZE)                                
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)          
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE))) 
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE))) 

// Explicit
#define GET_NEXT(bp)(*(void **)((char *)(bp) + WSIZE))	
#define GET_PREV(bp)(*(void **)(bp)) 			

// Segregated list
#define SEGSIZE 12
#define GET_ROOT(class)(*(void**)((char *)(heap_listp +  (2 * WSIZE)) + (class * WSIZE)))

// coalesce buffer
#define IS_BUFFER(p) ((GET(HDRP(p))>>1)&0x1) // 1 -> in buffer, 0 -> out buffer

static void* extend_heap(size_t words);
static void* coalesce(void* bp);
static void* find_fit(size_t asize);
static void* place(void* bp, size_t asize);
static void* heap_listp;
static void* heap_tail;

// Explicit
static void rm_free_block(void *bp); 	
static void add_free_block(void *bp); 	

// Segregated list
static int get_class(size_t size);
static size_t available_size(void* ptr);
static size_t available_alloc(void* ptr);
static void* default_realloc(void* ptr, size_t size);

// coalesce buffer
static void add_coal_buffer(void* bp);
static void rm_coal_buffer(void* bp);
static void coalesce_buffer();

int mm_init(void) {

	if ((heap_listp = mem_sbrk((SEGSIZE + 4) * WSIZE)) == (void *)-1)
		return -1;
	PUT(heap_listp, 0);
	PUT(heap_listp + (1 * WSIZE), PACK((SEGSIZE + 2) * WSIZE, 1));			// prologue header
	for(int i = 0; i < SEGSIZE; i++)
		PUT(heap_listp + ((2 + i) * WSIZE), NULL);
	PUT(heap_listp + ((2 + SEGSIZE) * WSIZE), PACK((SEGSIZE + 2) * WSIZE, 1));	// prologue footer	
	PUT(heap_listp + ((3 + SEGSIZE) * WSIZE), PACK(0, 1));		// epilogue header
	heap_tail = heap_listp + (4 + SEGSIZE) * WSIZE;


	if (extend_heap(8) == NULL)
		return -1;
	
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return -1;

	return 0;
}

void* mm_malloc(size_t size) {
#ifdef DEBUG
	mm_checkheap(__LINE__);
	printf("malloc size: %d\n", size);
#endif
	size_t asize;
	size_t extendsize;
	char *bp;
	char *ptr;

	coalesce_buffer();

	if (size == 0)
		return NULL;

	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + DSIZE + DSIZE -1) / DSIZE);

	if ((bp = find_fit(asize)) != NULL) {
		ptr = place(bp, asize);
		return ptr;
	}
	else {
		size_t new_size = asize;
		if (!GET_ALLOC(HDRP(PREV_BLKP(heap_tail)))) {
			size_t end_size = GET_SIZE(FTRP(PREV_BLKP(heap_tail)));
			if (asize >= end_size) {
				new_size = asize - end_size;
			}
			else if (IS_BUFFER(PREV_BLKP(heap_tail)))
				new_size = asize;
			else {
				new_size = asize;
			}
		}
		size_t nsize = MAX(new_size, CHUNKSIZE);
		if ((bp = extend_heap(nsize / WSIZE)) == NULL)
			return NULL;
		ptr = place(bp, asize);
		return ptr;
	}

}

void mm_free(void* bp) {
#ifdef DEBUG
	mm_checkheap(__LINE__);
	printf("free pointer: %p\n", ptr);
#endif
	size_t size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 2));
	PUT(FTRP(bp), PACK(size, 2));
	add_coal_buffer(bp);
}

void* mm_realloc(void* ptr, size_t size) {
#ifdef DEBUG
	mm_checkheap(__LINE__);
	printf("realloc size: %d\n", size);
#endif
	size_t asize;
	void* newptr;

	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + DSIZE + DSIZE - 1) / DSIZE);

	if (ptr == NULL)
		return mm_malloc(size);

	if (size <= 0) {
		mm_free(ptr);
		return 0;
	}
	coalesce_buffer();

	size_t oldsize = GET_SIZE(HDRP(ptr));
	size_t coalsize = available_size(ptr);
	int available = available_alloc(ptr);
	if (asize <= oldsize)
		return ptr;


	if (asize <= coalsize) {
		if (available == 2) {
			rm_free_block(NEXT_BLKP(ptr));
				PUT(HDRP(ptr), PACK(coalsize, 1));
				PUT(FTRP(ptr), PACK(coalsize, 1));
				return ptr;
			
		}
		else if (available == 0)
			return default_realloc(ptr, size);
		else if (available == 1) {
			char* prev_block = PREV_BLKP(ptr);
			size_t prev_size = GET_SIZE(HDRP(prev_block));
			if (prev_size > oldsize) {
				rm_free_block(PREV_BLKP(ptr));
				memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
				PUT(HDRP(prev_block), PACK(asize, 1));
				PUT(FTRP(prev_block), PACK(asize, 1));

				PUT(HDRP(NEXT_BLKP(prev_block)), PACK(coalsize - asize, 0));
				PUT(FTRP(NEXT_BLKP(prev_block)), PACK(coalsize - asize, 0));
				coalesce(NEXT_BLKP(prev_block));
				return prev_block;
			}
			else {
				int total_movesize = GET_SIZE(HDRP(ptr)) - DSIZE;
				int sep_movesize = GET_SIZE(HDRP(prev_block));
				int n = total_movesize / sep_movesize;
				rm_free_block(PREV_BLKP(ptr));

				for (int i = 0; i<n; i++) {
					memcpy(prev_block + i*sep_movesize, ptr + i * sep_movesize, sep_movesize);
				}
				memcpy(prev_block + n * sep_movesize, ptr + n * sep_movesize, total_movesize - (sep_movesize*n));

				if ((coalsize - asize) < 2 * DSIZE) {
					PUT(HDRP(prev_block), PACK(coalsize, 1));
					PUT(FTRP(prev_block), PACK(coalsize, 1));
					return prev_block;
				}
				else {
					PUT(HDRP(prev_block), PACK(asize, 1));
					PUT(FTRP(prev_block), PACK(asize, 1));

					PUT(HDRP(NEXT_BLKP(prev_block)), PACK(coalsize - asize, 0));
					PUT(FTRP(NEXT_BLKP(prev_block)), PACK(coalsize - asize, 0));
					
					coalesce(NEXT_BLKP(prev_block));
					return prev_block;
				}
				
			}
		}
		else if (available == 3) {
			void* prev_block = PREV_BLKP(ptr);
			size_t prev_size = GET_SIZE(HDRP(prev_block));
			size_t prev_current_size = GET_SIZE(HDRP(prev_block)) + GET_SIZE(HDRP(ptr));
			size_t current_next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr))) + GET_SIZE(HDRP(ptr));
			if (asize <= current_next_size) {
				coalsize = current_next_size;
				rm_free_block(NEXT_BLKP(ptr));
					PUT(HDRP(ptr), PACK(coalsize, 1));
					PUT(FTRP(ptr), PACK(coalsize, 1));
					return ptr;
				
			}

			if (prev_size > oldsize) {
				rm_free_block(PREV_BLKP(ptr));
				memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
				PUT(HDRP(prev_block), PACK(asize, 1));
				PUT(FTRP(prev_block), PACK(asize, 1));

				PUT(HDRP(NEXT_BLKP(prev_block)), PACK(coalsize - asize, 0));
				PUT(FTRP(NEXT_BLKP(prev_block)), PACK(coalsize - asize, 0));
				coalesce(NEXT_BLKP(prev_block));
				return prev_block;
			}

			if (asize <= prev_current_size) {
				coalsize = prev_current_size;
				int total_movesize = GET_SIZE(HDRP(ptr)) - DSIZE;
				int sep_movesize = GET_SIZE(HDRP(prev_block));
				int n = total_movesize / sep_movesize;
				rm_free_block(PREV_BLKP(ptr));

				for (int i = 0; i < n; i++) {
					memcpy(prev_block + i * sep_movesize, ptr + i * sep_movesize, sep_movesize);
				}
				memcpy(prev_block + n * sep_movesize, ptr + n * sep_movesize, total_movesize - (sep_movesize * n));

				if ((coalsize - asize) < 2 * DSIZE) {
					PUT(HDRP(prev_block), PACK(coalsize, 1));
					PUT(FTRP(prev_block), PACK(coalsize, 1));
					return prev_block;
				}
				else {
					PUT(HDRP(prev_block), PACK(asize, 1));
					PUT(FTRP(prev_block), PACK(asize, 1));

					PUT(HDRP(NEXT_BLKP(prev_block)), PACK(coalsize - asize, 0));
					PUT(FTRP(NEXT_BLKP(prev_block)), PACK(coalsize - asize, 0));
					coalesce(NEXT_BLKP(prev_block));

					return prev_block;
				}
			}

		}
	}
	return default_realloc(ptr, size);
}

static void* extend_heap(size_t words) {
	char* bp;
	size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;

	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1));
	
	heap_tail += size;
	return coalesce(bp);
}

static void* coalesce(void* bp) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && !next_alloc) {
		if (IS_BUFFER(PREV_BLKP(bp)) == 1) 
			next_alloc = 1;
	}
	else if (!prev_alloc && next_alloc) {
		if (IS_BUFFER(NEXT_BLKP(bp)) == 1)
			prev_alloc = 1;
	}
	else if (!prev_alloc && !next_alloc) {
		if (IS_BUFFER(PREV_BLKP(bp)) && IS_BUFFER(NEXT_BLKP(bp))) {
			prev_alloc = 1;
			next_alloc = 1;
		}
		else if (!IS_BUFFER(PREV_BLKP(bp)) && IS_BUFFER(NEXT_BLKP(bp))) {
			next_alloc = 1;
		}
		else if (IS_BUFFER(PREV_BLKP(bp)) && !IS_BUFFER(NEXT_BLKP(bp))) {
			prev_alloc = 1;
		}

	}

	if (prev_alloc && next_alloc) {
		add_free_block(bp);
		return bp;
	}

	else if (prev_alloc && !next_alloc) {
		rm_free_block(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	else if (!prev_alloc && next_alloc) {	
		rm_free_block(PREV_BLKP(bp));	
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	else {	
		rm_free_block(PREV_BLKP(bp));	
		rm_free_block(NEXT_BLKP(bp));	
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}

	add_free_block(bp);
	return bp;
}
static void coalesce_buffer(){
	int class = SEGSIZE - 1;
	char* ptr = GET_ROOT(class);
	if (ptr == NULL)
		return;
	if (ptr == 0)
		return;

	while (ptr != NULL) {
		char* start_ptr = ptr;
		char* next_ptr = ptr;
		int totalsize = 0;
		while (start_ptr >= (char*)heap_listp + 2*WSIZE) {
			totalsize += GET_SIZE(HDRP(start_ptr));
			if (IS_BUFFER(start_ptr))
				rm_coal_buffer(start_ptr);
			else
				rm_free_block(start_ptr);


			if (GET_ALLOC(HDRP(PREV_BLKP(start_ptr)))) {
				break;
			}
			else {
				start_ptr = PREV_BLKP(start_ptr);
			}
		}

		while (next_ptr <= ((char*)heap_tail-WSIZE)) {
			next_ptr = NEXT_BLKP(next_ptr);

			if (!GET_ALLOC(HDRP(next_ptr))) {
				totalsize += GET_SIZE(HDRP(next_ptr));
					
				if (IS_BUFFER(next_ptr))
					rm_coal_buffer(next_ptr);
				else
					rm_free_block(next_ptr);
			}
			else {
				break;
			}
		}
		PUT(HDRP(start_ptr), PACK(totalsize, 0));
		PUT(FTRP(start_ptr), PACK(totalsize, 0));
		add_free_block(start_ptr);
		ptr = GET_ROOT(class);
	}
}
static void* find_fit(size_t asize) {
	int class = get_class(asize);
	void *bp = GET_ROOT(class);
	
	while(class < SEGSIZE-1) {
		bp = GET_ROOT(class);

		while(bp != NULL) {
			if ((asize <= GET_SIZE(HDRP(bp))))
				return bp;
			bp = GET_NEXT(bp);
		}
		class += 1;
	}


    	return NULL;
}

static void * place(void* bp, size_t asize) {
	rm_free_block(bp);

	size_t csize = GET_SIZE(HDRP(bp));

	if ((csize - asize) < 2*DSIZE) {
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));

		return bp;
	}
	else if (asize  > 72) {
		/* separated free block is located front */
		PUT(HDRP(bp), PACK(csize - asize, 0));
		PUT(FTRP(bp), PACK(csize - asize, 0));

		/* split the block */
		PUT(HDRP(NEXT_BLKP(bp)), PACK(asize, 1));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(asize, 1));

		add_free_block(bp);
		return NEXT_BLKP(bp);
	}

	else {
			PUT(HDRP(bp), PACK(asize, 1));
			PUT(FTRP(bp), PACK(asize, 1));

			/* split the block */
			PUT(HDRP(NEXT_BLKP(bp)), PACK(csize - asize, 0));
			PUT(FTRP(NEXT_BLKP(bp)), PACK(csize - asize, 0));
			add_free_block(NEXT_BLKP(bp));
			return bp;
		}
}

static void rm_free_block(void *bp) {
	int class = get_class(GET_SIZE(HDRP(bp)));

	if(bp == GET_ROOT(class)) {		
		GET_ROOT(class) = GET_NEXT(GET_ROOT(class));
		return;
	}

	GET_NEXT(GET_PREV(bp)) = GET_NEXT(bp);

	if (GET_NEXT(bp) != NULL)
		GET_PREV(GET_NEXT(bp)) = GET_PREV(bp);
}

static void add_free_block(void *bp) {
	int class = get_class(GET_SIZE(HDRP(bp)));

	GET_NEXT(bp) = GET_ROOT(class);
	if (GET_ROOT(class) != NULL)
		GET_PREV(GET_ROOT(class)) = bp;
	GET_ROOT(class) = bp;
}

static void rm_coal_buffer(void* bp) {
	int class = SEGSIZE - 1;

	if (bp == GET_ROOT(class)) {
		GET_ROOT(class) = GET_NEXT(GET_ROOT(class));
		return;
	}
	
	GET_NEXT(GET_PREV(bp)) = GET_NEXT(bp);

	if (GET_NEXT(bp) != NULL)
		GET_PREV(GET_NEXT(bp)) = GET_PREV(bp);
}

static void add_coal_buffer(void* bp) {
	int class = SEGSIZE-1;
	GET_NEXT(bp) = GET_ROOT(class);
	if (GET_ROOT(class) != NULL)
		GET_PREV(GET_ROOT(class)) = bp;
	GET_ROOT(class) = bp;

}

int get_class(size_t size) {
	int temp_val = size;
	int cnt_bit = 0;
	int index;


	if (size < 16)
		return -1;

	if (size == 16)
		return 0;

	while (temp_val != 0)
	{
		cnt_bit++;
		temp_val /= 2;
	}

	index = cnt_bit - 4;

	if (index > SEGSIZE-2)
		return SEGSIZE - 2;

	return index;
}

static size_t available_alloc(void* ptr) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
	size_t size = GET_SIZE(HDRP(ptr));

	if (prev_alloc && next_alloc)
		return 0;
	else if (!prev_alloc && next_alloc)
		return 1;
	else if (prev_alloc && !next_alloc) {
		if (PREV_BLKP(heap_tail) == ptr)
			return 0;
		else
			return 2;
	}
	else if (!prev_alloc && !next_alloc) {
		if (PREV_BLKP(heap_tail) == ptr)
			return 1;
		else
			return 3;
	} 
	else
		return 0;
}

static size_t available_size(void* ptr) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
	size_t size = GET_SIZE(HDRP(ptr));

	if (prev_alloc && next_alloc)
		return size;
	else if (!prev_alloc && next_alloc)
		return ((GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(ptr))));
	else if (prev_alloc && !next_alloc)
		return ((GET_SIZE(HDRP(NEXT_BLKP(ptr))) + GET_SIZE(HDRP(ptr))));
	else
		return ((GET_SIZE(HDRP(NEXT_BLKP(ptr))) + GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(ptr))));
}

	static void* default_realloc(void* ptr, size_t size) {
		void * newptr = mm_malloc(size);
		size_t oldsize = GET_SIZE(HDRP(ptr));
		if (newptr == NULL)
			return NULL;

		memcpy(newptr, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));

		mm_free(ptr);

		return newptr;
	}

	void mm_checkheap(int _LINE) {
		printf("\nCHECKHEAP at Line No.%d\n", _LINE);
		char* ptr = heap_listp + ((2 + SEGSIZE) * WSIZE);
		while (GET_SIZE(HDRP(ptr)) > 0) { // Itearate until it reaches the epliogue header of the heap
			if (ptr < (char*)mem_heap_lo() || ptr >(char*)mem_heap_hi()) { // Check if current pointer is out of heap boundary
				printf("%p NOT IN THE CURRENT HEAP BOUNDARY", ptr);
			}
			if ((size_t)ptr % 8 != 0) { // Check if current pointer is not aligned with 8 bytes
				printf("%p NOT ALIGNED WITH DOUBLE WORD SIZE", ptr);
			}
			if (GET(HDRP(ptr)) != GET(FTRP(ptr))) { // Check if the header and footer of same pointer is not matched
				printf("%p NOT MATCHED WITH HEADER AND FOOTER", ptr);
			}
			if (!GET_ALLOC(ptr) && !GET_ALLOC(NEXT_BLKP(ptr))) { // Check if the pointer's contiguous free blocks are not merged
				printf("%p NOT MERGED WITH NEXT FREE BLOCK", ptr);
			}

			ptr = (char*)NEXT_BLKP(ptr);
		}
	}