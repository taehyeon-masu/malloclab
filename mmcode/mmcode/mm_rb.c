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

/*TREE의 ROOT ADDR.*/
#define TREE_ROOT 8
/*|Parent Addr.|Left Addr.|Right Addr.|*/
#define TREEOVERHEAD 24	
/* OVERHEAD */
#define OVERHEAD 8

/* left allocation bit in Header & Footer */
#define RED 1
#define BLACK 0

#define MAX(x, y) (x > y ? x : y)
#define PACK(size, alloc) (size | alloc)                              
#define GET(p) (*(unsigned int *)(p))                                 
#define PUT(p, val) (*(unsigned int *)(p) = (unsigned int)(val))                    
#define GET_SIZE(p) (GET(p) & ~0x7)                                  
#define GET_ALLOC(p) (GET(p) & 0x1)                                  
#define HDRP(bp) ((char *)(bp)-WSIZE)                                
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)          
#define NEXT_BLKP(bp) ((block_t*)((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))) 
#define PREV_BLKP(bp) ((block_t*)((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE))) )
#define GET_SIZE_ALLOC(p) (GET_SIZE(p)|GET_ALLOC(p))

/* DSIZE (TREE Addr.) */
typedef unsigned long long block_t;

/* GET TREE NODE ADDR.&SIZE MACRO */
#define GET_ADDRESS(p) ((block_t *)(p))
#define PUT_ADDRESS(p,val) (*(block_t *)(p) = (block_t)(val))
#define IS_RED(p) ((GET(HDRP(p))>>1)&0x1)
#define PARENT_BLKP(bp) ((block_t*)((char*)(bp)))
#define LEFT_BLKP(bp) ((char*)(bp)+DSIZE)
#define RIGHT_BLKP(bp) ((block_t*)((char*)(bp)+2*DSIZE))
#define PARENT_BLK(bp) (*(block_t*)((char*)(bp)))
#define LEFT_BLK(bp) (*(block_t*)((((char*)(bp))+DSIZE)))
#define RIGHT_BLK(bp) (*(block_t*)((((char*)(bp))+2*DSIZE)))

static void* extend_heap(size_t words);
static void* coalesce(void* bp);
static void* find_fit(size_t asize);
static void place(void* bp, size_t asize);
static char* heap_listp;

static int num;
static unsigned long long msize;

// rb tree
static void insert_tree(block_t *bp);
static void delete_tree(block_t *bp);
static void rotate_left(block_t *bp);
static void rotate_right(block_t * bp);
static void transplant(block_t* u, block_t* v);
static void* find_fit(size_t size);
static void delete_fixup(block_t *bp, block_t *par);
static void insert_fixup(block_t *bp);
static block_t* minimum(block_t *bp);

int mm_init(void)
{
	char * bp = NULL;
	if ((heap_listp = mem_sbrk(4*WSIZE+3 * DSIZE)) == NULL)
		return -1;
	PUT(heap_listp, 0);
	PUT(heap_listp + WSIZE, PACK(TREEOVERHEAD + OVERHEAD, 1));  
	PUT_ADDRESS(heap_listp + DSIZE, NULL);     /* root tree address*/
	PUT_ADDRESS(heap_listp + 2 * DSIZE, NULL); /* left is null */
	PUT_ADDRESS(heap_listp + 3 * DSIZE, NULL); /* right is null */
	PUT(heap_listp + 4 * DSIZE, PACK(TREEOVERHEAD + OVERHEAD, 1));  
	PUT(heap_listp + WSIZE + 4 * DSIZE, PACK(0, 1));   

	heap_listp += DSIZE;

	if ((bp = extend_heap(CHUNKSIZE / WSIZE)) == NULL)
		return -1;

	return 0;
}

void *mm_malloc(size_t size)
{
	size_t asize;      
	size_t extendsize; 
	char *bp;

	if (size <= 0)
		return NULL;

	if (size <= DSIZE + TREEOVERHEAD)
		asize = DSIZE + OVERHEAD + TREEOVERHEAD;
	else
		asize = DSIZE * ((size +TREEOVERHEAD+(OVERHEAD)+(DSIZE - 1)) / DSIZE);

	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
			return bp;
	}
	extendsize = MAX(asize, CHUNKSIZE);

	if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {

		return NULL;
	}

	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);		
		return bp;
	}

}

