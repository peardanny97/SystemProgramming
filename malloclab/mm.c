/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 */


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* from textbook Basic Concepts & Macros */

#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8
#define CHUNKSIZE (1<<7) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read a word at address p */
#define GET(p) (*(unsigned int *)(p))
/* Write a word at address p */
#define PUT(p, val) (*(unsigned int *)(p) = (val))
/* Set pointer */
#define SET(p, block) (*(unsigned int *)(p) = (unsigned int)(block))

/* Get Size */
#define GET_SIZE(p) (GET(p) & ~0x7)
/* Get Alloc */
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Get Header addr from block ptr as We don't use any padding no need for offsetof(x,y) */
#define HDRP(block) ((char *)(block) - WSIZE)
/* Get Footer addr from block ptr it will be Block + block size - DSIZE(header + footer) */
#define FTRP(block) ((char *)(block) + GET_SIZE(HDRP(block)) - DSIZE)

/* Get next block from current block addr */
#define NEXT_BLKP(block) ((char *)(block) + GET_SIZE(HDRP(block)))
/* Get prev block from current block addr get prev block size from prev block's footer */
#define PREV_BLKP(block) ((char *)(block) - GET_SIZE(HDRP(block) - WSIZE))

/* Get predecessor_ptr from block ptr */
#define PRED_PTR(block) ((char *)(block))
#define PRED(block) (*(char **)((char*)(block)))
/* Get successor_ptr from block ptr */
#define SUCC_PTR(block) ((char *)(block) + WSIZE)
#define SUCC(block) (*(char **)((char *)(block) + WSIZE))

#define MAXLENGTH 16




static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void * best_fit(size_t asize);
static void place(void *block, size_t asize);
static void list_init(void);
static int find_idx(size_t size);
static void insert_node(void * block);
static void delete_node(void *block);
static void place_realloc(void *block, size_t asize);
static char *heap_listp = 0;

/* free list length of 16 */
static void *seg_free_list[MAXLENGTH];
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    list_init();
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */

    /* Extend the empty heap with a free block with size of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

static void *extend_heap(size_t words)
{
    char *block;
    size_t size;

    /* alignment check and allocate size with word size */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(block = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(block), PACK(size, 0)); /* initialize block header with free */
    PUT(FTRP(block), PACK(size, 0)); /* initialize block footer with free */

    PUT(HDRP(NEXT_BLKP(block)), PACK(0, 1)); /* initialize new epilogue header with free */
    insert_node(block); /* insert node into free list */

    /* Coalesce if the previous block was free */

    // for(int a = 0; a<MAXLENGTH; a++){
    //     if (seg_free_list[a]!=NULL)
    //         printf("extend: seg %dth ptr with size %d \n",a,GET_SIZE(HDRP(seg_free_list[a])));
    // }
    // printf("\n");

    return coalesce(block);

}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */

void *mm_malloc(size_t size)
{

    size_t asize;  /* adjusted blockk size */
    char *block;
    if (heap_listp == 0) mm_init();
    if (size == 0) return NULL;
    /* adjust size to match alignment */
    asize = (size <= DSIZE) ? 2 * DSIZE : ALIGN(size + DSIZE);
    //asize = (size <= DSIZE) ? 2 * DSIZE : DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    // for(int a = 0; a<MAXLENGTH; a++){
    //     if (seg_free_list[a]!=NULL)
    //         printf("before place seg %dth ptr : %p \n",a,seg_free_list[a]);
    // }
    // printf("\n");
    /* asize fit free list */
    if ((block = best_fit(asize)) != NULL) {
        // printf("first fit! for size %d, %dth idx\n",asize, find_idx(asize));
        // printf("\n");
        place(block, asize);
        // for(int a = 0; a<MAXLENGTH; a++){
        //     if (seg_free_list[a]!=NULL)
        //         printf("after place seg %dth ptr : size of %d \n",a,GET_SIZE(HDRP(seg_free_list[a])));
        // }
        // printf("\n");
        return block;
    }

    /* if no space for free list, need to extend heap */
    size_t extendsize = MAX(asize, CHUNKSIZE);  /* extend heap size */
    if ((block = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(block, asize);
    // for(int a = 0; a<MAXLENGTH; a++){
    //         if (seg_free_list[a]!=NULL)
    //             printf("place %d after heap extended seg %dth ptr : size of %d \n",asize, a,GET_SIZE(HDRP(seg_free_list[a])));
    // }

    return block;

}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    /* get size first */
    size_t size = GET_SIZE(HDRP(ptr));
    /* set header & footer free */
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    /* insert to free list */
    insert_node(ptr);
    /* call coalesce */
    coalesce(ptr);
    // for(int a = 0; a<MAXLENGTH; a++){
    //         if (seg_free_list[a]!=NULL)
    //             printf("after free seg %dth ptr : size of %d \n",a,GET_SIZE(HDRP(seg_free_list[a])));
    // }
}

static void *coalesce(void *bp)
{
    /* check either prev or next block is allocated & get size of current block */
    void *prev_bp = PREV_BLKP(bp);
    void *next_bp = NEXT_BLKP(bp);
    size_t prev_alloc = GET_ALLOC(HDRP(prev_bp));
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t size = GET_SIZE(HDRP(bp));

    /* if prev & next is allocated no need for coalesce */
    if (prev_alloc && next_alloc){
        return bp;
    }
    /* if next block is allocated but prev is not */
    else if (!prev_alloc && next_alloc){
        size += GET_SIZE(HDRP(prev_bp));
        /* delete current & prev node in free list */
        delete_node(bp);
        delete_node(prev_bp);

        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));

        bp = prev_bp;

        /* insert coalesced block to list */
        insert_node(bp);
    }
    /* if prev block is allocated but next is not */
    else if (prev_alloc && !next_alloc){
        // printf("AM I RIGHT?? %d\n\n\n",size);
        size += GET_SIZE(HDRP(next_bp));
        /* delete current & next node in free list */
        delete_node(bp);
        delete_node(next_bp);

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));

        /* insert coalesced block to list */
        insert_node(bp);
    }
    /* both sides are not allocated */
    else {
        size += GET_SIZE(HDRP(next_bp)) + GET_SIZE(HDRP(prev_bp));
        /* delete current & next & prev node in free list */
        delete_node(bp);
        delete_node(next_bp);
        delete_node(prev_bp);

        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(next_bp), PACK(size, 0));
        bp = prev_bp;

        /* insert coalesced block to list */
        insert_node(bp);
    }
    // for(int a = 0; a<MAXLENGTH; a++){
    //     if (seg_free_list[a]!=NULL)
    //         printf("coalesce: seg %dth ptr with size %d \n",a,GET_SIZE(HDRP(seg_free_list[a])));
    // }
    // printf("\n");
    return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */

void *mm_realloc(void *ptr, size_t size)
{
    

    /* same as free */
    if (size == 0){
        mm_free(ptr);
        return NULL;
    }
    /* same as mm_malloc */
    if (ptr == NULL) {
        return mm_malloc(size);
    }
    /* reallocation */
    void *oldptr = ptr;
    void *newptr;
    size_t oldSize = GET_SIZE(HDRP(ptr)); // get old size
    size_t newSize = (size <= DSIZE) ? 2 * DSIZE : ALIGN(size + DSIZE);
    if(newSize == oldSize){
        return oldptr; // size hasn't changed no need to reallocation
    }
    else if(newSize < oldSize){
        place_realloc(oldptr, newSize);
        return oldptr;
    }
    else{
        /* first check if next block is epilogue */
        if(GET_SIZE(HDRP(NEXT_BLKP(oldptr))) == 0){
            size_t extendsize = MAX((newSize-oldSize), CHUNKSIZE);
            if (extend_heap(extendsize/WSIZE) == NULL) return NULL;
            oldSize += extendsize;
            delete_node(NEXT_BLKP(oldptr));
            PUT(HDRP(oldptr), PACK(oldSize, 1));
            PUT(FTRP(oldptr), PACK(oldSize, 1));
            place_realloc(oldptr, newSize);
            return oldptr;
        }
        /* check if next block is free */
        else if(!GET_ALLOC(HDRP(NEXT_BLKP(oldptr)))){
            size_t nextSize = GET_SIZE(HDRP(NEXT_BLKP(oldptr)));
            oldSize += nextSize;
            /* check if it's available to use for new block */
            if(newSize <= oldSize){
                // printf("hi? %p\n", NEXT_BLKP(oldptr));
                // printf("hi? %d\n", GET_SIZE(HDRP(NEXT_BLKP(oldptr))));
                delete_node(NEXT_BLKP(oldptr));
                PUT(HDRP(oldptr), PACK(oldSize, 1));
                PUT(FTRP(oldptr), PACK(oldSize, 1));
                place_realloc(oldptr, newSize);
                return oldptr;
            }
        }
    }
    newptr = mm_malloc(size);
    if (newptr == NULL) return NULL;
    memcpy(newptr, oldptr, oldSize);
    mm_free(oldptr);
    return newptr;
    // newptr = mm_malloc(size);
    // if (newptr == NULL)
    //   return NULL;
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    // if (size < copySize)
    //   copySize = size;
    // memcpy(newptr, oldptr, copySize);
    // mm_free(oldptr);
    // return newptr;
}

/*
 * first_fit - Implement first fit for implicit list
 * first fit's no good, change to best fit
 */
