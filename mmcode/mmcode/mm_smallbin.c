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
#define MIN_BLOCK_SIZE  32
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define WSIZE 8             
#define DSIZE 16             
#define CHUNKSIZE (1 << 12) 

#define MAX(x, y) (x > y ? x : y)
#define PACK(size, alloc) (size | alloc)                              
#define GET(p) (*(size_t *)(p))                                 
#define PUT(p, val) (*(size_t *)(p) = (size_t)(val))                    
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
#define SEGSIZE 32
#define GET_ROOT(class)(*(void**)((char *)(heap_listp +  (2 * WSIZE)) + (class * WSIZE)))

// coalesce buffer
#define IS_BUFFER(p) ((GET(HDRP(p))>>1)&0x1) // 1 -> in buffer, 0 -> out buffer

static void* extend_heap(size_t words);
static void* coalesce(void* bp);
static void* find_fit(size_t asize);
static void* place(void* bp, size_t asize);
static void* heap_listp = NULL;
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

// small bin
static void* mm_small_malloc(size_t asize);
static void mm_small_free(void* bp);
static void* bin_place(void* bp, size_t asize);
static void create_small_bin(size_t extend_size);
static void rm_small_bin(void* bp);
static void add_small_bin(void* bp);
static void* find_small(size_t asize);
static void* find_small_in_zero(size_t asize);

static long malloc_time_total = 0;
static int malloc_count = 0;
static long free_time_total = 0;
static int free_count = 0;
static long init_time_total = 0;
static int init_count = 0;
static int small_bin_count = 1;

void print_average_times(void) {
	if (malloc_count > 0) {
		//fprintf(stderr, "Average malloc time: %lf microseconds\n", (double)malloc_time_total / malloc_count);
		//fprintf(stderr, "total malloc time: %lf microseconds\n", (double)malloc_time_total);
		//fprintf(stderr, "total malloc count: %d \n", malloc_count);
	}
	if (free_count > 0) {
		//printf("Average free time: %lf microseconds\n", (double)free_time_total / free_count);
		//printf("total free time: %lf microseconds\n", (double)free_time_total);
		//fprintf(stderr, "total free count: %d \n", free_count);
	}
	if (init_count > 0) {
		//printf("Average init time: %lf microseconds\n", (double)init_time_total / init_count);
		//printf("total init time: %lf microseconds\n", (double)init_time_total);
	}
}


int mm_init(void) {
	if ((heap_listp = mem_sbrk((SEGSIZE + 4) * WSIZE)) == (void*)-1) {
		fprintf(stderr, "ERROR: mem_init failed to extend heap.\n");
		return -1;
	}

	PUT(heap_listp, 0);
	PUT(heap_listp + (1 * WSIZE), PACK((SEGSIZE + 2) * WSIZE, 1));			// prologue header
	for(int i = 0; i < SEGSIZE; i++)
		PUT(heap_listp + ((2 + i) * WSIZE), NULL);
	PUT(heap_listp + ((2 + SEGSIZE) * WSIZE), PACK((SEGSIZE + 2) * WSIZE, 1));	// prologue footer	
	PUT(heap_listp + ((3 + SEGSIZE) * WSIZE), PACK(0, 1));		// epilogue header
	heap_tail = heap_listp + (4 + SEGSIZE) * WSIZE;
	

	return 0;
}

void* mm_malloc(size_t size) {
	//struct timeval start, end;
	//gettimeofday(&start, NULL);
	size_t asize;
	size_t extendsize;
	char *bp;
	char *ptr;
	//print_average_times();
	//malloc_count++;
	//printf("malloc size: %d, malloc count: %d\n", size, malloc_count);

	coalesce_buffer();

	if (size == 0)
		return NULL;

	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = ALIGN(2*WSIZE + size);

	if (asize <= 112) {
		return mm_small_malloc(asize);
	}


	if ((bp = find_fit(asize)) != NULL) {
		ptr = place(bp, asize);
		//gettimeofday(&end, NULL);
		//malloc_time_total += ((end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec) - start.tv_usec;
		//malloc_count++;
		return ptr;
	}
	else {
		size_t new_size = asize;
		if (!GET_ALLOC(HDRP(PREV_BLKP(heap_tail)))) {
			size_t end_size = GET_SIZE(HDRP(PREV_BLKP(heap_tail)));
			if (asize >= end_size) {
				//printf("asize: %d, end_size: %d, prev_alloc: %d, is_buffer: %d \n", asize, end_size, GET_ALLOC(HDRP(PREV_BLKP(heap_listp))), IS_BUFFER(PREV_BLKP(heap_tail)));
				bp = PREV_BLKP(heap_tail);
				rm_free_block(bp);
				new_size = asize - end_size;
				//printf("new_size = %d \n", new_size);
				if (mem_sbrk(new_size) == NULL)
					return NULL;

				PUT(HDRP(bp), PACK(asize, 1));
				PUT(FTRP(bp), PACK(asize, 1));
				PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
				heap_tail += new_size;
				return bp;
			}
		}
		else {
			if ((bp = mem_sbrk(asize)) == NULL)
			return NULL;

		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
		heap_tail += asize;
		//gettimeofday(&end, NULL);
		//malloc_time_total += ((end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec) - start.tv_usec;
		//malloc_count++;
		return bp;
		}
	}

}



