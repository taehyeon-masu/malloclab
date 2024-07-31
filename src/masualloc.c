#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include "memlib.h"
// #include <sys/time.h>
#include <stdint.h>
// masualloc.c
#include <stddef.h>

extern void *masu_malloc(size_t size);
extern void masu_free(void *ptr);
extern void *masu_realloc(void *ptr, size_t size);
extern void *masu_calloc(size_t nmemb, size_t size);
extern char *masu_strdup(const char *s);

/*
unsigned long long malloc_time = 0, malloc_count = 0;
unsigned long long free_time = 0, free_count = 0;
unsigned long long realloc_time = 0, realloc_count = 0;
unsigned long long calloc_time = 0, calloc_count = 0;
unsigned long long total_time = 0;
unsigned long long get_class_time = 0, get_class_count = 0;
unsigned long long place_time = 0, place_count = 0;
unsigned long long delay_class_coalesce_time = 0, delay_class_coalesce_count = 0;
unsigned long long find_fit_time = 0, find_fit_count = 0;
*/


void *malloc(size_t size)
{
	//if(size>120){
		//fprintf(stderr, "malloc called with size %zu\n", size);
	//}
	//fprintf(stderr, "malloc called with size %zu\n", size);
	////struct timeval start, end;
	////gettimeofday(&start, NULL);
	void *ptr = masu_malloc(size);

	////gettimeofday(&end, NULL);
	////long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
	// malloc_time += time_taken;
	// malloc_count++;

	return ptr;
}

void free(void *ptr)
{
	//fprintf(stderr, "free called \n");
	////struct timeval start, end;
	////gettimeofday(&start, NULL);

	masu_free(ptr);

	////gettimeofday(&end, NULL);
	////long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
	// free_time += time_taken;
	// free_count++;
}

void *realloc(void *ptr, size_t size)
{
	//fprintf(stderr, "masu_realloc called with ptr %p and size %zu\n", ptr, size);
	//     fprintf(stderr, "realloc called size : %zu, ptr : %p\n", size, ptr);
	//     if(ptr != NULL){
	//    }
	////struct timeval start, end;
	////gettimeofday(&start, NULL);

	void *new_ptr = masu_realloc(ptr, size);

	////gettimeofday(&end, NULL);
	////long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
	// realloc_time += time_taken;
	// realloc_count++;

	return new_ptr;
}

void *calloc(size_t nmemb, size_t size)
{
	//fprintf(stderr, "masu_calloc called with nmemb * size %lu x %lu\n", nmemb, size);
	//   fprintf(stderr, "calloc called \n");
	////struct timeval start, end;
	////gettimeofday(&start, NULL);

	void *ptr = masu_calloc(nmemb, size);

	////gettimeofday(&end, NULL);
	////long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
	// calloc_time += time_taken;
	// calloc_count++;

	return ptr;
}



char *strdup(const char *s)
{
	// fprintf(stderr, "strdup called \n");
	return masu_strdup(s);
}


/*
void __attribute__((destructor)) cleanup() {
	print_average_times();
}
 */

#define ALIGNMENT 8
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define MIN_BLOCK_SIZE 32
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define WSIZE 8	 // word size
#define DSIZE 16 // double word size
#define CHUNKSIZE (1 << 12)

#define MAX(x, y) (x > y ? x : y)
#define PACK(size, alloc) (size | alloc)								// size와 할당 비트를 결합, header와 footer에 저장할 값
#define GET(p) (*(size_t *)(p))											// p가 참조하는 워드 반환 (포인터라서 직접 역참조 불가능 -> 타입 캐스팅)
#define PUT(p, val) (*(size_t *)(p) = (val))							// p에 val 저장
#define GET_SIZE(p) (GET(p) & ~0x7)										// 사이즈 (~0x7: ...11111000, '&' 연산으로 뒤에 세자리 없어짐)
#define GET_ALLOC(p) (GET(p) & 0x1)										// 할당 상태
#define HDRP(bp) ((char *)(bp) - WSIZE)									// Header 포인터
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)			// Footer 포인터
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 다음 블록의 포인터
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 이전 블록의 포인터
#define NEXT_BLKP_S(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
// Explicit
#define GET_NEXT(bp) (*(void **)((char *)(bp) + WSIZE))
#define GET_PREV(bp) (*(void **)(bp))

// Segregated list
#define SEGSIZE 32
#define GET_ROOT(class) (*(void **)((char *)(heap_listp + (2 * WSIZE)) + (class * WSIZE)))

// coalesce buffer
#define IS_BUFFER(p) ((GET(HDRP(p)) >> 1) & 0x1) // 1 -> in buffer, 0 -> out buffer
#define IS_BIN(p) ((GET(HDRP(p)) >> 2) & 0x1)
#define IS_BIN_N_BUF(p) (GET(HDRP(p)) & 0x6)
#define IS_BUF_N_ALOC(p) (GET(HDRP(p)) & 0x3)
#define GET_NEXT_S(bp) (*(void **)(bp))
static char *heap_listp = NULL;
static void *heap_tail = NULL;

static void *masu_find_fit(size_t size);
static void *masu_place(void *bp, size_t size);
static void *masu_coalesce(void *bp);
static void *masu_extend_chunk(size_t words);

// Explicit
static void masu_remove_free_block(void *bp);
static void masu_add_free_block(void *bp);

// Segregated list
int masu_get_class(size_t size);
static size_t available_size(void *ptr);
static size_t available_alloc(void *ptr);
static void *default_realloc(void *ptr, size_t size);

// coalesce buffer
static void masu_add_free_delay(void *bp);
static void masu_remove_free_delay(void *bp);
static void masu_delay_class_coalesce();

