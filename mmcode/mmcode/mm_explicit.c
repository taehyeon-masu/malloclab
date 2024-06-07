#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

#define ALIGNMENT 8

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

static void* extend_heap(size_t words);
static void* coalesce(void* bp);
static void* find_fit(size_t asize);
static void place(void* bp, size_t asize);
static void* heap_listp;

static int num;
static unsigned long long msize;

// Explicit
static void* free_listp; 	
static void rm_free_block(void *bp); 	
static void add_free_block(void *bp); 	


int mm_init(void) {
	if ((heap_listp = mem_sbrk(8 * WSIZE)) == (void *)-1)
		return -1;
	PUT(heap_listp, 0);
	PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
	PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
	PUT(heap_listp + (3 * WSIZE), PACK(2*DSIZE, 0));	
	PUT(heap_listp + (4 * WSIZE), NULL);			
	PUT(heap_listp + (5 * WSIZE), NULL);			
	PUT(heap_listp + (6 * WSIZE), PACK(2*DSIZE, 0));	
	PUT(heap_listp + (7 * WSIZE), PACK(0, 1));

	free_listp = heap_listp +  (4 * WSIZE); 			

	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return -1;
	return 0;
}

void* mm_malloc(size_t size) {
	size_t asize;
	size_t extendsize;
	char *bp;

	if (size == 0)
		return NULL;

	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + DSIZE + DSIZE -1) / DSIZE);

	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;
	place(bp, asize);
	return bp;
}

void mm_free(void* bp) {
	size_t size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}

void* mm_realloc(void* ptr, size_t size) {
	if (ptr == NULL)
		return mm_malloc(size);

	if (size <= 0) {
		mm_free(ptr);
		return 0;
	}

	void* newptr = mm_malloc(size);
	if (newptr == NULL)
		return NULL;

	size_t copySize = GET_SIZE(HDRP(ptr)) - DSIZE;
	if (size < copySize)
		copySize = size;

	memcpy(newptr, ptr, copySize);

	mm_free(ptr);

	return newptr;
}

static void* extend_heap(size_t words) {
	char *bp;

	size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;

	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1));
	
	num += 1;
	msize += size;
	printf("extend count : %d, size : %llu \n", num, msize);

	return coalesce(bp);
}

static void* coalesce(void* bp) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

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

static void* find_fit(size_t asize)
{
    	void *bp = free_listp;
	while(bp != NULL) {
		if((asize <= GET_SIZE(HDRP(bp))))
			return bp;
		bp = GET_NEXT(bp);
	}

    	return NULL;
}

static void place(void* bp, size_t asize) {
	rm_free_block(bp);

	size_t csize = GET_SIZE(HDRP(bp));

	if ((csize - asize) >= (2*DSIZE)) {
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));

		PUT(HDRP(NEXT_BLKP(bp)), PACK((csize - asize), 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK((csize - asize), 0));
		add_free_block(NEXT_BLKP(bp)); 
	}
	else {
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
}

static void rm_free_block(void *bp) {
	if(bp == free_listp) {		
		free_listp = GET_NEXT(free_listp);
		return;
	}

	GET_NEXT(GET_PREV(bp)) = GET_NEXT(bp);

	if (GET_NEXT(bp) != NULL)
		GET_PREV(GET_NEXT(bp)) = GET_PREV(bp);
}

static void add_free_block(void *bp) {
	GET_NEXT(bp) = free_listp;
	if (free_listp != NULL)
		GET_PREV(free_listp) = bp;
	free_listp = bp;
}