static void * best_fit(size_t asize)
{
    void *block;
    int list_idx = find_idx(asize);
    block = seg_free_list[list_idx];
    void *best_block = NULL;
    size_t best_size = 0;
    /* search through matched size list */
    while(block != NULL){
        if(GET_SIZE(HDRP(block))<asize){
            block = SUCC(block);
            continue;
        }
        else {
            if ( (best_size == 0) || (GET_SIZE(HDRP(block)) <= best_size)){
                best_size = GET_SIZE(HDRP(block));
                best_block = block;
                if(best_size == asize){
                    break;
                }
                block = SUCC(block);
            }
            else {
                block = SUCC(block);
            }
        }
    }
    block = best_block;
    // while( (block != NULL) && (GET_SIZE(HDRP(block)) < asize) ){
    //     block = SUCC(block);
    // }
    if (block != NULL) return block;  // found first fit



    /* if first fit of matched size is failed, need to search trough list with bigger sizes */
    list_idx ++;
    while(list_idx < MAXLENGTH){
        if(seg_free_list[list_idx] != NULL){ //found free list with bigger size
            return seg_free_list[list_idx];
        }
        list_idx++;
    }
    /* there's no free space in heap */
    return NULL;

}

static void place(void *block, size_t asize)
{

    size_t size = GET_SIZE(HDRP(block));
    /* delete block from free list */
    delete_node(block);

    /* if block has enough size for split */
    if( (size-asize) >= (2 * DSIZE) ){
        PUT(HDRP(block), PACK(asize, 1));
        PUT(FTRP(block), PACK(asize, 1));
        block = NEXT_BLKP(block);
        PUT(HDRP(block), PACK(size - asize, 0));
        PUT(FTRP(block), PACK(size - asize, 0));
        /* insert remainder to free list */
        insert_node(block);
    }
    else {
        PUT(HDRP(block), PACK(size, 1));
        PUT(FTRP(block), PACK(size, 1));
    }
}

/* same as place but no need to delete node from free list */
static void place_realloc(void *block, size_t asize)
{

    size_t size = GET_SIZE(HDRP(block));
    /* delete block from free list */

    /* if block has enough size for split */
    if( (size-asize) >= (2 * DSIZE) ){
        PUT(HDRP(block), PACK(asize, 1));
        PUT(FTRP(block), PACK(asize, 1));
        block = NEXT_BLKP(block);
        PUT(HDRP(block), PACK(size - asize, 0));
        PUT(FTRP(block), PACK(size - asize, 0));
        /* insert remainder to free list */
        insert_node(block);
    }
    else {
        PUT(HDRP(block), PACK(size, 1));
        PUT(FTRP(block), PACK(size, 1));
    }
}

/*
 *  ftn for segregated free list
 */

/* initialize seg free list */
static void list_init(void)
{
    for(int i=0; i< MAXLENGTH; i ++){
        seg_free_list[i] = NULL;
    }
}

/* find index of pointer for seg free list */
static int find_idx(size_t size)
{
    int idx = 0;
    /* size 1~31 idx 0 */
    if (0 < size && size < 32){
        return idx;
    }
    size >>= 4;
    /* after size of 32, divide free list with power of 2 until maxlength of list, 0~31, 32~63, .... */
    while (size > 1 && idx < MAXLENGTH){
        size >>= 1;
        idx++;
    }
    return idx;
}

/* insert node into seg free list */
static void insert_node(void * block)
{
    size_t size = GET_SIZE(HDRP(block));
    /* get ptr to seg free list by find_idx */
    int list_idx = find_idx(size);
    void *list_ptr = seg_free_list[list_idx];

    /* LIFO structure */
    if (list_ptr == NULL){  // list is empty
        SET(PRED_PTR(block), NULL);
        SET(SUCC_PTR(block), NULL);
        seg_free_list[list_idx] = block;
    }
    else {
        SET(PRED_PTR(block), NULL);
        SET(SUCC_PTR(block), list_ptr);
        SET(PRED_PTR(list_ptr), block);
        seg_free_list[list_idx] = block;
    }
    return;
}

static void delete_node(void *block)
{
    size_t size = GET_SIZE(HDRP(block));
    int list_idx = find_idx(size);
    /* if block is only free block of list */
    if (SUCC(block) == NULL && PRED(block) == NULL) {
        seg_free_list[list_idx] = NULL;
        return;
    }
    /* if block is TAIL */
    else if (SUCC(block) == NULL) {
        SET(SUCC_PTR(PRED(block)), NULL); // set pred to tail
        return;
    }
    /* if block is HEAD */
    else if (PRED(block) == NULL) {
        SET(PRED_PTR(SUCC(block)), NULL); // set succ to head
        seg_free_list[list_idx] = SUCC(block);
        return;
    }
    /* middle of the list */
    else {
        SET(SUCC_PTR(PRED(block)), SUCC(block));
        SET(PRED_PTR(SUCC(block)), PRED(block));
        return;
    }
}