// small bin
static void *mm_small_malloc(size_t asize);
static void mm_small_free(void *bp);
static void *bin_place(void *bp, size_t asize);
static void *create_small_bin(int class);
static void *n_place(void *bp, size_t newsize, size_t asize);

//static size_t large_block_align(size_t asize);
//static int max_cnt = 0;
// static size_t create_small_bin_cnt = 0;
//  pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
/*
static long malloc_time_total = 0;
static int malloc_count = 0;
static long free_time_total = 0;
static int free_count = 0;
static long init_time_total = 0;
static int init_count = 0;
*/
// static int free_count = 0;

// void print_average_times(void) {
// if (malloc_count > 0) {
// fprintf(stderr, "Average malloc time: %lf microseconds\n", (double)malloc_time_total / malloc_count);
// fprintf(stderr, "total malloc time: %lf microseconds\n", (double)malloc_time_total);
// fprintf(stderr, "total malloc count: %d \n", malloc_count);
//}
// if (free_count > 0) {
// printf("Average free time: %lf microseconds\n", (double)free_time_total / free_count);
// printf("total free time: %lf microseconds\n", (double)free_time_total);
// fprintf(stderr, "total free count: %d \n", free_count);
//}
// if (init_count > 0) {
// printf("Average init time: %lf microseconds\n", (double)init_time_total / init_count);
// printf("total init time: %lf microseconds\n", (double)init_time_total);
//}

// for (int i = 0; i < SEGSIZE - 1; i++) {
// fprintf(stderr, "class[%d] count : %d \n", i, class[i]);
//}
//}

/*
static size_t large_block_align(size_t asize) {
	size_t pool_size = (asize + (CHUNKSIZE - 1)) & ~(CHUNKSIZE -1);

	if((asize <= pool_size) && (asize > (pool_size - (pool_size>>3)))) {
		asize = pool_size;
		return asize;
	}

	return NULL;
}
*/

char *masu_strdup(const char *s)
{
	size_t len = strlen(s);

	char *dup = (char *)masu_malloc((len + 1));
	if (dup == NULL)
	{
		return NULL;
	}

	strcpy(dup, s);
	return dup;
}


static void *masu_extend_chunk(size_t words)
{
	char *bp;
	size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

	if ((bp = masu_sbrk(size)) == -1)
		return NULL;

	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

	heap_tail += size;
	return masu_coalesce(bp);
}

static void *mm_small_malloc(size_t size)
{
	size_t asize = ALIGN(size + WSIZE);
	if (asize < 16)
		asize = 16;

	void *bp;
	void *ptr;
	int class = (asize >> 3);
	// fprintf(stderr, "small size : %zu, class : %d\n", asize, class);
	//  fprintf(stderr, "size %d, asize : %d, find class = %d\n", size, asize, class);
	//   printf("asize = %d\n", asize);
	// fprintf(stderr, "get root : %p \n", GET_ROOT(class));
	if ((bp = GET_ROOT(class)) != NULL)
	{
		// fprintf(stderr,"getsize(bp) = %zu\n", GET_SIZE(HDRP(bp)));
		//  fprintf(stderr, "find class = %d\n", class);
		if (GET_SIZE(HDRP(bp)) > asize)
		{
			ptr = bin_place(bp, asize);
			// fprintf(stderr, "return ptr : %p \n", ptr);
			return ptr;
		}

		PUT(HDRP(bp), PACK(asize, 5));
		GET_ROOT(class) = GET_NEXT_S(bp);
		// fprintf(stderr, "GET_ROOT(%d) : %p & GET_NEXT(bp) : %p & bp : %p & getsize(bp) : %zu\n", class, GET_ROOT(class), GET_NEXT_S(bp), bp, GET_SIZE(HDRP(bp)));
		return bp;
	}
	else
	{
		bp = create_small_bin(class);
		return bin_place(bp, asize);
	}

	return NULL;
}

static void *create_small_bin(int class)
{
	// create_small_bin_cnt++;

	// fprintf(stderr, "create small bin called with class %d \n", class);
	void *bp;
	size_t extend_size = CHUNKSIZE;

	if ((bp = masu_sbrk(extend_size)) == NULL)
		return NULL;

	heap_tail += extend_size;
	PUT(HDRP(bp), PACK(extend_size, 1));
	PUT(FTRP(bp), PACK(extend_size, 1));

	PUT(HDRP(NEXT_BLKP_S(bp)), PACK(0, 1));

	bp += WSIZE;
	PUT(HDRP(bp), PACK(extend_size - DSIZE, 4));

	return bp;
}

static void mm_small_free(void *bp)
{
	// Get the size of the block
	size_t size = GET_SIZE(HDRP(bp));
	// Determine the class based on the size
	int class = (size >> 3);

	// Debugging statements
	// fprintf(stderr, "Freeing block: %p\n", bp);
	// fprintf(stderr, "Block size: %zu\n", size);
	// fprintf(stderr, "Class: %d\n", class);

	// Mark the block as free
	PUT(HDRP(bp), PACK(size, 4));

	// Debugging statements
	// fprintf(stderr, "Updated header for block %p to size %zu and free\n", bp, size);

	// Update the free list pointers
	GET_NEXT_S(bp) = GET_ROOT(class);

	// Debugging statements
	// fprintf(stderr, "Set next pointer of block %p to %p\n", bp, GET_NEXT_S(bp));

	// Insert the block into the appropriate class
	GET_ROOT(class) = bp;

	// Debugging statements
	// fprintf(stderr, "Inserted block %p into free list class %d\n", bp, class);
	// fprintf(stderr, "GETROOT : %p, GET_NEXT(bp) : %p\n", GET_ROOT(class), GET_NEXT_S(bp));
}

