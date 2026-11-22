#ifndef __MEM_POOL_H__
#define __MEM_POOL_H__

#include <stdlib.h>

typedef struct mempool_s {
    size_t mempool_size;    // total size of the pool (bytes)
    size_t block_size;  // size of each block
    size_t block_count; // number of blocks
    size_t free_count;  // number of blocks still available
    char *free_ptr;     // the next free slot
    char *mem;
} mempool_t;

int memp_create(mempool_t *m, size_t request_size, size_t block_size);
void memp_destroy(mempool_t *m);
int *memp_alloc(mempool_t *m, size_t size);
int memp_free(mempool_t *m, void *ptr);

#endif
