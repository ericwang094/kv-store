#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_pool.h"


// request_size: total pool size, ideally, 2^i, e.g. 4096 or 1 << 20 (1MB) but user can do anything
// use 2^n as block_size value, such 16, 32, 64, 128
int memp_create(mempool_t *m, size_t request_size, size_t block_size) {
    if (!m) {
        return -1;
    }

    // a block must be able to hold a next pointer (8bytes); the pool must hold at least one block
    if (block_size < sizeof(char *) || request_size < block_size) {
        return -2;
    }

    m->block_size = block_size;
    m->free_count = request_size / block_size;    // if not evenly divisible, the leftover tail bytes are unused
    m->block_count = m->free_count;
    m->mempool_size   = m->free_count * block_size;

    m->mem = (char *)malloc(m->mempool_size);
    if (!m->mem) {
        return -3;
    }

    memset(m->mem, 0, m->mempool_size);

    m->free_ptr = m->mem;

    size_t i = 0;
    char *ptr = m->mem;
    for (i = 0; i < m->block_count - 1; i++) {  // NOTE: minus one so the last iteration does not write past the end of the pool
        /*
        * NOTE:
        * char *ptr   points to the start address of a block
        * (char **)ptr reinterprets this address as "a pointer to a char * variable", i.e. the first
        *              8 bytes of the block (on 64-bit systems) are used as a pointer variable.
        * *(char **)ptr = ...  writes an address into those 8 bytes: the address of the next block,
        *              ptr + block_size.
        */
        *(char **)ptr = ptr + block_size;  // NOTE: the first bytes of the block point to the next node; *(char **) acts like the first field of a struct
        ptr = ptr + block_size;
    }

    *(char **)ptr = NULL;  // the last block points to NULL: no more blocks to allocate

    return 0;
}

void memp_destroy(mempool_t *m) {
    if (!m) {
        return;
    }

    free(m->mem);

}

int *memp_alloc(mempool_t *m, size_t size) {
    if (!m || size > m->block_size || m->free_count == 0) {
        return NULL;
    }

    void *ptr = m->free_ptr;

    m->free_ptr = *(char **)ptr;
    m->free_count--;

    return ptr;
}

int memp_free(mempool_t *m, void *ptr) {
 
    if (!m || ptr == NULL) {

        return -1;
    }

    char *p = (char *)ptr;
    if (p < m->mem || p >= m->mem + m->mempool_size) {
        // free(ptr);
        return -2; // not from this pool
    }

    *(char **)ptr = m->free_ptr;  // NOTE: insert at head (LIFO)

    m->free_ptr = (char *)ptr;
    m->free_count++;

    return 0;
}

