/**
 * ============================================================================
 *  Project:      QDeep / MatrixP
 *  File:         mp_page.h
 *  Description:  Page-level allocator for chunk-based matrix storage.
 *
 *  A "page" owns:
 *   - One large contiguous memory region (mmap)
 *   - PAGE_SIZE fixed-size chunks
 *   - A free-list for chunk reuse
 *   - Tree and list links for global management
 *
 *  Design goals:
 *   - Minimize mmap / munmap calls
 *   - O(1) chunk allocation and return
 *   - Cache-friendly linear memory
 *   - RB-tree compatibility for indexing pages
 *
 *  Memory layout:
 *
 *      mmap() -> [ chunk0 | chunk1 | ... | chunkN ]
 *
 *  Each chunk maps to:
 *      data + i * CHUNK_SIZE
 *
 *  Copyright:
 *      (c) 2025 QDeep.Net
 * ============================================================================
 */

#ifndef QDEEP_MATRIXP_PAGE_H
#define QDEEP_MATRIXP_PAGE_H

#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>

#include "mp_chunk.h"

#ifdef __cplusplus
extern "C" {

#endif


/* ============================================================================
 *  Configuration
 * ============================================================================
 */

/**
 * Number of chunks per page.
 *
 * PAGE_SIZE must:
 *   - Fit into uint16_t
 *   - Be small enough to keep metadata cache-hot
 */
#define PAGE_SIZE 1024


/* ============================================================================
 *  Page structure
 * ============================================================================
 */

/**
 * Page descriptor.
 *
 * A page is both:
 *   - A memory owner (mmap region)
 *   - A container of chunks
 *
 * And participates in:
 *   - A Red-Black tree (address-based indexing)
 *   - A doubly-linked page list (iteration / eviction)
 */
typedef struct mp_page {
    /* --------------------------------------------------------------------
     * Backing memory
     * ------------------------------------------------------------------ */

    /**
     * Pointer to raw chunk data storage.
     *
     * Layout:
     *   data + (i * CHUNK_SIZE)  ->  chunk[i]
     */
    mp_cdata data;

    /* --------------------------------------------------------------------
     * Chunk metadata
     * ------------------------------------------------------------------ */

    /** All chunks owned by this page */
    mp_chunk chunk[PAGE_SIZE];

    /**
     * Free-list linkage (intrusive circular list).
     *
     * - next[pos] / prev[pos] define the free ring
     * - page->free stores the head
     * - UINT16_MAX means "no free chunks"
     */
    uint16_t next[PAGE_SIZE];
    uint16_t prev[PAGE_SIZE];

    /**
     * Allocation state:
     *   fill = number of chunks ever handed out
     *   free = head of free-list (or UINT16_MAX)
     */
    uint16_t free;
    uint16_t fill;

    /* --------------------------------------------------------------------
     * RB-tree linkage (page index)
     * ------------------------------------------------------------------ */

    struct mp_page *sides[2]; /**< left / right children */
    uint8_t color; /**< RB-tree node color */

    /* --------------------------------------------------------------------
     * Doubly-linked page list
     * ------------------------------------------------------------------ */

    struct mp_page *nextp;
    struct mp_page *prevp;

    /* --------------------------------------------------------------------
     * Memory size bookkeeping
     * ------------------------------------------------------------------ */
} mp_page;

/* ============================================================================
 *  Page lifecycle
 * ============================================================================
 */

/**
 * Initialize a page and map its backing memory.
 *
 * Responsibilities:
 *   - mmap backing storage
 *   - Bind chunk->data pointers
 *   - Reset RB-tree and list links
 *   - Initialize allocation state
 *
 * Returns:
 *   EXIT_SUCCESS on success
 *   EXIT_FAILURE on mmap failure
 */
static __inline__ int32_t
mp_page_init(mp_page *page);


/**
 * Release page backing memory.
 *
 * Note:
 *   - Does NOT destroy page object itself
 *   - Caller must ensure no chunks are in use
 */
static __inline__ void
mp_page_free(const mp_page *page);


/* ============================================================================
 *  Allocation helpers
 * ============================================================================
 */

/**
 * Check whether a page is fully occupied.
 *
 * A page is considered full when:
 *   - All chunks have been issued at least once
 *   - No chunks are currently free
 */
static __inline__ int32_t
mp_page_full(const mp_page *page) {
    return (page->fill == PAGE_SIZE) && (page->free == UINT16_MAX);
}


/* ============================================================================
 *  Public chunk allocation API
 * ============================================================================
 */

/**
 * Allocate a chunk from the page.
 *
 * Strategy:
 *   1. Use never-issued chunks (linear growth)
 *   2. Reuse returned chunks from free-list
 *
 * Returns:
 *   Pointer to chunk or NULL if page exhausted
 */
static __inline__ mp_chunk *
mp_page_get_new(mp_page *page);


/**
 * Mark an already known chunk as allocated.
 *
 * Used when the position is known externally.
 */
static __inline__ void
mp_page_get(mp_page *page, const mp_chunk *chunk);


/**
 * Return a chunk back to the page.
 */
static __inline__ void
mp_page_ret(mp_page *page, const mp_chunk *chunk);


#ifdef __cplusplus
}
#endif

#endif /* QDEEP_MATRIXP_PAGE_H */
