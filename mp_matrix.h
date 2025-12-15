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

    mp_coffs offset;     /**< Last accessed offset */
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
 *  Tree initialization
 * ============================================================================
 */

/**
 * Initialize an empty tree.
 */
static __inline__ void
mp_tree_init(mp_tree *tree) {
    tree->root = NULL;
    tree->find = NULL;
    tree->offset.pos = UINT64_MAX;
}

/**
 * Free all nodes in the tree and return there chunks into pool.
 */
static __inline__ void
mp_tree_free(mp_tree *tree, mp_pool *pool) {
    mp_chunk *node = tree->root;
    int32_t pos = -1;
    while (1) {
        while (node) node = (tree->stack[++pos] = node)->sides[0];
        if (pos == -1) break;

        node = tree->stack[pos--];

        mp_chunk *next = node->sides[1];
        mp_pool_ret(pool, node);

        node = next;
    }
}

/* ============================================================================
 *  RB-tree insertion / removal optimization
 * ============================================================================
 *
 * These functions rebalance the RB-tree after insertion or removal,
 * maintaining standard RB-tree invariants.
 */

/**
 * Rebalance tree after insertion.
 */
static __inline__ void
rb_tree_insert_optimize(mp_tree *tree) {
    while (--tree->pos >= 0) {
        const uint8_t side = tree->sides[tree->pos];
        mp_chunk *g_ = tree->stack[tree->pos];       // Grandparent
        mp_chunk *y_ = g_->sides[side ^ 1];          // Uncle
        mp_chunk *x_ = tree->stack[tree->pos + 1];  // Parent

        if (x_->color == MP_BLACK) break;

        if (y_ && y_->color == MP_RED) {
            x_->color = MP_BLACK;
            y_->color = MP_BLACK;
            g_->color = MP_RED;
            --tree->pos;
            continue;
        }

        if (side == 1 - tree->sides[tree->pos + 1]) {
            y_ = x_->sides[side ^ 1];
            x_->sides[side ^ 1] = y_->sides[side];
            y_->sides[side] = x_;
            x_ = g_->sides[side] = y_;
        }

        g_->color = MP_RED;
        x_->color = MP_BLACK;
        g_->sides[side] = x_->sides[side ^ 1];
        x_->sides[side ^ 1] = g_;

        if (tree->pos == 0) tree->root = x_;
        else tree->stack[tree->pos - 1]->sides[tree->sides[tree->pos - 1]] = x_;
        break;
    }

    if (tree->root) tree->root->color = MP_BLACK;
}

/**
 * Rebalance tree after removal.
 */
static __inline__ void
rb_tree_remove_optimize(mp_tree *tree) {
    while (tree->pos >= 0) {
        const uint8_t side = tree->sides[tree->pos];
        mp_chunk *p = tree->stack[tree->pos];       // Parent
        mp_chunk *s = p->sides[side ^ 1];           // Sibling

        if (p->sides[side] && p->sides[side]->color == MP_RED) {
            p->sides[side]->color = MP_BLACK;
            break;
        }

        if (s && s->color == MP_RED) {
            s->color = MP_BLACK;
            p->color = MP_RED;

            if (tree->pos == 0) tree->root = s;
            else tree->stack[tree->pos - 1]->sides[tree->sides[tree->pos - 1]] = s;

            p->sides[side ^ 1] = s->sides[side];
            s->sides[side] = p;

            tree->stack[tree->pos] = s;
            tree->sides[++tree->pos] = side;
            tree->stack[tree->pos] = p;

            s = p->sides[side ^ 1];
        }

        if (!s) break;

        if ((s->sides[0] == NULL || s->sides[0]->color == MP_BLACK) &&
            (s->sides[1] == NULL || s->sides[1]->color == MP_BLACK)) {
            s->color = MP_RED;
            --tree->pos;
            continue;
        }

        if (s->sides[side ^ 1] == NULL || s->sides[side ^ 1]->color == MP_BLACK) {
            mp_chunk *y = s->sides[side];
            y->color = MP_BLACK;
            s->color = MP_RED;

            s->sides[side] = y->sides[side ^ 1];
            y->sides[side ^ 1] = s;

            s = p->sides[side ^ 1] = y;
        }

        s->color = p->color;
        p->color = MP_BLACK;

        if (s->sides[side ^ 1]) s->sides[side ^ 1]->color = MP_BLACK;

        if (tree->pos == 0) tree->root = s;
        else tree->stack[tree->pos - 1]->sides[tree->sides[tree->pos - 1]] = s;

        p->sides[side ^ 1] = s->sides[side];
        s->sides[side] = p;
        break;
    }
}


/* ============================================================================
 *  RB-tree find / insert / remove
 * ============================================================================
 */

