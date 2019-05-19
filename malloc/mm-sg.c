/*
 ******************************************************************************
 *                               mm-baseline.c                                *
 *           64-bit struct-based implicit free list memory allocator          *
 *                  15-213: Introduction to Computer Systems                  *
 *                                                                            *
 *  ************************************************************************  *
 *  Each block has minimum size of 32 bytes, and formatted as follows :       *
 *                                                                            *
 *                              Allocated Block                               *
 *  ---------------------------------------------------------------------------
 * |        Header      |            Payload            |       Footer         |
 *  ___________________________________________________________________________
 *                                                                            *
 *                                   Free Block                               *
 *  ---------------------------------------------------------------------------
 * |        Header      |    Prev ptr   |   Next ptr    |       Footer         |
 *  ---------------------------------------------------------------------------
 *                                                                            *
 * The explicit free list is implemented using doubly linked list, where      *
 * prev ptr points to the previous free block of the free list, and next ptr  *
 * points to the next free block of the free list. Free list is manipulated   *
 * using by find fit. If best_fit is set to zero, find_fit implements first   *
 * fit and best fit otherwise. Currently it uses first fit, searches from the *
 * last element of the free list, traversing until it reaches the first       *
 * element. Also, free list is implemented on FIFO polocy.                    *
 *                                                                            *
 *  ************************************************************************  *
 */

/* Do not change the following! */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stddef.h>

#include "mm.h"
#include "memlib.h"

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 * If DEBUG is defined, enable printing on dbg_printf and contracts.
 * Debugging macros, with names beginning "dbg_" are allowed.
 * You may not define any other macros having arguments.
 */
 #define DEBUG // uncomment this line to enable debugging

#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disnabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/* Basic constants */
typedef uint64_t word_t;
static const size_t wsize = sizeof(word_t);  // word and header size (bytes)
static const size_t dsize = 2*wsize; // double word size (bytes)
static const size_t MIN_BLOCK_SIZE = 2*dsize; // Minimum block size
static const size_t  chunksize = (1 << 12);
                       // requires (chunksize % 16 == 0)

static const word_t alloc_mask = 0x1;
static const word_t size_mask = ~(word_t)0xF;

#define seg_size 15

typedef struct block
{
    /* Header contains size + allocation flag */
    word_t header;
    /*
     * We don't know how big the payload will be.  Declaring it as an
     * array of size 0 allows computing its starting address using
     * pointer notation.
     */

    char payload[0];

    struct block *prev; // pointer to prev free block
    struct block *next; // pointer to next free block
    /*
     * We can't declare the footer as part of the struct, since its starting
     * position is unknown
     */
} block_t;


/* Global variables */
/* Pointer to first block */
static block_t *heap_start = NULL; // pointer to first block
static block_t *epilogue = NULL; // pointer to epilogue
static block_t *seg_list[seg_size]; // array of segregated free lists


/* Function prototypes */
static block_t *get_prev_freed(block_t *block);
static block_t *get_next_freed(block_t *block);

static size_t get_seg_size(size_t size);

static void add_free(block_t *block, size_t size);

static void delete_free(block_t *block);

bool mm_init(void);
void *malloc(size_t size);
void free(void *bp);
void *realloc(void *ptr, size_t size);
void *calloc(size_t elements, size_t size);

bool check_prologue_and_epilogue(void);
bool check_block_consistency(void);
bool check_freelist(void);
bool mm_checkheap(int lineno);


/* Function prototypes for internal helper routines */
static block_t *extend_heap(size_t size);
static void place(block_t *block, size_t asize);
static block_t *find_fit(size_t asize);
static block_t *coalesce(block_t *block);

static size_t max(size_t x, size_t y);
static size_t round_up(size_t size, size_t n);
static word_t pack(size_t size, bool alloc);

static size_t extract_size(word_t header);
static size_t get_size(block_t *block);
static size_t get_payload_size(block_t *block);

static bool extract_alloc(word_t header);
static bool get_alloc(block_t *block);

static void write_header(block_t *block, size_t size, bool alloc);
static void write_footer(block_t *block, size_t size, bool alloc);