static void* mm_small_malloc(size_t asize) {
	if (GET_ROOT(0) == NULL) {
		create_small_bin(CHUNKSIZE);
	}
	
	void* bp;
	void* ptr;
	//printf("asize = %d\n", asize);
	if ((bp = find_small(asize)) != NULL) {
		//printf("find_small class : %d\n", get_class(asize));
		rm_free_block(bp);
		//printf("rm small free block complete\n");
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));


		//gettimeofday(&end, NULL);
		//malloc_time_total += ((end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec) - start.tv_usec;
		//malloc_count++;
		return bp;
	}
	else {
		if ((bp = find_small_in_zero(asize)) != NULL) {
			//printf("find_bin class\n");
			ptr = bin_place(bp, asize);
			return ptr;
		}
		else {
			create_small_bin(CHUNKSIZE);
			if ((bp = find_small_in_zero(asize)) != NULL) {
				//printf("create_bin class\n");
				ptr = bin_place(bp, asize);
				return ptr;
			}
		}
	}
	return NULL;
}


static void mm_small_free(void* bp) {
	size_t size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));

	add_free_block(bp);
}

static void* bin_place(void* bp, size_t asize) {
	rm_small_bin(bp);

	size_t csize = GET_SIZE(HDRP(bp));

	if ((csize - asize) < 2 * DSIZE) {
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));

		return bp;
	}
	else {
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));

		/* split the block */
		PUT(HDRP(NEXT_BLKP(bp)), PACK(csize - asize, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(csize - asize, 0));

		if ((csize - asize) > 112)
			add_small_bin(NEXT_BLKP(bp));
		else
			add_free_block(NEXT_BLKP(bp));

		return bp;
	}

}


static void create_small_bin(size_t extend_size) {
	void* bp;
	if(extend_size == CHUNKSIZE) {
		extend_size == small_bin_count * CHUNKSIZE;
	}

	if ((bp = mem_sbrk(extend_size)) == NULL)
		return NULL;

	heap_tail += extend_size;

	PUT(HDRP(bp), PACK(extend_size, 1));
	PUT(FTRP(bp), PACK(extend_size, 1));

	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

	bp += WSIZE;
	PUT(HDRP(bp), PACK(extend_size - DSIZE, 0));
	PUT(FTRP(bp), PACK(extend_size - DSIZE, 0));

	add_small_bin(bp);
	small_bin_count++;
}

static void rm_small_bin(void* bp) {

	int class = 0;

	if (bp == GET_ROOT(class)) {
		GET_ROOT(class) = GET_NEXT(GET_ROOT(class));
		return;
	}

	GET_NEXT(GET_PREV(bp)) = GET_NEXT(bp);

	if (GET_NEXT(bp) != NULL)
		GET_PREV(GET_NEXT(bp)) = GET_PREV(bp);
}

static void add_small_bin(void* bp) {
	int class = 0;

	GET_NEXT(bp) = GET_ROOT(class);
	if (GET_ROOT(class) != NULL)
		GET_PREV(GET_ROOT(class)) = bp;
	GET_ROOT(class) = bp;
}

static void* find_small(size_t asize) {
	int class = get_class(asize);
	void* bp = GET_ROOT(class);

	if (bp != NULL)
		return bp;

	return NULL;
}

static void* find_small_in_zero(size_t asize) {
	int class = get_class(asize);
	void* bp = GET_ROOT(0);
	while (bp != NULL) {
		if ((asize <= GET_SIZE(HDRP(bp))))
			return bp;

		bp = GET_NEXT(bp);
	}
}



void mm_free(void* bp) {
	size_t size = GET_SIZE(HDRP(bp));
	//int buf = IS_BUFFER(bp);
	if ((size <= 112) /*&& buf*/) {
		mm_small_free(bp);
		return;
	}

	PUT(HDRP(bp), PACK(size, 2));
	PUT(FTRP(bp), PACK(size, 2));
	add_coal_buffer(bp);
}