static void *bin_place(void *bp, size_t asize)
{
	size_t csize = GET_SIZE(HDRP(bp));
	int class = (asize >> 3);

	// fprintf(stderr, "remain region size : %zu \n", csize-asize);
	if ((csize - asize) < asize)
	{
		PUT(HDRP(bp), PACK(asize, 5));
		GET_ROOT(class) = NULL;

		return bp;
	}
	else
	{
		PUT(HDRP(bp), PACK(asize, 5));

		/* split the block */
		PUT(HDRP(NEXT_BLKP_S(bp)), PACK(csize - asize, 4));
		GET_ROOT(class) = NEXT_BLKP_S(bp);
		return bp;
	}
}

void alloc_init(void)
{
	// fprintf(stderr, "Initializing allocator\n");
	mem_init();
	// //struct timeval start, end;
	// //gettimeofday(&start, NULL);

	if ((heap_listp = masu_sbrk((SEGSIZE + 4) * WSIZE)) == (void *)-1)
	{
		// pthread_mutex_unlock(&lock);
		return -1;
	}
	PUT(heap_listp, 0);
	PUT(heap_listp + (1 * WSIZE), PACK((SEGSIZE + 2) * WSIZE, 1)); // prologue header
	for (int i = 0; i < SEGSIZE; i++)
		PUT(heap_listp + ((2 + i) * WSIZE), NULL);
	PUT(heap_listp + ((2 + SEGSIZE) * WSIZE), PACK((SEGSIZE + 2) * WSIZE, 1)); // prologue footer
	PUT(heap_listp + ((3 + SEGSIZE) * WSIZE), PACK(0, 1));					   // epilogue header
	heap_tail = heap_listp + (4 + SEGSIZE) * WSIZE;

	void* ptr = masu_sbrk(256);
	PUT(HDRP(ptr), PACK(256, 0));
	PUT(FTRP(ptr), PACK(256, 0));
	PUT(HDRP(NEXT_BLKP(ptr)), PACK(0,1));
	heap_tail = heap_tail + 256;
	masu_add_free_block(ptr);


	// //gettimeofday(&end, NULL);
	// init_time_total += ((end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec) - start.tv_usec;
	// init_count++;
	// fprintf(stderr, "Allocator initialized successfully\n");
}

void *masu_malloc(size_t size)
{
	// pthread_mutex_lock(&lock);
	//  fprintf(stderr, "masu_malloc: size = %zu\n", size);
	//   //struct timeval start, end;
	//   //gettimeofday(&start, NULL);
	//   fprintf(stderr, "total malloc count: %d \n", malloc_count);

	// fprintf(stderr, "maslloc size : %d\n", size);
	// fprintf(stderr, "masu_malloc called size : %zu\n", size);
	if (heap_listp == NULL)
	{
		alloc_init();
	}

	size_t asize;
	size_t extendsize;
	// size_t poolsize;
	char *bp;
	char *ptr;

	if (size <= 120)
	{
		// pthread_mutex_unlock(&lock);
		//  fprintf(stderr, "start mm_small_malloc asize : %zu\n", asize);
		ptr = mm_small_malloc(size);
		// fprintf(stderr, "return ptr : %p \n", ptr);

		// fprintf(stderr, "1 ptr : %p\n", ptr);
		return ptr;
	}


	// print_average_times();
	masu_delay_class_coalesce();
	// fprintf(stderr, "coalesce buffer completed\n");
	
	if (size > 0xFFFFFFFF)
	{
		// pthread_mutex_unlock(&lock);
		return NULL;
	}


	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = ALIGN(2 * WSIZE + size);

	/*
	if((asize % CHUNKSIZE) == 0) {
		ptr = mmap(NULL, asize, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
		if (ptr == MAP_FAILED) return NULL;
		bp = ptr + WSIZE;
		PUT(HDRP(bp), PACK(asize, 3));
		PUT(FTRP(bp), PACK(asize, 3));
		return bp;
	}
	*/

	/*
	large_block mmap

	if((poolsize = large_block_align(asize)) != NULL) {
		ptr = mmap(NULL, poolsize, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
		if (ptr == MAP_FAILED) return NULL;

		bp = ptr + WSIZE;
		PUT(HDRP(bp), PACK(poolsize, 3));
		PUT(FTRP(bp), PACK(poolsize, 3));
		return bp;
	}
	*/

	if ((bp = masu_find_fit(asize)) != NULL)
	{
		// fprintf(stderr, "find fit asize %d, class %d, bp %p\n", asize, masu_get_class(asize), bp);
		// fprintf(stderr, "masu_place start\n");
		ptr = masu_place(bp, asize);
		// fprintf(stderr, "masu_place complete\n");
		//  //gettimeofday(&end, NULL);
		//  malloc_time_total += ((end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec) - start.tv_usec;
		//  malloc_count++;
		// pthread_mutex_unlock(&lock);
		// fprintf(stderr, "2 ptr : %p\n", ptr);
		return ptr;
	}
	else
	{
		size_t new_size = asize;
		if (!GET_ALLOC(HDRP(PREV_BLKP(heap_tail))))
		{
			size_t end_size = GET_SIZE(HDRP(PREV_BLKP(heap_tail)));
			if (asize >= end_size)
			{
				// printf("asize: %d, end_size: %d, prev_alloc: %d, is_buffer: %d \n", asize, end_size, GET_ALLOC(HDRP(PREV_BLKP(heap_listp))), IS_BUFFER(PREV_BLKP(heap_tail)));
				bp = PREV_BLKP(heap_tail);
				masu_remove_free_block(bp);
				new_size = asize - end_size;
				// printf("new_size = %d \n", new_size);
				if (masu_sbrk(new_size) == NULL)
				{
					// pthread_mutex_unlock(&lock);
					return NULL;
				}

				PUT(HDRP(bp), PACK(asize, 1));
				PUT(FTRP(bp), PACK(asize, 1));
				PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
				heap_tail += new_size;
				// pthread_mutex_unlock(&lock);
				return bp;
			}
		}
		else
		{
			if ((bp = masu_sbrk(asize)) == NULL)
			{
				// pthread_mutex_unlock(&lock);
				return NULL;
			}
			PUT(HDRP(bp), PACK(asize, 1));
			PUT(FTRP(bp), PACK(asize, 1));
			PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
			heap_tail += asize;
			// //gettimeofday(&end, NULL);
			// malloc_time_total += ((end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec) - start.tv_usec;
			// malloc_count++;
			// pthread_mutex_unlock(&lock);
			return bp;
		}
	}
}