static block_t *payload_to_header(void *bp);
static void *header_to_payload(block_t *block);

static block_t *find_next(block_t *block);
static word_t *find_prev_footer(block_t *block);
static block_t *find_prev(block_t *block);

/*
 * get_prev_freed and get_next_freed returns the pointer to the previous
 * freed block and next freed block of explicit free list
 * in the current freed block, respectively
 */

static block_t *get_prev_freed(block_t *block) {
    return block->prev;
}

static block_t *get_next_freed(block_t *block) {
    return block->next;
}

/*
 * get_seg_index returns the appropriate index to the array of free lists
 * according to its input size
 */
static size_t get_seg_size(size_t size) {
    size_t index = 0;

    while ((index < seg_size - 1) && (size > 1)) {
        size >>= 1;
        index++;
    }

    return index;
}

/*
 * add_free adds the freed block on the correct segregated list.
 */

static void add_free(block_t *freed, size_t size) {

    size_t index = get_seg_size(size); // index into array of free lists
    block_t *next = seg_list[index]; // ptr to next in free list

    if (next == NULL) {
        freed->next = next;
        freed->prev = NULL;
    }

    else {
        freed->next = next;
        freed->prev = NULL;
        next->prev = freed;
    }

    seg_list[index] = freed;
/*
    block_t *prev = NULL;
    // now look for position inside the free list
    while (next != NULL && size > get_size(next)) {
        prev = next;
        next = get_next_freed(next);
    }

    // Case 1. Insert at empty free list
    if ((next == NULL) && (prev == NULL)) {
        freed->next = NULL;
        freed->prev = NULL;
        seg_list[index] = freed;
    }

    // Case 2. Insert at last in non-empty list
    else if ((next == NULL) && (prev != NULL)) {
        freed->next = NULL;
        freed->prev = prev;
        prev->next = freed;
    }

    // Case 3. Insert at front in non-empty list
    else if ((next != NULL) && (prev == NULL)) {
        freed->next = next;
        freed->prev = NULL;
        next->prev = freed;
        seg_list[index] = freed;
    }

    // Case 4. Insert in the middle of non-empty list
    else {
        freed->next = next;
        freed->prev = prev;
        prev->next = freed;
        next->prev = freed;
    }
    dbg_printf("added %p to %d\n", freed, (int) index);
    */return;
}

/*
 * delete_free deletes the free block from the explicit free list
 */

static void delete_free(block_t *removed) {
    size_t size = get_size(removed);
    size_t index = get_seg_size(size);

    block_t *prev = get_prev_freed(removed);
    block_t *next = get_next_freed(removed);
    dbg_printf("\n prev: %p ", prev);
    dbg_printf("next : %p", next);
    // Case 1. The only element on the free list
    if (prev == NULL || next == NULL) {
        seg_list[index] = NULL;
        dbg_printf("  deleted the only element in %d", (int)index);
        dbg_printf("\n hahaha %p haha \n", seg_list[9]);
    }

    // Case 2. At the front of free list
    else if ((prev == NULL) && (next != NULL)) {
        next->prev = NULL;
        seg_list[index] = next;
    }

    // Case 3. At the end of free list
    else if ((prev != NULL) && (next == NULL)) {
        prev->next = NULL;
    }

    // Case 4. At the middle
    else {
        next->prev = prev;
        prev->next = next;
    }
    return;
}


/*
 * mm_init initiates a heap that will be used for memory allocation.
 * First it increases the heap, then places prologue block with
 * epilogue header, extends heap again, and coalesces the free block.
 */
bool mm_init(void)
{
    // Create the initial empty heap
    word_t *start = (word_t *)(mem_sbrk(2*dsize));

    for (size_t i = 0; i < seg_size; i++) {
        seg_list[i] = NULL;
    }

    if (start == (void *)-1)
    {
        return false;
    }
    start[0] = pack(0, 0); // alignment padding
    start[1] = pack(dsize, true);
    start[2] = pack(dsize, true);
    start[3] = pack(0, true);
    /* create prologue block */

    // Heap starts with first "block header", currently the epilogue footer
    heap_start = (block_t *) &(start[3]);

    // Extend the empty heap with a free block of chunksize bytes
    if (extend_heap(chunksize) == NULL)
    {
        return false;
    }
    return true;
}

