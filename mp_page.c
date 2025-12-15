#include "mp_page.h"

/**
 * Required logical size for chunk storage (bytes).
 */
static constexpr uint64_t __NEED_SIZE =
        (uint64_t) PAGE_SIZE * CHUNK_BYTES;

/**
 * System page size (cached).
 */
static uint64_t __PAGE_SIZE = 0;

/**
 * Real mmap size, rounded up to system page boundary.
 */
static uint64_t __MMAP_SIZE = 0;

__inline__ int32_t
mp_page_init(mp_page *page) {
    /* Caching the sizes for mmap usage */
    if (!__PAGE_SIZE) __PAGE_SIZE = sysconf(_SC_PAGESIZE);
    if (!__MMAP_SIZE) __MMAP_SIZE = (__NEED_SIZE + __PAGE_SIZE - 1) & ~(__PAGE_SIZE - 1);

    /* Allocate aligned backing storage */
    page->data = (mp_cdata) mmap(
        NULL,
        __MMAP_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );

    if (page->data == MAP_FAILED)
        return EXIT_FAILURE;

    /* Initialize chunk descriptors */
    for (uint16_t i = 0; i < PAGE_SIZE; i++) {
        mp_chunk *chunk = page->chunk + i;
        mp_chunk_init(chunk);

        chunk->data = page->data + (uint64_t) i * CHUNK_SIZE;
    }

    /* Reset page links */
    page->nextp = NULL;
    page->prevp = NULL;

    /* Allocation state */
    page->free = UINT16_MAX;
    page->fill = 0;

    return EXIT_SUCCESS;
}


/**
 * Release page backing memory.
 *
 * Note:
 *   - Does NOT destroy page object itself
 *   - Caller must ensure no chunks are in use
 */
__inline__ void
mp_page_free(const mp_page *page) {
    munmap(page->data, __MMAP_SIZE);
}


/* ============================================================================
 *  Internal free-list manipulation
 * ============================================================================
 */

/**
 * Remove a position from the free-list.
 *
 * Preconditions:
 *   - pos is currently free
 */
static void
mp_page_get_pos(mp_page *page, const uint16_t pos) {
    uint16_t *__restrict next = page->next;
    uint16_t *__restrict prev = page->prev;

    /* Single-element list */
    if (next[pos] == pos) {
        page->free = UINT16_MAX;
        return;
    }

    prev[next[pos]] = prev[pos];
    next[prev[pos]] = next[pos];

    if (page->free == pos)
        page->free = next[pos];
}


/**
 * Insert a position into the free-list.
 */
static void
mp_page_ret_pos(mp_page *page, const uint16_t pos) {
    uint16_t *__restrict next = page->next;
    uint16_t *__restrict prev = page->prev;

    const uint16_t free = page->free;

    /* Empty free-list */
    if (free == UINT16_MAX) {
        page->free = pos;
        next[pos] = pos;
        prev[pos] = pos;
        return;
    }

    /* Insert before free head */
    const uint16_t tail = prev[free];

    next[pos] = free;
    prev[pos] = tail;

    next[tail] = pos;
    prev[free] = pos;
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
mp_chunk *
mp_page_get_new(mp_page *page) {
    const uint16_t pos = page->free;

    if (page->fill < PAGE_SIZE)
        return page->chunk + page->fill++;

    if (pos == UINT16_MAX)
        return NULL;

    mp_page_get_pos(page, pos);
    return page->chunk + pos;
}


/**
 * Mark an already known chunk as allocated.
 *
 * Used when the position is known externally.
 */
void
mp_page_get(mp_page *page, const mp_chunk *chunk) {
    const uint16_t pos = (uint16_t) (chunk - page->chunk);
    mp_page_get_pos(page, pos);
}


/**
 * Return a chunk back to the page.
 */
void
mp_page_ret(mp_page *page, const mp_chunk *chunk) {
    const uint16_t pos = (uint16_t) (chunk - page->chunk);
    mp_page_ret_pos(page, pos);
}