static void *n_place(void *bp, size_t newsize, size_t asize)
{

	size_t csize = newsize;

	if ((csize - asize) <= 112)
	{
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));

		return bp;
	}
	else
	{
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));


		/* split the block */
		PUT(HDRP(NEXT_BLKP(bp)), PACK(csize - asize, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(csize - asize, 0));
		masu_add_free_block(NEXT_BLKP(bp));
		return bp;
	}
}

void masu_free(void *bp)
{
	// pthread_mutex_lock(&lock);
	// fprintf(stderr, "masu_free: bp = %p\n", bp);
	//   //struct timeval start, end;
	//   //gettimeofday(&start, NULL);
	// fprintf(stderr, "start free : %d \n", free_count);
	// free_count ++;
	// fprintf(stderr, "start free bp : %p, IS_BIN bp : %d, GET_ALLOC bp : %d\n", bp, IS_BIN(bp), GET_ALLOC(HDRP(bp)));

	if (bp == NULL)
	{
		// fprintf(stderr, "Debug: bp is NULL at initial check.\n");
		//  pthread_mutex_unlock(&lock);
		return NULL;
	}


	
	size_t size = GET_SIZE(HDRP(bp));
	// fprintf(stderr, "Debug: Size of block at %p is %zu.\n", (void *)bp, size);

	
	//large block mmap

	/*
	if(IS_BUFFER(bp)) {
		void* ptr = bp - WSIZE;
		munmap(ptr, size);
		return;
	}
	*/
	

	if ((size <= 128) && IS_BIN(bp))
	{
		// fprintf(stderr, "Debug: Small block operation for block at %p with size %zu.\n", (void *)bp, size);
		// fprintf(stderr, "small free call \n");
		mm_small_free(bp);
		// pthread_mutex_unlock(&lock);
		return;
	}

	/*
	IS_BUF_N_ALLOC 	= 1 => allocated block
					= 2 => in (near)buffered block
	*/
	int prev_buf_n_alloc = 1;
	int next_buf_n_alloc = 1;
	if (PREV_BLKP(bp) != NULL)
	{
		prev_buf_n_alloc = IS_BUF_N_ALOC(PREV_BLKP(bp));
		// fprintf(stderr, "com. prev.\n");
	}

	if (NEXT_BLKP(bp) != NULL && GET_SIZE(NEXT_BLKP(bp)))
	{
		next_buf_n_alloc = IS_BUF_N_ALOC(NEXT_BLKP(bp));
		// fprintf(stderr, "com. next.\n");
	}

	if ((prev_buf_n_alloc == 2) || (next_buf_n_alloc == 2))
	{
		// fprintf(stderr, "set six(6) prev : %d, next : %d \n", prev_buf_n_alloc, next_buf_n_alloc);
		PUT(HDRP(bp), PACK(size, 6));
		PUT(FTRP(bp), PACK(size, 6));
		return;
		// fprintf(stderr, "com. set six\n");
	}
	else if ((prev_buf_n_alloc == 0) || (next_buf_n_alloc == 0))
	{
		// fprintf(stderr, "set two(2) prev : %d, next : %d \n", prev_buf_n_alloc, next_buf_n_alloc);
		PUT(HDRP(bp), PACK(size, 2));
		PUT(FTRP(bp), PACK(size, 2));
		masu_add_free_delay(bp);
		// printf("get_root after add_coal %p\n", GET_ROOT(SEGSIZE-1));
		// int class = SEGSIZE -1;
		// printf("get_root class after add_coal %p\n", GET_ROOT(class));
	}
	else if ((prev_buf_n_alloc == 1) && (next_buf_n_alloc == 1))
	{
		// fprintf(stderr, "set zero(0) prev : %d, next : %d \n", prev_buf_n_alloc, next_buf_n_alloc);
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
		masu_add_free_block(bp);
	}
	// pthread_mutex_unlock(&lock);
	//  //gettimeofday(&end, NULL);
	//  free_time_total += ((end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec) - start.tv_usec;
	//  free_count++;
}

void *masu_calloc(size_t nmemb, size_t size)
{
	size_t bytes = nmemb * size;
	void *new_ptr = masu_malloc(bytes);
	// fprintf(stderr, "calloc ptr %p size %d\n", new_ptr, GET_SIZE(HDRP(new_ptr)));
	if (bytes <= 120)
		bytes = GET_SIZE(HDRP(new_ptr)) - WSIZE;
	else
		bytes = GET_SIZE(HDRP(new_ptr)) - DSIZE;
	if (new_ptr == NULL)
	{
		return NULL;
	}

	memset(new_ptr, 0, bytes);
	return new_ptr;
}