/*
 * malloc takes size as its input, initializes the heap
 * if it wasn't initialized, looks for a free block that fits the
 * size using find_fit. If there is no fit, extends the heap and
 * allocates the block. Returns the pointer to payload of the block.
 */
void *malloc(size_t size)
{
    //dbg_requires(mm_checkheap(__LINE__));
    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit is found
    block_t *block;
    void *bp = NULL;

    if (heap_start == NULL) // Initialize heap if it isn't initialized
    {
        mm_init();
    }

    if (size == 0) // Ignore spurious request
    {
        dbg_ensures(mm_checkheap(__LINE__));
        return bp;
    }

    // Adjust block size to include overhead and to meet alignment requirements
    asize = round_up(max(MIN_BLOCK_SIZE, dsize + size) , dsize);

    // Search the free list for a fit
    block = find_fit(asize);

    // If no fit is found, request more memory, and then and place the block
    if (block == NULL)
    {
        extendsize = max(asize, chunksize);
        block = extend_heap(extendsize);
        if (block == NULL) // extend_heap returns an error
        {
            return bp;
        }

    }

    place(block, asize);
    bp = header_to_payload(block);

    //dbg_ensures(mm_checkheap(__LINE__));
    return bp;
}

/*
 * free takes in pointer to a payload of the block that was allocated
 * by calls to malloc, realloc or calloc, and frees the block.
 * Coalesce with the adjacent free blocks if they exist.
 */
void free(void *bp)
{
    if (bp == NULL)
    {
        return;
    }

    block_t *block = payload_to_header(bp);
    bool alloced = get_alloc(block);
    if (!alloced) return;

    size_t size = get_size(block);

    write_header(block, size, false);
    write_footer(block, size, false);
    dbg_printf("free coalesced: \n");
    coalesce(block);

}

/*
 * realloc(ptr, size) has three cases.
 * 1. If ptr == NULL, the call is equivalent to malloc(size).
 * 2. If size == 0, the call is equivalent to free(ptr) and returns NULL
 * 3. If ptr is non-NULL, it copies the memory from old block pointed
 * by ptr, returns a new pointer to which realloc copied the old block
 * memory to.
 */
void *realloc(void *ptr, size_t size)
{
    block_t *block = payload_to_header(ptr);
    size_t copysize;
    void *newptr;

    // If size == 0, then free block and return NULL
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (ptr == NULL)
    {
        return malloc(size);
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);
    // If malloc fails, the original block is left untouched
    if (newptr == NULL)
    {
        return NULL;
    }

    // Copy the old data
    copysize = get_payload_size(block); // gets size of old payload
    if(size < copysize)
    {
        copysize = size;
    }
    memcpy(newptr, ptr, copysize);

    // Free the old block
    free(ptr);

    return newptr;
}

/*
 * calloc(elements, size) acts equivalently as malloc(size*elements),
 * except that calloc initializes the elements allocated to 0.
 */
void *calloc(size_t elements, size_t size)
{
    void *bp;
    size_t asize = elements * size;

    if (asize/elements != size)
    // Multiplication overflowed
    return NULL;

    bp = malloc(asize);
    if (bp == NULL)
    {
        return NULL;
    }
    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}

/******** The remaining content below are helper and debug routines ********/

/*
 * extend_heap extends the heap using mem_sbrk function, according to
 * its input size. It creates a new block from the extended heap,
 * and coalesces if there are any adjacent free blocks.
 */
static block_t *extend_heap(size_t size)
{
    void *bp;

    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);
    if ((bp = mem_sbrk(size)) == (void *)-1)
    {
        return NULL;
    }

    // Initialize free block header/footer
    block_t *block = payload_to_header(bp);

    write_header(block, size, false);
    write_footer(block, size, false);
    // Create new epilogue header
    block_t *block_next = find_next(block);
    write_header(block_next, dsize, true);
    write_footer(block_next, dsize, true);

    block_t *target;
    for (size_t index = 0; index < seg_size; index++) {
        for (target = seg_list[index]; target != NULL;
                target = get_next_freed(target)) {
            if (target == block) {
                dbg_printf("before extension, deleted: %p\n", block);
                delete_free(block);
                break;
            }
        }
    }
    dbg_printf("heap extended at: %p\n", block);
    // coalesce in case the previous block was free
    dbg_printf("heap coalescion:\n");
    return coalesce(block);
}