void *mm_realloc(void *ptr, size_t size)
{
    size_t asize;
    void *newptr;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + DSIZE + DSIZE -1) / DSIZE);

    if (ptr == NULL)
        return mm_malloc(size);

    if (size <= 0)
    {
        mm_free(ptr);
        return 0;
    }

	
	if(size <= 112) {
		newptr = mm_small_malloc(size);
		size_t oldsize = GET_SIZE(HDRP(ptr));
		if (newptr == NULL)
			return NULL;

		memcpy(newptr, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));

		mm_free(ptr);

		return newptr;
	}
	

    size_t oldsize = GET_SIZE(HDRP(ptr));
    if (asize <= oldsize)
        return ptr;

    coalesce_buffer();
    int prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t prev_size = GET_SIZE(FTRP(PREV_BLKP(ptr)));
    int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));

    if (PREV_BLKP(heap_tail) == ptr) {
        next_alloc = 1;
    }

    if (!next_alloc)
    {
        if ((oldsize + next_size) >= asize)
        {
            rm_free_block(NEXT_BLKP(ptr));
            PUT(HDRP(ptr), PACK((oldsize + next_size), 1));
            PUT(FTRP(ptr), PACK((oldsize + next_size), 1));
            return ptr;
        }
    }
    else if ((!next_alloc) && (!prev_alloc) && ((oldsize + prev_size + next_size) >= asize))
    {
        void *prev_block = PREV_BLKP(ptr);
        if (prev_size >= oldsize)
        {
            rm_free_block(PREV_BLKP(ptr));
            rm_free_block(NEXT_BLKP(ptr));
            if ((prev_size + oldsize + next_size - asize) < 2 * DSIZE)
            {
                memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
                PUT(HDRP(prev_block), PACK((prev_size + oldsize + next_size - asize), 1));
                PUT(FTRP(prev_block), PACK((prev_size + oldsize + next_size - asize), 1));
            }
            else
            {
                memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
                PUT(HDRP(prev_block), PACK(asize, 1));
                PUT(FTRP(prev_block), PACK(asize, 1));

                PUT(HDRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize + next_size - asize), 0));
                PUT(FTRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize + next_size - asize), 0));

                add_free_block(NEXT_BLKP(prev_block));
                return prev_block;
            }
        }
        else if (prev_size < oldsize)
        {
            rm_free_block(PREV_BLKP(ptr));
            rm_free_block(NEXT_BLKP(ptr));
            int total_movesize = GET_SIZE(HDRP(ptr)) - DSIZE;
            int sep_movesize = GET_SIZE(HDRP(prev_block));
            int n = total_movesize / sep_movesize;

            for (int i = 0; i < n; i++)
            {
                memcpy(prev_block + i * sep_movesize, ptr + i * sep_movesize, sep_movesize);
            }
            memcpy(prev_block + n * sep_movesize, ptr + n * sep_movesize, total_movesize - (sep_movesize * n));

            if (((prev_size + oldsize + next_size) - asize) < 2 * DSIZE)
            {
                PUT(HDRP(prev_block), PACK((prev_size + oldsize + next_size), 1));
                PUT(FTRP(prev_block), PACK((prev_size + oldsize + next_size), 1));
                return prev_block;
            }
            else
            {
                PUT(HDRP(prev_block), PACK(asize, 1));
                PUT(FTRP(prev_block), PACK(asize, 1));

                PUT(HDRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize + next_size) - asize, 0));
                PUT(FTRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize + next_size) - asize, 0));
                add_free_block(NEXT_BLKP(prev_block));

                return prev_block;
            }
        }
    }
    else if ((next_alloc) && (!prev_alloc) && ((oldsize + prev_size) >= asize))
    {
        void *prev_block = PREV_BLKP(ptr);
        if (prev_size >= oldsize)
        {
            rm_free_block(PREV_BLKP(ptr));
            if ((prev_size + oldsize + next_size - asize) < 2 * DSIZE)
            {
                memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
                PUT(HDRP(prev_block), PACK((prev_size + oldsize - asize), 1));
                PUT(FTRP(prev_block), PACK((prev_size + oldsize - asize), 1));
            }
            else
            {
                memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
                PUT(HDRP(prev_block), PACK(asize, 1));
                PUT(FTRP(prev_block), PACK(asize, 1));

                PUT(HDRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize - asize), 0));
                PUT(FTRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize - asize), 0));

                add_free_block(NEXT_BLKP(prev_block));
                return prev_block;
            }
        }
        else if (prev_size < oldsize)
        {
            rm_free_block(PREV_BLKP(ptr));
            int total_movesize = GET_SIZE(HDRP(ptr)) - DSIZE;
            int sep_movesize = GET_SIZE(HDRP(prev_block));
            int n = total_movesize / sep_movesize;

            for (int i = 0; i < n; i++)
            {
                memcpy(prev_block + i * sep_movesize, ptr + i * sep_movesize, sep_movesize);
            }
            memcpy(prev_block + n * sep_movesize, ptr + n * sep_movesize, total_movesize - (sep_movesize * n));

            if (((prev_size + oldsize) - asize) < 2 * DSIZE)
            {
                PUT(HDRP(prev_block), PACK((prev_size + oldsize), 1));
                PUT(FTRP(prev_block), PACK((prev_size + oldsize), 1));
                return prev_block;
            }
            else
            {
                PUT(HDRP(prev_block), PACK(asize, 1));
                PUT(FTRP(prev_block), PACK(asize, 1));

                PUT(HDRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize) - asize, 0));
                PUT(FTRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize) - asize, 0));
                add_free_block(NEXT_BLKP(prev_block));

                return prev_block;
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
		size_t totalsize = 0;
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
	else if (asize >= 112) {
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
	if (size < 32) return -1;

	int ind = 5;
	if (size > 112) {
		while ((1 << ind) < size) {
			ind++;
		}
		ind = ind + 6;
	}
	else {
		ind = (size - 24) / 8;
	}


	if (ind > SEGSIZE - 2)
		return SEGSIZE - 2;

	return ind;

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