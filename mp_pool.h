/**
 * ============================================================================
 *  Project:      QDeep / MatrixP
 *  File:         mp_pool.h
 *  Description:  Pool allocator managing pages of chunks.
 *
 *  Responsibilities:
 *    - Maintain pages in a Red-Black tree (fast search by data pointer)
 *    - Maintain pages in a doubly-linked list (fast iteration / rotation)
 *    - Allocate and free individual chunks
 *    - Handle page creation and destruction transparently
 *
 *  Notes:
 *    - Pages are allocated via mmap inside mp_page
 *    - RB-tree ensures O(log N) lookup of a page given a chunk
 *    - List rotation implements simple FIFO for load balancing
 *
 *  Copyright:
 *      (c) 2025 QDeep.Net
 * ============================================================================
 */

#ifndef QDEEP_MATRIXP_POOL_H
#define QDEEP_MATRIXP_POOL_H

#include "mp_page.h"

#ifdef __cplusplus
extern "C" {

#endif


/* ============================================================================
 *  Pool structure
 * ============================================================================
 */

/**
 * Chunk page pool.
 *
 * Contains:
 *  - head/root pointers for list and RB-tree
 *  - pool size
 *  - temporary stack for tree insert operations
 */
typedef struct mp_pool {
    mp_page *head; /**< Head of page list */
    mp_page *root; /**< Root of RB-tree (indexed by data ptr) */
    uint32_t size; /**< Total number of pages */

    /* ------------------------------------------------------------------------
     * Temporary stack for RB-tree insertion balancing
     * ---------------------------------------------------------------------- */
    mp_page *stack[32]; /**< Ancestor nodes during tree traversal */
    uint8_t  sides[32]; /**< Side taken at each level (0=left,1=right) */
} mp_pool;


/* ============================================================================
 *  Pool initialization / destruction
 * ============================================================================
 */

/**
 * Initialize a pool.
 */
static __inline__ void
mp_pool_init(mp_pool *pool) {
    pool->head = NULL;
    pool->root = NULL;
    pool->size = 0;
}

/**
 * Initialize pool for GPU variant (currently same as CPU).
 */
static __inline__ void
mp_pool_init_gpu(mp_pool *pool) {
    mp_pool_init(pool);
}

/**
 * Free all pages in the pool and their memory.
 *
 * Notes:
 *   - Iterates page list
 *   - Calls mp_page_free for each
 */
static __inline__ void
mp_pool_free(const mp_pool *pool) {
    for (mp_page *page = pool->head, *next; page != NULL; page = next) {
        next = page->nextp;
        mp_page_free(page);
        free(page);
    }
}


/* ============================================================================
 *  Page list operations
 * ============================================================================
 */

/**
 * Insert a page into the circular doubly-linked list.
 */
static __inline__ void
mp_pool_list_insert(mp_pool *pool, mp_page *page) {
    mp_page *head = pool->head ? pool->head : page;
    mp_page *last = pool->head ? pool->head->prevp : page;

    page->nextp = head;
    page->prevp = last;

    head->prevp = page;
    last->nextp = page;

    pool->head = page;
    pool->size += 1;
}

/**
 * Remove a page from the list.
 */
static __inline__ void
mp_pool_list_remove(mp_pool *pool, const mp_page *page) {
    mp_page *prev = page->prevp;
    mp_page *next = page->nextp;

    prev->nextp = next;
    next->prevp = prev;

    pool->head = (--pool->size == 0) ? NULL : (pool->head == page ? next : pool->head);
}

/**
 * Rotate head pointer to next page (simple FIFO rotation).
 */
static __inline__ void
mp_pool_list_rotate(mp_pool *pool) {
    if (pool->head) pool->head = pool->head->nextp;
}


/* ============================================================================
 *  RB-tree operations
 * ============================================================================
 */

/**
 * Insert a page into the RB-tree based on its data pointer.
 *
 * Notes:
 *  - Uses pool->stack and pool->sides for temporary state
 *  - Performs standard RB-tree balancing
 */
static __inline__ void
mp_pool_tree_insert(mp_pool *pool, mp_page *page) {
    uint8_t side = 0;
    int8_t pos = -1;
    mp_page *node = pool->root;

    /* Find insert position */
    while (node) {
        pool->stack[++pos] = node;
        side = pool->sides[pos] = node->data < page->data;
        node = node->sides[side];
    }

    /* Insert As Red Colored Node */
    page->color = MP_RED;
    page->sides[0] = NULL;
    page->sides[1] = NULL;

    if (pos == -1) pool->root = page;
    else pool->stack[pos]->sides[side] = page;

    /* Rebalance tree */
    while (--pos >= 0) {
        side = pool->sides[pos];
        mp_page *g_ = pool->stack[pos]; // Grandparent
        mp_page *y_ = g_->sides[1 - side]; // Uncle
        mp_page *x_ = pool->stack[pos + 1]; // Parent

        if (x_->color == MP_BLACK) break;

        if (y_ && y_->color == MP_RED) {
            x_->color = MP_BLACK;
            y_->color = MP_BLACK;
            g_->color = MP_RED;
            --pos;
            continue;
        }

        if (side == 1 - pool->sides[pos + 1]) {
            y_ = x_->sides[1 - side];
            x_->sides[1 - side] = y_->sides[side];
            y_->sides[side] = x_;
            x_ = g_->sides[side] = y_;
        }

        g_->color = MP_RED;
        x_->color = MP_BLACK;
        g_->sides[side] = x_->sides[1 - side];
        x_->sides[1 - side] = g_;

        if (pos == 0) pool->root = x_;
        else pool->stack[pos - 1]->sides[pool->sides[pos - 1]] = x_;
        break;
    }

    pool->root->color = MP_BLACK;
}

/**
 * Find the page containing a given chunk using the RB-tree.
 */
static __inline__ mp_page *
mp_pool_tree_find(const mp_pool *pool, const mp_chunk *chunk) {
    mp_page *node = pool->head;

    while (node != NULL) {
        if (chunk >= node->chunk && chunk < node->chunk + PAGE_SIZE) break;
        node = node->sides[node->data < chunk->data];
    }
    return node;
}


/* ============================================================================
 *  Chunk allocation / return
 * ============================================================================
 */

/**
 * Allocate a chunk from the pool.
 *
 * Strategy:
 *  - Try head page first
 *  - Create new page if necessary
 *  - Rotate list if head page is full
 */
static __inline__ mp_chunk *
mp_pool_get(mp_pool *pool) {
    mp_page *page = pool->head;
    mp_chunk *chunk = NULL;

    if (!page || mp_page_full(page)) {
        page = (mp_page *) malloc(sizeof(mp_page));
        if (!page) goto end;
        if (mp_page_init(page)) goto end;

        mp_pool_tree_insert(pool, page);
        mp_pool_list_insert(pool, page);
    }

    chunk = mp_page_get_new(page);
    if (mp_page_full(page)) mp_pool_list_rotate(pool);

end:
    if (!chunk && page) free(page);
    return chunk;
}

/**
 * Return a chunk to the pool.
 *
 * Updates:
 *  - Free-list in page
 *  - Rotates page to back of list
 */
static __inline__ void
mp_pool_ret(mp_pool *pool, const mp_chunk *chunk) {
    mp_page *page = mp_pool_tree_find(pool, chunk);

    mp_page_ret(page, chunk);

    mp_pool_list_remove(pool, page);
    mp_pool_list_insert(pool, page);
}


#ifdef __cplusplus
}
#endif

#endif /* QDEEP_MATRIXP_POOL_H */