/*
 * coalesce takes in the block that was just freed,
 * checks if any adjacent block is also freed, coalesces if true,
 * and adds the resulting block to the explicit free list
 */
static block_t *coalesce(block_t * block)
{
    block_t *block_next = find_next(block);
    block_t *block_prev = find_prev(block);
    bool prev_alloc = get_alloc(block_prev);
    bool next_alloc = get_alloc(block_next);
    size_t size = get_size(block);

    // Case 1. next block is free
    if (prev_alloc && !next_alloc) {
        dbg_printf("coalesce next free: %p\n", block_next);
        size += get_size(block_next);
        delete_free(block_next);
        write_header(block, size, false);
        write_footer(block, size, false);
    }

    // Case 2. prev block is free
    else if (!prev_alloc && next_alloc) {
        dbg_printf("coalesce prev free: %p\n", block_prev);
        size += get_size(block_prev);
        delete_free(block_prev);
        write_header(block_prev, size, false);
        write_footer(block_prev, size, false);
        block = block_prev;
    }

    // Case 3. both prev and next are free
    else if (!prev_alloc && !next_alloc) {
        dbg_printf("coalesce both prev: %p next: %p \n", block_prev, block_next);
        size += get_size(block_next) + get_size(block_prev);
        delete_free(block_prev);
        delete_free(block_next);
        write_header(block_prev, size, false);
        write_footer(block_prev, size, false);

        block = block_prev;
    }
    dbg_printf("added to free list (no coalesced) : %p\n", block);
    add_free(block, size);
    return block;
}

/*
 * place allocates a free block upon request from malloc, callor or realloc.
 * It requires to take a block bigger than the size requested, and
 * if the remaining size of the free block after placing is bigger than
 * the minimum block size, split the block. Else, allocate the whole block.
 */
static void place(block_t *block, size_t asize)
{
    size_t csize = get_size(block);
    delete_free(block);

    // if the remaining free block size is bigger than minimum size
    if ((csize - asize) >= MIN_BLOCK_SIZE) {
        write_header(block, asize, true);
        write_footer(block, asize, true);
        dbg_printf("placed at %p: coalescing splitted block:",block);
        block_t *block_next = find_next(block);
        dbg_printf(" %p\n", block_next);
        write_header(block_next, csize-asize, false);
        write_footer(block_next, csize-asize, false);
        add_free(block_next, (csize - asize));
    }

    else { // remaining block size is small, so allocate whole block
        write_header(block, csize, true);
        write_footer(block, csize, true);
        dbg_printf("only placed: (%p)\n", block);
    }
}

/*
 * find_fit looks for a block using first fit or best fit according to
 * the constant best_fit. best_fit is also modified by limiting the
 * number of searched after a fit is found. It traverses from the
 * end of the free list.
 */
static block_t *find_fit(size_t asize)
{
    block_t *block;
    size_t size = asize;
    size_t index = get_seg_size(asize);
    while (index < seg_size) {
        if ((index == seg_size - 1) || ((size <= 1) && (seg_list[index]))){

            block = seg_list[index];
            while (block != NULL) {
                if (asize <= get_size(block)) {
                    return block;
                }
                block = get_next_freed(block);
            }
        }
        index++;
    }
    return NULL;
}

/*
 * check_prologue_and_epilogue is a helper function for mm_checkheap
 * for checking whether prologue block and epilogue blocks are consistent
 */