void mm_free(void* bp) {
	size_t size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}

void *mm_realloc(void *ptr, size_t size)
{
	void *newp;
	size_t copySize;
	copySize = GET_SIZE(HDRP(ptr));
	
	if ((newp = mm_malloc(size)) == NULL) {
		printf("ERROR: mm_malloc failed in mm_realloc\n");
		exit(1);
	}
	
	if (size < copySize)
		copySize = size;
	memcpy(newp, ptr, copySize);
	mm_free(ptr);
	return newp;
}

static void rotate_right(block_t *parent) {
	block_t * me = NULL;
	
	me = LEFT_BLK(parent);

	/* 부모의 왼쪽 자식을 내 오른쪽 자식으로 바꾼다. */
	PUT_ADDRESS(LEFT_BLKP(parent), RIGHT_BLK(me));
	
	/* 내 오른쪽 자식의 부모를 나의 부모로 바꾼다. */
	if(RIGHT_BLK(me) != NULL)
		PUT_ADDRESS(PARENT_BLKP(RIGHT_BLK(me)), parent);
	
	/* 내 부모를 내 부모의 부모로 바꾼다. */
	PUT_ADDRESS(PARENT_BLKP(me), PARENT_BLK(parent));

	
	/* 내 부모가 ROOT NODE라면 나를 ROOT NODE로 바꾼다. */
	/* 부모가 조부모의 오른쪽 자식이라면 조부모의 오른쪽 자신을 나로 바꾸고,
	 * 왼쪽 자식이라면 조부모의 왼쪽 자신을 나로 바꾼다. */

	if (PARENT_BLK(parent) == NULL)
		PUT_ADDRESS(heap_listp, me);
	else {
		if(parent == RIGHT_BLK(PARENT_BLK(parent)))
			PUT_ADDRESS(RIGHT_BLKP(PARENT_BLK(parent)), me);
		else
			PUT_ADDRESS(LEFT_BLKP(PARENT_BLK(parent)), me);
	}
	
	/* 내 오른쪽 자식을 부모로 바꾸고, 부모의 부모를 나로 바꾼다. */
	PUT_ADDRESS(RIGHT_BLKP(me), parent);
	PUT_ADDRESS(PARENT_BLKP(parent), me);
	return;
}

static void rotate_left(block_t *parent) {
	block_t * me = NULL;

	me = RIGHT_BLK(parent);
	
	/* 부모의 오른쪽 자식을 내 왼쪽 자식으로 바꾼다. */
	PUT_ADDRESS(RIGHT_BLKP(parent), LEFT_BLK(me));

	/* 내 왼쪽 자식의 부모를 나의 부모로 바꾼다. */
	if(LEFT_BLK(me) != NULL) {
		PUT_ADDRESS(PARENT_BLKP(LEFT_BLK(me)), parent);
	}
	
	/* 내 부모를 내 부모의 부모로 바꾼다. */

	PUT_ADDRESS(PARENT_BLKP(me), PARENT_BLK(parent));

	if (PARENT_BLK(parent) == NULL) {
		PUT_ADDRESS(heap_listp, me);
	}
	else {
		if(parent == LEFT_BLK(PARENT_BLK(parent)))
			PUT_ADDRESS(LEFT_BLKP(PARENT_BLK(parent)), me);
		else
			PUT_ADDRESS(RIGHT_BLKP(PARENT_BLK(parent)), me);
	}
	
	/* 내 왼쪽 자식을 부모로 바꾸고, 부모의 부모를 나로 바꾼다. */
	PUT_ADDRESS(LEFT_BLKP(me), parent);
	PUT_ADDRESS(PARENT_BLKP(parent), me);

	return;
}

static void* find_fit(size_t size) {
	block_t* bp;
	block_t* temp = NULL;
	bp = PARENT_BLK(heap_listp);

	while (bp != NULL) {
		if (GET_SIZE(HDRP(bp)) < size) {
			bp = RIGHT_BLK(bp);
			continue;
		}

		if (GET_SIZE(HDRP(bp)) == size) {
			temp = bp;
			delete_tree(temp);

			return temp;
		}

		else {
			temp = bp;
			if (bp != NULL) {
				bp = LEFT_BLK(bp);
				continue;
			}
			else
				break;
		}
	}

	if (temp != NULL) 
		delete_tree(temp);

	return temp;
}