void *masu_realloc(void *ptr, size_t size)
{
	// pthread_mutex_lock(&lock);
	//  fprintf(stderr,"masu realloc called\n");
	size_t asize;
	void *newptr;
	// fprintf("realloc called size : %zu, ptr : %p\n", size, ptr);

	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = ALIGN(2 * WSIZE + size);

	if (ptr == NULL)
	{
		// pthread_mutex_unlock(&lock);
		return masu_malloc(size);
	}

	size_t oldsize = GET_SIZE(HDRP(ptr));
	// fprintf(stderr, "realloc called newsize : %zu, oldsize : %zu\n", asize, oldsize);
	//  fprintf(stderr, "realloc called old block metadata %d\n", IS_BIN(ptr)*4+IS_BUFFER(ptr)*2+GET_ALLOC(HDRP(ptr)));

	if (size <= 0)
	{
		// pthread_mutex_unlock(&lock);
		masu_free(ptr);
		return 0;
	}

	if (asize <= oldsize)
	{
		// fprintf(stderr,"asize <= oldsize \n");
		// pthread_mutex_unlock(&lock);
		return ptr;
	}
	masu_delay_class_coalesce();
	// fprintf("realloc called newsize : %zu, oldsize : %zu\n", asize, oldsize);

	if ((GET_SIZE(HDRP(ptr)) <= 128) && IS_BIN(ptr))
	{
		if (size > 120)
		{
			// pthread_mutex_unlock(&lock);
			newptr = masu_malloc(size);
			// pthread_mutex_lock(&lock);
			size_t oldsize = GET_SIZE(HDRP(ptr));
			if (newptr == NULL)
			{
				// pthread_mutex_unlock(&lock);
				return NULL;
			}

			memcpy(newptr, ptr, (GET_SIZE(HDRP(ptr)) - WSIZE));
			// pthread_mutex_unlock(&lock);
			masu_free(ptr);

			return newptr;
		}
		else
		{
			// fprintf(stderr,"mm_small_malloc start\n");
			// pthread_mutex_unlock(&lock);
			newptr = masu_malloc(size);
			// pthread_mutex_lock(&lock);
			//  fprintf(stderr,"mm_small_malloc complete ptr %p, size %d\n", newptr, GET_SIZE(HDRP(newptr)));
			size_t oldsize = GET_SIZE(HDRP(ptr));
			if (newptr == NULL)
			{
				// pthread_mutex_unlock(&lock);
				return NULL;
			}

			memcpy(newptr, ptr, (GET_SIZE(HDRP(ptr)) - WSIZE));
			// pthread_mutex_unlock(&lock);
			masu_free(ptr);
			return newptr;
		}
	}

	int prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
	size_t prev_size = GET_SIZE(FTRP(PREV_BLKP(ptr)));
	int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
	size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
	if (PREV_BLKP(heap_tail) == ptr)
	{
		next_alloc = 1;
	}

	if (!next_alloc)
	{
		if ((oldsize + next_size) >= asize)
		{
			masu_remove_free_block(NEXT_BLKP(ptr));
			PUT(HDRP(ptr), PACK((oldsize + next_size), 1));
			PUT(FTRP(ptr), PACK((oldsize + next_size), 1));
			// pthread_mutex_unlock(&lock);
			return ptr;
		}
	}
	else if ((!next_alloc) && (!prev_alloc) && ((oldsize + prev_size + next_size) >= asize))
	{
		void *prev_block = PREV_BLKP(ptr);
		if (prev_size >= oldsize)
		{
			// fprintf(stderr, "here 1\n");
			masu_remove_free_block(PREV_BLKP(ptr));
			masu_remove_free_block(NEXT_BLKP(ptr));
			if ((prev_size + oldsize + next_size - asize) <= 128)
			{
				memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
				PUT(HDRP(prev_block), PACK((prev_size + oldsize + next_size - asize), 1));
				PUT(FTRP(prev_block), PACK((prev_size + oldsize + next_size - asize), 1));
				return prev_block;
			}
			else
			{
				memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
				PUT(HDRP(prev_block), PACK(asize, 1));
				PUT(FTRP(prev_block), PACK(asize, 1));

				PUT(HDRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize + next_size - asize), 0));
				PUT(FTRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize + next_size - asize), 0));

				// fprintf(stderr, "here 1\n");
				masu_add_free_block(NEXT_BLKP(prev_block));
				// pthread_mutex_unlock(&lock);
				return prev_block;
			}
		}
		else if (prev_size < oldsize)
		{
			// fprintf(stderr, "here 2\n");
			masu_remove_free_block(PREV_BLKP(ptr));
			masu_remove_free_block(NEXT_BLKP(ptr));
			int total_movesize = GET_SIZE(HDRP(ptr)) - DSIZE;
			int sep_movesize = GET_SIZE(HDRP(prev_block));
			int n = total_movesize / sep_movesize;

			for (int i = 0; i < n; i++)
			{
				memcpy(prev_block + i * sep_movesize, ptr + i * sep_movesize, sep_movesize);
			}
			memcpy(prev_block + n * sep_movesize, ptr + n * sep_movesize, total_movesize - (sep_movesize * n));

			if (((prev_size + oldsize + next_size) - asize) <= 128)
			{
				PUT(HDRP(prev_block), PACK((prev_size + oldsize + next_size), 1));
				PUT(FTRP(prev_block), PACK((prev_size + oldsize + next_size), 1));
				// pthread_mutex_unlock(&lock);
				return prev_block;
			}
			else
			{
				PUT(HDRP(prev_block), PACK(asize, 1));
				PUT(FTRP(prev_block), PACK(asize, 1));

				PUT(HDRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize + next_size - asize), 0));
				PUT(FTRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize + next_size - asize), 0));
				// fprintf(stderr, "here 2\n");
				masu_add_free_block(NEXT_BLKP(prev_block));
				// pthread_mutex_unlock(&lock);
				return prev_block;
			}
		}
	}
	else if ((next_alloc) && (!prev_alloc) && ((oldsize + prev_size) >= asize))
	{
		void *prev_block = PREV_BLKP(ptr);
		if (prev_size >= oldsize)
		{
			// fprintf(stderr, "here 3\n");
			masu_remove_free_block(PREV_BLKP(ptr));
			if ((prev_size + oldsize - asize) <= 128)
			{
				memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
				PUT(HDRP(prev_block), PACK((prev_size + oldsize), 1));
				PUT(FTRP(prev_block), PACK((prev_size + oldsize), 1));
				return prev_block;
			}
			else
			{
				memcpy(prev_block, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
				PUT(HDRP(prev_block), PACK(asize, 1));
				PUT(FTRP(prev_block), PACK(asize, 1));

				PUT(HDRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize - asize), 0));
				PUT(FTRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize - asize), 0));

				// fprintf(stderr, "here 3\n");
				masu_add_free_block(NEXT_BLKP(prev_block));
				// pthread_mutex_unlock(&lock);
				return prev_block;
			}
		}
		else if (prev_size < oldsize)
		{
			// fprintf(stderr, "here 4\n");
			masu_remove_free_block(PREV_BLKP(ptr));
			int total_movesize = GET_SIZE(HDRP(ptr)) - DSIZE;
			int sep_movesize = GET_SIZE(HDRP(prev_block));
			int n = total_movesize / sep_movesize;

			for (int i = 0; i < n; i++)
			{
				memcpy(prev_block + i * sep_movesize, ptr + i * sep_movesize, sep_movesize);
			}
			memcpy(prev_block + n * sep_movesize, ptr + n * sep_movesize, total_movesize - (sep_movesize * n));

			if (((prev_size + oldsize) - asize) <= 128)
			{
				PUT(HDRP(prev_block), PACK((prev_size + oldsize), 1));
				PUT(FTRP(prev_block), PACK((prev_size + oldsize), 1));
				// pthread_mutex_unlock(&lock);
				return prev_block;
			}
			else
			{
				PUT(HDRP(prev_block), PACK(asize, 1));
				PUT(FTRP(prev_block), PACK(asize, 1));

				PUT(HDRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize - asize), 0));
				PUT(FTRP(NEXT_BLKP(prev_block)), PACK((prev_size + oldsize - asize), 0));
				// fprintf(stderr, "here 4\n");
				masu_add_free_block(NEXT_BLKP(prev_block));
				// pthread_mutex_unlock(&lock);
				return prev_block;
			}
		}
	}
	// pthread_mutex_unlock(&lock);
	newptr = default_realloc(ptr, size);
	return newptr;
}