bool check_prologue_and_epilogue(void) {

    block_t *prologue = find_prev(heap_start);
    word_t p_header = prologue->header;
    word_t p_footer = *(find_prev_footer(heap_start));
    word_t e_header = epilogue->header;

    // if prologue block is not consistent
    if ((p_header != p_footer) ||
            (p_header != pack(MIN_BLOCK_SIZE, true))) {
        dbg_printf("Prologue block Inconsistent");
        return false;
    }

    // if epilogue block is erroneous
    if (e_header != pack(0, true)) {
        dbg_printf("\n%p\n", mem_heap_hi());
        dbg_printf("\n%p\n", epilogue);
        dbg_printf("Epilogue block inconsistent");
        return false;
    }

    // if both not, return true;
    return true;
}

/*
 * check_block_consistency traverses through all blocks, checks for
 * header and footer, and block consistencies
 */
bool check_block_consistency(void) {
    block_t *block;
    word_t header, footer;
    size_t size;
    // traverse through all blocks
    for (block = heap_start; get_size(block) != 0;
            block = find_next(block)) {

        // check for block(header) alignment

        if (((unsigned long)block % 16lu) != 8lu) {
            dbg_printf("header alignment wrong! :%p",block);
            return false;
        }

        // check for payload alignment
        if (((unsigned long)(block->payload) % 16lu)) {
            dbg_printf("payload alignment wrong! :%p", block);
            return false;
        }

        // check for block size
        size = get_size(block);
        if (size < MIN_BLOCK_SIZE) {
            dbg_printf("Block (%p) size < min_size!", block);
            return false;
        }

        // check for header and footer consistency
        header = block->header;
        footer = *((word_t *) (((char *)block) + (size - wsize)));
        if (header != footer) {
            dbg_printf("header and footer inconsistent: %p", block);
            return false;
        }
    }

    // if not all of them, return true
    return true;
}

/*
 * check_freelist checks the following for free list
 * 1. checks if the free block's alloc bit is false.
 * 2. checks if the free block is in right range.
 * 3. checks if the free block can't be coalesced any further.
 * 4. checks if the free block's prev and next pointer are valid.
 * 5. checks if the number of free blocks by traversing through the
 *    whole free list is equal to the number of free blocks by
 *    traversing through the all blocks.
 */

bool check_freelist(void) {
/*
    block_t *free_block; //target free block
    block_t *prev; // prev block on heap
    block_t *next; // next block on heap
    block_t *prev_free; // prev free block on free list
    block_t *next_free; // next free block on free list

    size_t total_free_1 = 0; // number of total free blocks using free list
    size_t total_free_2 = 0; // # of total blocks using all heap

    for (free_block = free_start; free_block != NULL;
            free_block = get_next_freed(free_block)) {

        // first check for alloc bit consistency
        if (get_alloc(free_block)) {
            dbg_printf("Alloc bit inconsistent: %p", free_block);
            return false;
        }

        // check if the pointer is in right range
        if (((unsigned long)free_block < (unsigned long)mem_heap_lo()) ||
            ((unsigned long)free_block > (unsigned long) mem_heap_hi())) {
            dbg_printf("Free block (%p) not in right range ", free_block);
            return false;
        }

        // check if coalesced right
        prev = find_prev(free_block);
        next = find_next(free_block);
        if (!get_alloc(prev) || !get_alloc(next)) {
            dbg_printf("Coalescion wrong (%p)", free_block);
            return false;
        }

        // check for pointer consistency
        prev_free = free_block->prev;
        next_free = free_block->next;
        if (prev_free) {
            if (prev_free->next != free_block) {
                dbg_printf("Prev free block inconsistent: %p", free_block);
                return false;
            }
        }
        if (next_free) {
            if (next_free->prev != free_block) {
                dbg_printf("Next free block inconsistent: %p", free_block);
                return false;
            }
        }
        total_free_1++; // counting number of free blocks
    }

    // now count free blocks by traversing through all blocks
    for (free_block = heap_start; get_size(free_block) != 0;
            free_block = find_next(free_block)) {

        if (!get_alloc(free_block)) total_free_2++;
    }

    return (total_free_1 == total_free_2);
*/  return true;
}


/*
 * mm_checkheap checks for the consistency of prologue and epilogue block,
 * checks block consistencies (such as alignment and header == footer) for
 * each block, and check the consistency for free list. If any of them fail,
 * it returns false and prints the line number that went wrong.
 */