/**
 * Find chunk in tree by offset.
 *
 * Uses cache in tree->find to speed repeated lookups.
 */
static __inline__ mp_chunk *
rb_tree_find(mp_tree *tree, const mp_coffs offset) {
    if (tree->find && mp_coffs_cmp(tree->offset, offset) == 0) return tree->find;

    mp_chunk *node = tree->root;
    tree->pos = -1;
    tree->offset = offset;

    while (node) {
        if (mp_coffs_cmp(node->offset, offset) == 0) return tree->find = node;
        tree->stack[++tree->pos] = node;
        node = node->sides[tree->sides[tree->pos] = mp_coffs_cmp(node->offset, offset) < 0];
    }

    return tree->find = NULL;
}

/**
 * Insert chunk into tree.
 */
static __inline__ void
rb_tree_insert(mp_tree *tree, mp_chunk *chunk) {
    mp_chunk *node = tree->find;
    
    if (!node || mp_coffs_cmp(node->offset, chunk->offset) != 0)
        node = rb_tree_find(tree, chunk->offset);

    if (node) return;

    /* Insert As Red Colored Node */
    tree->offset.pos = UINT64_MAX;
    chunk->color = MP_RED;
    chunk->sides[0] = NULL;
    chunk->sides[1] = NULL;

    if (tree->pos == -1) tree->root = chunk;
    else tree->stack[tree->pos]->sides[tree->sides[tree->pos]] = chunk;

    rb_tree_insert_optimize(tree);
}

/**
 * Remove chunk from tree.
 */
static __inline__ void
rb_tree_remove(mp_tree *tree, const mp_chunk *chunk) {
    if (!chunk) return;

    mp_chunk *node = tree->find;
    if (!node || mp_coffs_cmp(node->offset, chunk->offset) != 0)
        node = rb_tree_find(tree, chunk->offset);

    if (!node) return;

    tree->offset.pos = UINT64_MAX;

    uint32_t side = tree->sides[tree->pos];

    /* Node with two children */
    if (node->sides[0] && node->sides[1]) {
        mp_chunk *target = node->sides[0];
        const int32_t pos = tree->pos;

        tree->stack[++tree->pos] = node;
        tree->sides[tree->pos] = 0;

        while (target->sides[1]) {
            tree->stack[++tree->pos] = target;
            tree->sides[tree->pos] = 1;
            target = target->sides[1];
        }

        if (pos == -1) tree->root = target;
        else tree->stack[pos]->sides[tree->sides[pos]] = target;

        tree->stack[pos + 1] = target;

        const uint8_t color = target->color;
        target->color = node->color;
        node->color = color;

        target->sides[1] = node->sides[1];
        node->sides[1] = NULL;

        mp_chunk *tmp = node->sides[0];
        node->sides[0] = target->sides[0];
        target->sides[0] = tmp;
    }

    mp_chunk *child = node->sides[0] ? node->sides[0] : node->sides[1];
    if (tree->pos == -1) tree->root = child;
    else tree->stack[tree->pos]->sides[side] = child;

    if (node->color == MP_BLACK)
        rb_tree_remove_optimize(tree);
}


/* ============================================================================
 *  Matrix initialization
 * ============================================================================
 */

/**
 * Initialize an empty matrix.
 */
static __inline__ void
mp_matrix_init(mp_matrix *matx, mp_pool *pool) {
    mp_tree_init(&matx->tree);
    matx->pool = pool;
    matx->fd = -1;
}


/**
 * Free the data taken y thi s matrix
 */
static __inline__ void
mp_matrix_free(mp_matrix *matx) {
    mp_tree_free(&matx->tree, matx->pool);
}

static __inline__ int32_t
mp_matrix_set_size(mp_matrix *matx, const mp_msize size) {
    if (matx->fd == -1) return -1;
    constexpr uint64_t _header = sizeof(mp_msize);
    const uint64_t _size = size.x * size.y * sizeof(int64_t);

    const int32_t res = ftruncate(matx->fd, 0) == -1 || ftruncate(matx->fd, _header + _size) == -1 ? -1 : 0;
    matx->size = res == 0? size: (mp_msize){0, 0};

    return res;
}

static __inline__ void
mp_matrix_set_file(mp_matrix *matx, const char *filename) {
    matx->fd = open(filename, O_RDWR | O_CREAT);
}


static __inline__ void
mp_matrix_recv(mp_matrix *matx, int32_t fd) {

}

static __inline__ void
mp_matrix_send(mp_matrix *matx, int32_t fd) {

}


#ifdef __cplusplus
}
#endif

#endif /* QDEEP_MATRIXP_MATRIX_H */