static void *masu_coalesce(void *bp)
{
	// pthread_mutex_lock(&lock);

	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && !next_alloc)
	{
		if (IS_BUFFER(PREV_BLKP(bp)) == 1)
			next_alloc = 1;
	}
	else if (!prev_alloc && next_alloc)
	{
		if (IS_BUFFER(NEXT_BLKP(bp)) == 1)
			prev_alloc = 1;
	}
	else if (!prev_alloc && !next_alloc)
	{
		if (IS_BUFFER(PREV_BLKP(bp)) && IS_BUFFER(NEXT_BLKP(bp)))
		{
			prev_alloc = 1;
			next_alloc = 1;
		}
		else if (!IS_BUFFER(PREV_BLKP(bp)) && IS_BUFFER(NEXT_BLKP(bp)))
		{
			next_alloc = 1;
		}
		else if (IS_BUFFER(PREV_BLKP(bp)) && !IS_BUFFER(NEXT_BLKP(bp)))
		{
			prev_alloc = 1;
		}
	}

	if (prev_alloc && next_alloc)
	{
		masu_add_free_block(bp);
		// pthread_mutex_unlock(&lock);
		return bp;
	}
	else if (prev_alloc && !next_alloc)
	{
		masu_remove_free_block(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	else if (!prev_alloc && next_alloc)
	{
		masu_remove_free_block(PREV_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	else
	{
		masu_remove_free_block(PREV_BLKP(bp));
		masu_remove_free_block(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}

	masu_add_free_block(bp);
	// pthread_mutex_unlock(&lock);
	return bp;
}

static void masu_delay_class_coalesce()
{
	// struct timeval start, end;
	// gettimeofday(&start, NULL);
	int class = 1;
	// fprintf(stderr, "GET_ROOT(1) : %p\n", GET_ROOT(1));
	char *ptr = GET_ROOT(class);

	// fprintf(stderr, "Starting masu_delay_class_coalesce()\n");
	// fprintf(stderr, "Initial class: %d\n", class);

	if (ptr == NULL)
	{
		// fprintf(stderr, "GET_ROOT(%d) returned NULL\n", class);
		return;
	}
	if (ptr == 0)
	{
		// fprintf(stderr, "GET_ROOT(%d) returned 0\n", class);
		return;
	}

	while (ptr != NULL)
	{
		// fprintf(stderr, "Processing block at %p\n", ptr);

		char *start_ptr = ptr;
		char *next_ptr = ptr;
		size_t totalsize = 0;
		// Coalescing backwards
		while (start_ptr >= (char *)heap_listp + 2 * WSIZE)
		{

			totalsize += GET_SIZE(HDRP(start_ptr));
			// fprintf(stderr, "Coalescing backward, new totalsize: %zu\n", totalsize);
			if (IS_BUFFER(start_ptr) && !IS_BIN(start_ptr))
			{
				// fprintf(stderr, "Removing free delay block at %p\n", start_ptr);
				masu_remove_free_delay(start_ptr);
			}
			else if (!IS_BUFFER(start_ptr) && !IS_BIN(start_ptr))
			{
				// fprintf(stderr, "Removing free block at %p\n", start_ptr);
				// fprintf(stderr, " |bin %d|buf %d|alloc %d| \n", IS_BIN(start_ptr), IS_BUFFER(start_ptr), GET_ALLOC(HDRP(start_ptr)));
				masu_remove_free_block(start_ptr);
			}

			if (GET_ALLOC(HDRP(PREV_BLKP(start_ptr))))
			{
				// fprintf(stderr, "Previous block is allocated, stopping backward coalescing\n");
				break;
			}
			else
			{
				if (start_ptr == PREV_BLKP(start_ptr))
					break;

				start_ptr = PREV_BLKP(start_ptr);
				// fprintf(stderr, "Moving to previous block at %p\n", start_ptr);
			}
		}

		// Coalescing forwards
		while (next_ptr <= ((char *)heap_tail - WSIZE))
		{
			next_ptr = NEXT_BLKP(next_ptr);
			// fprintf(stderr, "Moving to next block at %p\n", next_ptr);

			if (!GET_ALLOC(HDRP(next_ptr)))
			{
				totalsize += GET_SIZE(HDRP(next_ptr));
				// fprintf(stderr, "Coalescing forward, new totalsize: %zu\n", totalsize);

				if (IS_BUFFER(next_ptr) && !IS_BIN(next_ptr))
				{
					// fprintf(stderr, "Removing free delay block at %p\n", start_ptr);
					masu_remove_free_delay(next_ptr);
				}
				else if (!IS_BUFFER(next_ptr) && !IS_BIN(next_ptr))
				{
					// fprintf(stderr, "Removing free block at %p\n", start_ptr);
					masu_remove_free_block(next_ptr);
				}
			}
			else
			{
				// fprintf(stderr, "Next block is allocated, stopping forward coalescing\n");
				break;
			}
		}

		PUT(HDRP(start_ptr), PACK(totalsize, 0));
		PUT(FTRP(start_ptr), PACK(totalsize, 0));
		// fprintf(stderr, "New free block: start=%p, size=%zu\n", start_ptr, totalsize);
		// fprintf(stderr, "start_ptr : %p \n", start_ptr);
		masu_add_free_block(start_ptr);

		ptr = GET_ROOT(class);
		// fprintf(stderr, "Next root pointer: %p\n", ptr);
	}
	// gettimeofday(&end, NULL);
	// long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
	// delay_class_coalesce_time += time_taken;
	// delay_class_coalesce_count++;

	// fprintf(stderr, "Ending masu_delay_class_coalesce()\n");
}

static void *masu_find_fit(size_t asize)
{
	// struct timeval start, end;
	// gettimeofday(&start, NULL);
	int class = masu_get_class(asize);
	void *bp = GET_ROOT(class);

	while (class < SEGSIZE)
	{
		bp = GET_ROOT(class);

		while (bp != NULL)
		{
			if ((asize <= GET_SIZE(HDRP(bp))))
			{
				// gettimeofday(&end, NULL);
				// long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
				// find_fit_time += time_taken;
				// find_fit_count++;
				return bp;
			}
			bp = GET_NEXT(bp);
		}
		class += 1;
	}
	// gettimeofday(&end, NULL);
	// long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
	// find_fit_time += time_taken;
	// find_fit_count++;
	return NULL;
}

static void *find_fit_one_class(int class)
{
	void *bp = GET_ROOT(class);

	bp = GET_ROOT(class);

	while (bp != NULL)
	{
		return bp;
	}

	return NULL;
}

static void *masu_place(void *bp, size_t asize)
{
	// struct timeval start, end;
	// gettimeofday(&start, NULL);
	//  fprintf(stderr, "start remove_free_block \n");
	masu_remove_free_block(bp);
	// fprintf(stderr, "complete remove_free_block\n");
	size_t csize = GET_SIZE(HDRP(bp));
	// fprintf(stderr, "csize %d asize %d\n", csize, asize);
	if ((csize - asize) <= 128)
	{
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
		// gettimeofday(&end, NULL);
		// long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
		// place_time += time_taken;
		// place_count++;
		return bp;
	}
	else
	{
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));

		/* split the block */
		PUT(HDRP(NEXT_BLKP(bp)), PACK(csize - asize, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(csize - asize, 0));
		masu_add_free_block(NEXT_BLKP(bp));
		// gettimeofday(&end, NULL);
		// long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
		// place_time += time_taken;
		// place_count++;
		return bp;
	}
}

static void masu_remove_free_block(void *bp)
{
	// fprintf(stderr, "masu_remove_free_block: Starting to remove block %p\n", bp);
	int size = (int)GET_SIZE(HDRP(bp));
	int class = masu_get_class(size);
	// printf("size %zu, class %d GET_SIZE(HDRP(bp)) %d\n", GET_SIZE(HDRP(bp)),class, masu_get_class(GET_SIZE(HDRP(bp))));
	//  fprintf(stderr, "masu_remove_free_block: Block size: %zu, class: %d, GET_ROOT: %p\n", GET_SIZE(HDRP(bp)), class, GET_ROOT(class));

	if (bp == GET_ROOT(class))
	{
		// fprintf(stderr, "masu_remove_free_block: Block %p is the root of class %d\n", bp, class);
		GET_ROOT(class) = GET_NEXT(GET_ROOT(class));
		// fprintf(stderr, "masu_remove_free_block: Removed root block %p, new root is %p\n", bp, GET_ROOT(class));
		return;
	}

	// fprintf(stderr, "masu_remove_free_block: Block %p is not the root of class %d\n", bp, class);
	GET_NEXT(GET_PREV(bp)) = GET_NEXT(bp);
	// fprintf(stderr, "masu_remove_free_block: Setting next of %p to %p\n", GET_PREV(bp), GET_NEXT(bp));

	if (GET_NEXT(bp) != NULL)
	{
		// fprintf(stderr, "masu_remove_free_block: Setting prev of %p to %p\n", GET_NEXT(bp), GET_PREV(bp));
		GET_PREV(GET_NEXT(bp)) = GET_PREV(bp);
	}
	// fprintf(stderr, "masu_remove_free_block: Block %p removed from class %d\n", bp, class);
}

static void masu_add_free_block(void *bp)
{
	int class = masu_get_class(GET_SIZE(HDRP(bp)));
	//fprintf(stderr,"class : %d, size : %zu \n", class, GET_SIZE(HDRP(bp)));

	// fprintf(stderr, "masu_add_free_block: Adding block %p to class %d and size  %zu\n", bp, class, GET_SIZE(HDRP(bp)));

	GET_NEXT(bp) = GET_ROOT(class);
	if (GET_ROOT(class) != NULL)
	{
		// fprintf(stderr, "masu_add_free_block: Setting prev of root %p to %p\n", GET_ROOT(class), bp);
		GET_PREV(GET_ROOT(class)) = bp;
	}
	GET_ROOT(class) = bp;

	// fprintf(stderr, "masu_add_free_block: Root of class %d is now %p\n", class, GET_ROOT(class));
}

static void masu_remove_free_delay(void *bp)
{
	int class = 1;

	if (bp == GET_ROOT(class))
	{
		GET_ROOT(class) = GET_NEXT(GET_ROOT(class));
		return;
	}

	GET_NEXT(GET_PREV(bp)) = GET_NEXT(bp);

	if (GET_NEXT(bp) != NULL)
		GET_PREV(GET_NEXT(bp)) = GET_PREV(bp);
}

static void masu_add_free_delay(void *bp)
{
	int class = 1;
	GET_NEXT(bp) = GET_ROOT(class);
	if (GET_ROOT(class) != NULL)
		GET_PREV(GET_ROOT(class)) = bp;
	GET_ROOT(class) = bp;
}

int masu_get_class(size_t size)
{
	// struct timeval start, end;
	// gettimeofday(&start, NULL);
	int ind = 7;
	if (size > 128)
	{
		while ((1 << ind) < size)
		{
			ind++;
		}
		ind = ind + 9;
	}

	if (ind > SEGSIZE - 1)
	{
		// gettimeofday(&end, NULL);
		// long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
		// get_class_time += time_taken;
		// get_class_count++;
		return SEGSIZE - 1;
	}
	// gettimeofday(&end, NULL);
	// long time_taken = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
	// get_class_time += time_taken;
	// get_class_count++;
	return ind;
}


static size_t available_alloc(void *ptr)
{
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
	size_t size = GET_SIZE(HDRP(ptr));

	if (prev_alloc && next_alloc)
		return 0;
	else if (!prev_alloc && next_alloc)
		return 1;
	else if (prev_alloc && !next_alloc)
	{
		if (PREV_BLKP(heap_tail) == ptr)
			return 0;
		else
			return 2;
	}
	else if (!prev_alloc && !next_alloc)
	{
		if (PREV_BLKP(heap_tail) == ptr)
			return 1;
		else
			return 3;
	}
	else
		return 0;
}

static size_t available_size(void *ptr)
{
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

static void *default_realloc(void *ptr, size_t size)
{
	void *newptr = masu_malloc(size);
	// pthread_mutex_lock(&lock);
	size_t oldsize = GET_SIZE(HDRP(ptr));
	if (newptr == NULL)
	{
		// pthread_mutex_unlock(&lock);
		return NULL;
	}

	memcpy(newptr, ptr, (GET_SIZE(HDRP(ptr)) - DSIZE));
	// pthread_mutex_unlock(&lock);
	masu_free(ptr);

	return newptr;
}

//void __attribute__((destructor)) cleanup()
//{
//   fprintf(stderr, "Average malloc time: %lf microseconds\n", (double)malloc_time / malloc_count);
//   fprintf(stderr, "Total malloc time: %lf microseconds\n", (double)malloc_time);
//   fprintf(stderr, "Total malloc count: %d\n", malloc_count);

// fprintf(stderr, "Average free time: %lf microseconds\n", (double)free_time / free_count);
// fprintf(stderr, "Total free time: %lf microseconds\n", (double)free_time);
// fprintf(stderr, "Total free count: %d\n", free_count);

// fprintf(stderr, "Average calloc time: %lf microseconds\n", (double)calloc_time / calloc_count);
// fprintf(stderr, "Total calloc time: %lf microseconds\n", (double)calloc_time);
// fprintf(stderr, "Total calloc count: %d\n", calloc_count);

// fprintf(stderr, "Average realloc time: %lf microseconds\n", (double)realloc_time / realloc_count);
// fprintf(stderr, "Total realloc time: %lf microseconds\n", (double)realloc_time);
// fprintf(stderr, "Total realloc count: %d\n", realloc_count);

// fprintf(stderr, "Average masu_delay_class_coalesce time: %lf microseconds\n", (double)delay_class_coalesce_time / delay_class_coalesce_count);
// fprintf(stderr, "Average find_fit time: %lf microseconds\n", (double)find_fit_time / find_fit_count);
// fprintf(stderr, "Average place time: %lf microseconds\n", (double)place_time / place_count);
// fprintf(stderr, "Average get_class time: %lf microseconds\n", (double)get_class_time / get_class_count);

// total_time = (malloc_time + free_time + calloc_time + realloc_time);
// fprintf(stderr, "ALL TOTAL TIME : %lf microseconds\n", (double)total_time);
// fprintf(stderr, "create_small_bin_cnt : %d \n", create_small_bin_cnt);
//fprintf(stderr, "max_cnt : %d \n", max_cnt);
//}
