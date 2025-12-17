/**
 * ============================================================================
 *  Project:      QDeep / MatrixP
 *  File:         mp_matrix.h
 *  Description:  Matrix structure and chunk-based RB-tree management.
 *
 *  Responsibilities:
 *    - Maintain chunks in a Red-Black tree for fast lookup
 *    - Store matrix size and optional file descriptor
 *    - Provide tree insert/remove/find operations
 *
 *  Notes:
 *    - mp_chunk nodes hold the actual data
 *    - Offsets (mp_coffs) are used for fast tree comparisons
 *    - Tree operations maintain standard RB-tree invariants:
 *        • Root is always black
 *        • No red node has red children
 *        • All paths have equal black node counts
 *
 *  Copyright:
 *      (c) 2025 QDeep.Net
 * ============================================================================
 */

#ifndef QDEEP_MATRIXP_MATRIX_H
#define QDEEP_MATRIXP_MATRIX_H


#include <stdio.h>

#include "mp_chunk.h"
#include "mp_pool.h"

#ifdef __cplusplus
extern "C" {
#endif


/* ============================================================================
 *  Tree and matrix structures
 * ============================================================================
 */

/**
 * RB-tree state for chunk management.
 */
typedef struct mp_tree {
    mp_chunk *root;      /**< Root of RB-tree */
    mp_chunk *find;      /**< Cache for last found node */

    mp_copos offset;     /**< Last accessed offset */
    int32_t pos;          /**< Depth index for stack during insert/remove */

    mp_chunk *stack[32]; /**< Ancestor nodes during traversal */
    uint8_t   sides[32]; /**< Side taken at each level (0=left, 1=right) */
} mp_tree;

/**
 * Matrix size descriptor.
 */
typedef struct {
    uint64_t x; /**< Number of columns */
    uint64_t y; /**< Number of rows */
} mp_msize;

/**
 * Matrix structure.
 *
 * Contains:
 *   - RB-tree of chunks
 *   - Optional file descriptor (fd)
 *   - Matrix size
 */
typedef struct mp_matrix {
    mp_pool *pool;
    mp_tree  tree;

    mp_msize size;
    int32_t fd;
} mp_matrix;

/* ============================================================================
 *  Matrix initialization
 * ============================================================================
 */

/**
 * Initialize an empty matrix.
 */
static __inline__ void
mp_matrix_init(mp_matrix *matx, mp_pool *pool);


/**
 * Free the data taken y thi s matrix
 */
static __inline__ void
mp_matrix_free(mp_matrix *matx);

/**
 * @brief Set the matrix size and resize the underlying file.
 *
 * Stores the matrix dimensions in the file header and resizes the file
 * to accommodate the matrix data.
 *
 * @param matx Pointer to the matrix object.
 * @param size Matrix dimensions (width × height).
 *
 * @return 0  On success.
 * @return -1 On error (invalid file descriptor or system call failure).
 */
static __inline__ int32_t
mp_matrix_set_size(mp_matrix *matx, mp_msize size);

/**
 * @brief Open a file for the matrix and read its header if it exists.
 *
 * Opens the specified file in read/write mode, creating it if necessary.
 * If the file already contains a matrix header, reads the matrix size
 * into the matrix structure.
 *
 * @param matx    Pointer to the matrix object.
 * @param filename Path to the file to open.
 *
 * @return 0  On success.
 * @return -1 On error (invalid parameters or file open failure).
 */
static __inline__ int32_t
mp_matrix_set_file(mp_matrix *matx, const char *filename);


static __inline__ void
mp_matrix_recv(mp_matrix *matx, int32_t fd);

static __inline__ void
mp_matrix_send(mp_matrix *matx, int32_t fd);


#ifdef __cplusplus
}
#endif

#endif /* QDEEP_MATRIXP_MATRIX_H */