static void *coalesce(void *bp) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && next_alloc) {
		insert_tree(bp);
		return bp;
	}
	else if (prev_alloc && !next_alloc) {
		delete_tree(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
		insert_tree(bp);
	}
	else if (!prev_alloc && next_alloc) {
		delete_tree(PREV_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
		insert_tree(bp);
	}
	else {
		delete_tree(NEXT_BLKP(bp));
		delete_tree(PREV_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
		insert_tree(bp);
	}
	
	return bp;
}

static void *extend_heap(size_t words) {
	char *bp;
	size_t size;

	size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
	if((bp = mem_sbrk(size)) == (void *)-1)
		return NULL;

	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
	
	num += 1;
	msize += size;

	printf("extend num : %d, size : %llu \n", num, msize);

	return coalesce(bp);
}

static void place(void *bp, size_t asize) {
	size_t csize = GET_SIZE(HDRP(bp));

	if ((csize - asize) >= (DSIZE + TREEOVERHEAD + OVERHEAD)) {
		PUT(HDRP(bp), PACK(asize+DSIZE, 1));
		PUT(FTRP(bp), PACK(asize+DSIZE, 1));
		bp = NEXT_BLKP(bp);

		PUT(HDRP(bp), PACK(csize - asize-DSIZE, 0));
		PUT(FTRP(bp), PACK(csize - asize-DSIZE, 0));

		insert_tree(bp);
	}
	else {
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
}

static void insert_tree(block_t* bp) {
	PUT(HDRP(bp), PACK(GET_SIZE_ALLOC(HDRP(bp)), RED << 1));
	PUT(FTRP(bp), PACK(GET_SIZE_ALLOC(FTRP(bp)), RED << 1));
	block_t* x = GET_ADDRESS(heap_listp);
	x = *x;
	block_t *y = NULL;

	while ((x) != NULL) {
		y = x;

		if (GET_SIZE(HDRP(bp)) < GET_SIZE(HDRP(x)))
			x = LEFT_BLK(x);
		else
			x = RIGHT_BLK(x);
	}

	PARENT_BLK(bp) = y;

	if (y == NULL) {
		PUT_ADDRESS(heap_listp, bp);
	}
	else {
		if (GET_SIZE(HDRP(bp)) < GET_SIZE(HDRP(y)))
			PUT_ADDRESS(LEFT_BLKP(y), bp);
		else
			PUT_ADDRESS(RIGHT_BLKP(y), bp);
	}

	PUT_ADDRESS(LEFT_BLKP(bp), 0);
	PUT_ADDRESS(RIGHT_BLKP(bp), 0);
	PUT(HDRP(bp), PACK(GET_SIZE_ALLOC(HDRP(bp)), RED << 1));
	PUT(FTRP(bp), PACK(GET_SIZE_ALLOC(FTRP(bp)), RED << 1));

	insert_fixup(bp);
	return;
}

static void delete_tree(block_t *z) {
	block_t* y = z;
	block_t*par = NULL;
	block_t* x = NULL;
	int yoc;
	yoc = IS_RED(y);
	if (LEFT_BLK(z) == NULL) {
		x = RIGHT_BLK(z);
		par = PARENT_BLK(z);
		transplant(z, RIGHT_BLK(z));
	}
	else if (RIGHT_BLK(z) == NULL) {
		par = PARENT_BLK(z);
		x = LEFT_BLK(z);
		transplant(z, LEFT_BLK(z));
	}
	else {
		y = minimum(RIGHT_BLK(z));
		yoc = IS_RED(y);
		x = RIGHT_BLK(y);
		if (z == PARENT_BLK(y)) {
			if (x != NULL)
				PUT_ADDRESS(PARENT_BLKP(x), y);
			par = y;
		}
		else {
			transplant(y, RIGHT_BLK(y));
			par = PARENT_BLK(y);
			PUT_ADDRESS(RIGHT_BLKP(y), RIGHT_BLK(z));
			PUT_ADDRESS(PARENT_BLKP(RIGHT_BLK(y)), y);

		}
		transplant(z, y);
		PUT_ADDRESS(LEFT_BLKP(y), LEFT_BLK(z));
		PUT_ADDRESS(PARENT_BLKP(LEFT_BLK(y)), y);
		PUT(HDRP(y), PACK(GET_SIZE_ALLOC(HDRP(y)), IS_RED(z) << 1));
		PUT(FTRP(y), PACK(GET_SIZE_ALLOC(FTRP(y)), IS_RED(z) << 1));
	}

	if (yoc == BLACK)
		delete_fixup(x, par);
	return;
}

static block_t* minimum(block_t *bp) {
	while (LEFT_BLK(bp) != NULL)
		bp = LEFT_BLK(bp);
	return bp;
}

static void delete_fixup(block_t * x, block_t *par) {
	block_t* w = NULL;
	while (x != PARENT_BLK(heap_listp) && (x == NULL || !IS_RED(x))) {

		if (x == LEFT_BLK(par)) {
			w = RIGHT_BLK(par);
			if (w != NULL && IS_RED(w)) {
				PUT(HDRP(w), PACK(GET_SIZE_ALLOC(HDRP(w)), BLACK << 1));
				PUT(FTRP(w), PACK(GET_SIZE_ALLOC(FTRP(w)), BLACK << 1));
				PUT(HDRP(par), PACK(GET_SIZE_ALLOC(HDRP((par))), RED << 1));
				PUT(FTRP(par), PACK(GET_SIZE_ALLOC(FTRP((par))), RED << 1));
				rotate_left(par);
				w = RIGHT_BLK(par);
			}

			if ((RIGHT_BLK(w) == NULL || !IS_RED(RIGHT_BLK(w))) && (LEFT_BLK(w) == NULL || !IS_RED(LEFT_BLK(w)))) {
				PUT(HDRP(w), PACK(GET_SIZE_ALLOC(HDRP(w)), RED << 1));
				PUT(FTRP(w), PACK(GET_SIZE_ALLOC(FTRP(w)), RED << 1));
				x = par;
				par = PARENT_BLK(par);
			}

			else {
				if (RIGHT_BLK(w) == NULL || !IS_RED(RIGHT_BLK(w))) {
					PUT(HDRP(LEFT_BLK(w)), PACK(GET_SIZE_ALLOC(HDRP(LEFT_BLK(w))), BLACK << 1));
					PUT(FTRP(LEFT_BLK(w)), PACK(GET_SIZE_ALLOC(FTRP(LEFT_BLK(w))), BLACK << 1));
					PUT(HDRP(w), PACK(GET_SIZE_ALLOC(HDRP(w)), RED << 1));
					PUT(FTRP(w), PACK(GET_SIZE_ALLOC(FTRP(w)), RED << 1));
					rotate_right(w);
					w = RIGHT_BLK(par);
				}
				PUT(HDRP(w), PACK(GET_SIZE_ALLOC(HDRP(w)), IS_RED(par) << 1));
				PUT(FTRP(w), PACK(GET_SIZE_ALLOC(FTRP(w)), IS_RED(par) << 1));
				PUT(HDRP(par), PACK(GET_SIZE_ALLOC(HDRP(par)), BLACK << 1));
				PUT(FTRP(par), PACK(GET_SIZE_ALLOC(FTRP(par)), BLACK << 1));
				PUT(HDRP(RIGHT_BLK(w)), PACK(GET_SIZE_ALLOC(HDRP(RIGHT_BLK(w))), BLACK << 1));
				PUT(FTRP(RIGHT_BLK(w)), PACK(GET_SIZE_ALLOC(FTRP(RIGHT_BLK(w))), BLACK << 1));
				rotate_left(par);
				x = PARENT_BLK(heap_listp);
			}
		}
		else {
			w = LEFT_BLK(par);
			if (w != NULL && IS_RED(w)) {
				PUT(HDRP(w), PACK(GET_SIZE_ALLOC(HDRP(w)), BLACK << 1));
				PUT(FTRP(w), PACK(GET_SIZE_ALLOC(FTRP(w)), BLACK << 1));
				PUT(HDRP(par), PACK(GET_SIZE_ALLOC(HDRP((par))), RED << 1));
				PUT(FTRP(par), PACK(GET_SIZE_ALLOC(FTRP((par))), RED << 1));
				rotate_right(par);
				w = LEFT_BLK(par);
			}

			if ((RIGHT_BLK(w) == NULL || !IS_RED(RIGHT_BLK(w))) && (LEFT_BLK(w) == NULL || !IS_RED(LEFT_BLK(w)))) {
				PUT(HDRP(w), PACK(GET_SIZE_ALLOC(HDRP(w)), RED << 1));
				PUT(FTRP(w), PACK(GET_SIZE_ALLOC(FTRP(w)), RED << 1));
				x = par;
				par = PARENT_BLK(par);
			}
			else {
				if (LEFT_BLK(w) == NULL || !IS_RED(LEFT_BLK(w))) {
					PUT(HDRP(RIGHT_BLK(w)), PACK(GET_SIZE_ALLOC(HDRP(RIGHT_BLK(w))), BLACK << 1));
					PUT(FTRP(RIGHT_BLK(w)), PACK(GET_SIZE_ALLOC(FTRP(RIGHT_BLK(w))), BLACK << 1));
					PUT(HDRP(w), PACK(GET_SIZE_ALLOC(HDRP(w)), RED << 1));
					PUT(FTRP(w), PACK(GET_SIZE_ALLOC(FTRP(w)), RED << 1));
					rotate_left(w);
					w = LEFT_BLK(par);
				}
				PUT(HDRP(w), PACK(GET_SIZE_ALLOC(HDRP(w)), IS_RED(par) << 1));
				PUT(FTRP(w), PACK(GET_SIZE_ALLOC(FTRP(w)), IS_RED(par) << 1));
				PUT(HDRP(par), PACK(GET_SIZE_ALLOC(HDRP(par)), BLACK << 1));
				PUT(FTRP(par), PACK(GET_SIZE_ALLOC(FTRP(par)), BLACK << 1));
				PUT(HDRP(LEFT_BLK(w)), PACK(GET_SIZE_ALLOC(HDRP(LEFT_BLK(w))), BLACK << 1));
				PUT(FTRP(LEFT_BLK(w)), PACK(GET_SIZE_ALLOC(FTRP(LEFT_BLK(w))), BLACK << 1));
				rotate_right(par);
				x = PARENT_BLK(heap_listp);
			}
		}
	}
	if (x != NULL) {
		PUT(HDRP(x), PACK(GET_SIZE_ALLOC(HDRP(x)), BLACK << 1));
		PUT(FTRP(x), PACK(GET_SIZE_ALLOC(FTRP(x)), BLACK << 1));
	}
	return;
}

static void insert_fixup(block_t* bp) {

	block_t* y = NULL;

	while (PARENT_BLK(bp) && IS_RED(PARENT_BLK(bp))) {
		if (PARENT_BLK(bp) == LEFT_BLK(PARENT_BLK(PARENT_BLK(bp)))) {
			y = (char*)RIGHT_BLK(PARENT_BLK(PARENT_BLK(bp)));

			if (y != NULL && IS_RED(y)) {
				PUT(HDRP(PARENT_BLK(bp)), PACK(GET_SIZE_ALLOC(HDRP((PARENT_BLK(bp)))), BLACK << 1));
				PUT(FTRP(PARENT_BLK(bp)), PACK(GET_SIZE_ALLOC(FTRP((PARENT_BLK(bp)))), BLACK << 1));
				PUT(HDRP(y), PACK(GET_SIZE_ALLOC(HDRP(y)), BLACK << 1));
				PUT(FTRP(y), PACK(GET_SIZE_ALLOC(FTRP(y)), BLACK << 1));
				PUT(HDRP(PARENT_BLK(PARENT_BLK(bp))), PACK(GET_SIZE_ALLOC(HDRP(PARENT_BLK(PARENT_BLK(bp)))), RED << 1));
				PUT(FTRP(PARENT_BLK(PARENT_BLK(bp))), PACK(GET_SIZE_ALLOC(FTRP(PARENT_BLK(PARENT_BLK(bp)))), RED << 1));
				bp = PARENT_BLK(PARENT_BLK(bp));
			}
			else {
				if (bp == RIGHT_BLK(PARENT_BLK(bp))) {
					bp = PARENT_BLK(bp);
					rotate_left(bp);
				}
				PUT(HDRP(PARENT_BLK(bp)), PACK(GET_SIZE_ALLOC(HDRP((PARENT_BLK(bp)))), BLACK << 1));
				PUT(FTRP(PARENT_BLK(bp)), PACK(GET_SIZE_ALLOC(FTRP((PARENT_BLK(bp)))), BLACK << 1));
				PUT(HDRP(PARENT_BLK(PARENT_BLK(bp))), PACK(GET_SIZE_ALLOC(HDRP(PARENT_BLK(PARENT_BLK(bp)))), RED << 1));
				PUT(FTRP(PARENT_BLK(PARENT_BLK(bp))), PACK(GET_SIZE_ALLOC(FTRP(PARENT_BLK(PARENT_BLK(bp)))), RED << 1));
				rotate_right(PARENT_BLK(PARENT_BLK(bp)));

			}
		}

		else {
			y = (char*)LEFT_BLK(PARENT_BLK(PARENT_BLK(bp)));
			if (y != NULL && IS_RED(y)) {
				PUT(HDRP(PARENT_BLK(bp)), PACK(GET_SIZE_ALLOC(HDRP((PARENT_BLK(bp)))), BLACK << 1));
				PUT(FTRP(PARENT_BLK(bp)), PACK(GET_SIZE_ALLOC(FTRP((PARENT_BLK(bp)))), BLACK << 1));
				PUT(HDRP(y), PACK(GET_SIZE_ALLOC(HDRP(y)), BLACK << 1));
				PUT(FTRP(y), PACK(GET_SIZE_ALLOC(FTRP(y)), BLACK << 1));
				PUT(HDRP(PARENT_BLK(PARENT_BLK(bp))), PACK(GET_SIZE_ALLOC(HDRP(PARENT_BLK(PARENT_BLK(bp)))), RED << 1));
				PUT(FTRP(PARENT_BLK(PARENT_BLK(bp))), PACK(GET_SIZE_ALLOC(FTRP(PARENT_BLK(PARENT_BLK(bp)))), RED << 1));
				bp = PARENT_BLK(PARENT_BLK(bp));
			}
			else {
				if (bp == LEFT_BLK(PARENT_BLK(bp))) {
					bp = PARENT_BLK(bp);
					rotate_right(bp);
				}
				PUT(HDRP(PARENT_BLK(bp)), PACK(GET_SIZE_ALLOC(HDRP((PARENT_BLK(bp)))), BLACK << 1));
				PUT(FTRP(PARENT_BLK(bp)), PACK(GET_SIZE_ALLOC(FTRP((PARENT_BLK(bp)))), BLACK << 1));
				PUT(HDRP(PARENT_BLK(PARENT_BLK(bp))), PACK(GET_SIZE_ALLOC(HDRP(PARENT_BLK(PARENT_BLK(bp)))), RED << 1));
				PUT(FTRP(PARENT_BLK(PARENT_BLK(bp))), PACK(GET_SIZE_ALLOC(FTRP(PARENT_BLK(PARENT_BLK(bp)))), RED << 1));
				rotate_left(PARENT_BLK(PARENT_BLK(bp)));
			}
		}
	}
	PUT(HDRP(PARENT_BLK(heap_listp)), PACK(GET_SIZE_ALLOC(HDRP(PARENT_BLK((heap_listp)))), BLACK << 1));
	PUT(FTRP(PARENT_BLK(heap_listp)), PACK(GET_SIZE_ALLOC(FTRP(PARENT_BLK((heap_listp)))), BLACK << 1));
	return;
}

static void transplant(block_t* u, block_t* v) {
	if (PARENT_BLK(u) == NULL) {
		PUT_ADDRESS(heap_listp, v);
	}
	else if (u == LEFT_BLK(PARENT_BLK(u))) {
		PUT_ADDRESS((LEFT_BLKP(PARENT_BLK(u))), v);

	}
	else {
		PUT_ADDRESS((RIGHT_BLKP(PARENT_BLK(u))), v);
	}
	if (v != NULL) {
		PUT_ADDRESS(PARENT_BLKP(v), PARENT_BLK(u));
	}
	return;
}

