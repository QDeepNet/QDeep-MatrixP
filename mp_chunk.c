#include "mp_chunk.h"


/**
 * Read an entire chunk from file descriptor into chunk->data.
 * Chunks size must be set before this function
 *
 * Returns:
 *   0  on success
 *  -1  on EOF or unrecoverable error
 */
int32_t
mp_chunk_recv(const mp_chunk *chunk, const int32_t fd) {
    uint8_t *ptr = (uint8_t *) chunk->data;

    const uint16_t size_x = chunk->size.dim.x + 1;
    const uint16_t size_y = chunk->size.dim.y + 1;
    constexpr uint64_t size_d = sizeof(int64_t);

    for (uint16_t _y = 0; _y <= size_y; _y++) {
        uint64_t rem = size_x * size_d;

        while (rem > 0) {
            const int64_t ret = read(fd, ptr, rem);

            /* Expected: positive bytes read. ret <= 0 is unlikely. */
            if (__builtin_expect(ret <= 0, 0)) {
                if (errno == EINTR) continue; /* retry on interrupt */
                return -1; /* EOF or real error */
            }

            ptr += ret;
            rem -= (uint64_t) ret;
        }

        // Aligning to the next raw of the data in chunk
        ptr += (CHUNK_W - size_x) * size_d;
    }

    return 0;
}


/**
 * Write entire chunk->data to file descriptor.
 * Chunks size must be set before this function
 *
 * Returns:
 *   0  on success
 *  -1  on error
 */
int32_t
mp_chunk_send(const mp_chunk *chunk, const int32_t fd) {
    const uint8_t *ptr = (const uint8_t *) chunk->data;

    const uint16_t size_x = chunk->size.dim.x + 1;
    const uint16_t size_y = chunk->size.dim.y + 1;
    constexpr uint64_t size_d = sizeof(int64_t);

    for (uint16_t _y = 0; _y <= size_y; _y++) {
        uint64_t rem = size_x * size_d;

        while (rem > 0) {
            const int64_t ret = write(fd, ptr, rem);

            /* Expected: positive bytes read. ret <= 0 is unlikely. */
            if (__builtin_expect(ret <= 0, 0)) {
                if (errno == EINTR) continue; /* retry on interrupt */
                return -1; /* EOF or real error */
            }

            ptr += ret;
            rem -= (uint64_t) ret;
        }

        // Aligning to the next raw of the data in chunk
        ptr += (CHUNK_W - size_x) * size_d;
    }

    return 0;
}