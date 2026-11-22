#include <assert.h>
#include <stdio.h>
#include "../src/mem_pool/mem_pool.h"

static void test_sequential(void) {
    mempool_t m;
    assert(memp_create(&m, 4096, 32) == 0);
    char *p1 = (char *)memp_alloc(&m, 16);
    char *p2 = (char *)memp_alloc(&m, 16);
    char *p3 = (char *)memp_alloc(&m, 16);

    assert(p1 && p2 && p2);
    assert(p2 - p1 == 32 && p3 - p2 == 32);
    memp_destroy(&m);

}

// if a pointer is freed, and then recreated,
// they should be the same pointer
static void test_free_then_alloc(void) {
    mempool_t m;
    assert(memp_create(&m, 4096, 32) == 0);
    void *p1 = memp_alloc(&m, 16);
    assert(memp_free(&m, p1) == 0);
    assert(memp_alloc(&m, 16) == p1);
    memp_destroy(&m);
}


static void test_exhaust(void) {
    mempool_t m;
    assert(memp_create(&m, 4096, 32) == 0);
    for (int i = 0; i < (4096/32); i++) {
        assert(memp_alloc(&m, 32) != NULL);
    }
    memp_destroy(&m);
}

static void test_too_big(void) {
    mempool_t m;
    assert(memp_create(&m, 4096, 32) == 0);
    assert(memp_alloc(&m, 33) == NULL);  // assigned memory is larger than block
    memp_destroy(&m);
}

static void test_invalid_free(void) {
    mempool_t m;
    int outside;
    assert(memp_create(&m, 4096, 32) == 0);
    assert(memp_free(&m, NULL) == -1);
    assert(memp_free(&m, &outside) == -2);
    memp_destroy(&m);
}

static void test_create_args(void) {
    mempool_t m;
    assert(memp_create(NULL, 4096, 32) != 0);
    assert(memp_create(&m, 4096, 4) != 0);     // block can't hold a pointer
    assert(memp_create(&m, 16, 32) != 0);      // pool smaller than one block
    assert(memp_create(&m, 100, 32) == 0);     // not evenly divisible
    assert(m.block_count == 3);
    memp_destroy(&m);
}

int main(void) {
    test_sequential();
    test_free_then_alloc();
    test_exhaust();
    test_too_big();
    test_invalid_free();
    test_create_args();
    printf("all mem_pool tests passed\n");
    return 0;
}