bool mm_checkheap(int line) {

    if (!check_prologue_and_epilogue()) {
        dbg_printf("Prologue and epilogue block inconsistent!:%d\n",line);
        return false;
    }

    if (!check_block_consistency()) {
        dbg_printf("Block consistency check failed!:%d\n", line);
        return false;
    }

    if (!check_freelist()) {
        dbg_printf("Free list consistency check failed!:%d\n",line);
        return false;
    }

    return true;
}


/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details within your header comments for the functions above!     *
 *                                                                           *
 *                                                                           *
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a de ad be ef 0a 0a 0a               *
 *                                                                           *
 *****************************************************************************
 */


/*
 * max: returns x if x > y, and y otherwise.
 */
static size_t max(size_t x, size_t y)
{
    return (x > y) ? x : y;
}

/*
 * round_up: Rounds size up to next multiple of n
 */
static size_t round_up(size_t size, size_t n)
{
    return (n * ((size + (n-1)) / n));
}

/*
 * pack: returns a header reflecting a specified size and its alloc status.
 *       If the block is allocated, the lowest bit is set to 1, and 0 otherwise.
 */
static word_t pack(size_t size, bool alloc)
{
    return alloc ? (size | alloc_mask) : size;
}


/*
 * extract_size: returns the size of a given header value based on the header
 *               specification above.
 */
static size_t extract_size(word_t word)
{
    return (word & size_mask);
}

/*
 * get_size: returns the size of a given block by clearing the lowest 4 bits
 *           (as the heap is 16-byte aligned).
 */
static size_t get_size(block_t *block)
{
    return extract_size(block->header);
}

/*
 * get_payload_size: returns the payload size of a given block, equal to
 *                   the entire block size minus the header and footer sizes.
 */
static word_t get_payload_size(block_t *block)
{
    size_t asize = get_size(block);
    return asize - dsize;
}

/*
 * extract_alloc: returns the allocation status of a given header value based
 *                on the header specification above.
 */
static bool extract_alloc(word_t word)
{
    return (bool)(word & alloc_mask);
}

/*
 * get_alloc: returns true when the block is allocated based on the
 *            block header's lowest bit, and false otherwise.
 */
static bool get_alloc(block_t *block)
{
    return extract_alloc(block->header);
}

/*
 * write_header: given a block and its size and allocation status,
 *               writes an appropriate value to the block header.
 */
static void write_header(block_t *block, size_t size, bool alloc)
{
    block->header = pack(size, alloc);
}


/*
 * write_footer: given a block and its size and allocation status,
 *               writes an appropriate value to the block footer by first
 *               computing the position of the footer.
 */
static void write_footer(block_t *block, size_t size, bool alloc)
{
    word_t *footerp = (word_t *)((block->payload) + get_size(block) - dsize);
    *footerp = pack(size, alloc);
}


/*
 * find_next: returns the next consecutive block on the heap by adding the
 *            size of the block.
 */
static block_t *find_next(block_t *block)
{
    dbg_requires(block != NULL);
    block_t *block_next = (block_t *)(((char *)block) + get_size(block));

    dbg_ensures(block_next != NULL);
    return block_next;
}

/*
 * find_prev_footer: returns the footer of the previous block.
 */
static word_t *find_prev_footer(block_t *block)
{
    // Compute previous footer position as one word before the header
    return (&(block->header)) - 1;
}

/*
 * find_prev: returns the previous block position by checking the previous
 *            block's footer and calculating the start of the previous block
 *            based on its size.
 */
static block_t *find_prev(block_t *block)
{
    word_t *footerp = find_prev_footer(block);
    size_t size = extract_size(*footerp);
    return (block_t *)((char *)block - size);
}

/*
 * payload_to_header: given a payload pointer, returns a pointer to the
 *                    corresponding block.
 */
static block_t *payload_to_header(void *bp)
{
    return (block_t *)(((char *)bp) - offsetof(block_t, payload));
}

/*
 * header_to_payload: given a block pointer, returns a pointer to the
 *                    corresponding payload.
 */
static void *header_to_payload(block_t *block)
{
    return (void *)(block->payload);
}
