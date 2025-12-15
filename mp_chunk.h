/**
 * ============================================================================
 *  Project:      QDeep / MatrixP
 *  File:         mp_chunk.h
 *  Description:  Chunk abstraction for tiled matrix storage and processing.
 *
 *  This header defines:
 *   - Chunk dimensional constants
 *   - Compact coordinate and size representations
 *   - Chunk metadata used for spatial indexing (RB-tree compatible)
 *
 *  Design goals:
 *   - Cache-friendly fixed-size chunks
 *   - Fast bitwise coordinate computation
 *   - Minimal memory overhead
 *   - GPU / CPU friendly layout
 *
 *  Notes:
 *   - A single chunk represents a square matrix block
 *   - Maximum chunk dimension is 256 × 256
 *   - Chunk data is stored as a contiguous int64_t buffer
 *
 *  Copyright:
 *      (c) 2025 QDeep.Net
 * ============================================================================
 */

#ifndef QDEEP_MATRIXP_CHUNK_H
#define QDEEP_MATRIXP_CHUNK_H

#include <errno.h>
#include <stdint.h>
#include <unistd.h>    // read(), write(), close()
#include <fcntl.h>     // fcntl(), open() flags
#include <errno.h>     // errno, EINTR
#include <stdint.h>    // int64_t
#include <stdlib.h>    // malloc(), free()
#include <sys/types.h> // ssize_t

#ifdef __cplusplus
extern "C" {



#endif

/* ============================================================================
 *  Chunk configuration constants
 * ============================================================================
 */

/**
 * Red-Black tree node colors.
 *
 * These values are used by both:
 *   - mp_chunk
 *   - mp_page
 *   -
 *
 * Invariants:
 *   - Root node must always be MP_BLACK
 *   - Red nodes may not have red children
 *   - Every path from a node to leaf contains
 *     the same number of black nodes
 */
#define MP_BLACK  0
#define MP_RED    1

/**
 * Power-of-two exponent for chunk dimensions.
 *
 * CHUNK_POW = 8  →  2^8 = 256
 * This ensures fast addressing using bit shifts.
 */
#define CHUNK_POW 8

/** Width of a chunk (elements) */
#define CHUNK_W (1 << CHUNK_POW)   /* 256 */

/** Height of a chunk (elements) */
#define CHUNK_H (1 << CHUNK_POW)   /* 256 */

/**
 * Total number of elements in a chunk.
 *
 * 256 × 256 = 65,536
 */
#define CHUNK_SIZE (1 << (CHUNK_POW + CHUNK_POW)) /* 65536 */

/**
 * Convert 2D chunk-local coordinates to linear index.
 *
 * Layout:
 *   - Row-major
 *   - Uses bit shifting instead of multiplication
 *
 * Preconditions:
 *   - 0 ≤ x < 256
 *   - 0 ≤ y < 256
 */
#define CHUNK_POS(x, y) (((y) << CHUNK_POW) | (x))

#define CHUNK_BYTES  (CHUNK_SIZE * sizeof(int64_t))


/* ============================================================================
 *  Core type definitions
 * ============================================================================
 */

/**
 * Pointer to chunk numerical data.
 *
 * Notes:
 *   - Points to CHUNK_SIZE elements
 *   - Each element is int64_t
 *   - Ownership rules are defined by allocator / memory pool
 */
typedef int64_t *mp_cdata;


/* ============================================================================
 *  Chunk size representation
 * ============================================================================
 */

/**
 * Compact chunk size descriptor.
 *
 * Union rationale:
 *   - Allows treating size as a single 16-bit value
 *   - Or as separate x/y dimensions
 *
 * Important:
 *   - x and y are stored as inverted values
 *     (real_size = 256 - stored_value)
 *   - This allows compact encoding of sizes: 1 < size <= 256
 */
typedef union mp_csize {
    uint16_t size;

    struct {
        uint8_t x; /**< Encoded width  (real = = x + 1) */
        uint8_t y; /**< Encoded height (real = y + 1) */
    } dim;
} mp_csize;

/**
 * This function is to calculate the real size of the chunk
 *
 * @return real size of the chunk
 */
static __inline__ uint32_t
mp_csize_real(const mp_csize size) {
    return (size.dim.x + 1) * (size.dim.y + 1);
}


/* ============================================================================
 *  Chunk offset (global position)
 * ============================================================================
 */

/**
 * Global chunk offset.
 *
 * This union enables:
 *   - Fast 64-bit comparisons
 *   - Direct access to X/Y coordinates
 *
 * Typical use:
 *   - Spatial indexing
 *   - RB-tree ordering
 *   - Hashing or sorting
 */
typedef union mp_coffs {
    uint64_t pos;

    struct {
        uint32_t x; /**< Chunk X coordinate (global space) */
        uint32_t y; /**< Chunk Y coordinate (global space) */
    } dim;
} mp_coffs;


/**
 * Compare two chunk offsets.
 *
 * Returns:
 *   -1 if a < b
 *    0 if a == b
 *   +1 if a > b
 *
 * Ordering is lexicographical over the 64-bit packed value.
 */
static __inline__ int32_t
mp_coffs_cmp(const mp_coffs a, const mp_coffs b) {
    return (a.pos > b.pos) - (a.pos < b.pos);
}


/* ============================================================================
 *  Chunk structure
 * ============================================================================
 */

/**
 * Matrix chunk descriptor.
 *
 * This structure is intentionally compatible with
 * Red-Black tree node layouts.
 *
 * Layout overview:
 *   - Tree navigation (left / right)
 *   - Node color (RB-tree)
 *   - Data pointer
 *   - Chunk dimensions
 *   - Global offset
 */
typedef struct mp_chunk {
    /* --------------------------------------------------------------------
     * Tree linkage (Red-Black tree)
     * ------------------------------------------------------------------ */

    struct mp_chunk *sides[2]; /**< sides[0] = left, sides[1] = right */
    uint8_t color; /**< RB-tree node color */

    /* --------------------------------------------------------------------
     * Chunk payload
     * ------------------------------------------------------------------ */

    mp_cdata data; /**< Pointer to chunk data buffer */
    mp_csize size; /**< Effective chunk dimensions */
    mp_coffs offset; /**< Global chunk offset */
} mp_chunk;

/**
 * Initialize MP chunk structure.
 *
 * Resets color, tree links, data pointer, and metadata fields.
 */
static __inline__ void
mp_chunk_init(mp_chunk *chunk) {
    chunk->data = NULL; /* no attached memory yet */
    chunk->size.size = 0; /* chunk data size (bytes/elements) */
    chunk->offset.pos = 0; /* logical offset of this chunk */
}

/**
 * Initialize MP chunk size.
 */
static __inline__ void
mp_chunk_set_size(mp_chunk *chunk, const mp_csize size) {
    chunk->size = size;
}

/**
 * Read an entire chunk from file descriptor into chunk->data.
 * Chunks size must be set before this function
 *
 * Returns:
 *   0  on success
 *  -1  on EOF or unrecoverable error
 */
static __inline__ int32_t
mp_chunk_read(const mp_chunk *chunk, int32_t fd);


/**
 * Write entire chunk->data to file descriptor.
 * Chunks size must be set before this function
 *
 * Returns:
 *   0  on success
 *  -1  on error
 */
static __inline__ int32_t
mp_chunk_write(const mp_chunk *chunk, int32_t fd);


#ifdef __cplusplus
}
#endif

#endif /* QDEEP_MATRIXP_CHUNK_H */
