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
mp_pool_get(mp_pool *pool);

/**
 * Return a chunk to the pool.
 *
 * Updates:
 *  - Free-list in page
 *  - Rotates page to back of list
 */
static __inline__ void
mp_pool_ret(mp_pool *pool, const mp_chunk *chunk);


#ifdef __cplusplus
}
#endif

#endif /* QDEEP_MATRIXP_POOL_H */
