#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "kvstore.h"

/*
 * kvs_malloc / kvs_free live in kvstore.c, which also has main() and needs
 * io_uring, so it can't be linked into a unit test. These copies follow the
 * same logic: use the pool first, fall back to malloc.
 */
void *kvs_malloc(mempool_t *m, size_t size) {
    void *ptr = memp_alloc(m, size);
    return ptr ? ptr : malloc(size);
}

void kvs_free(mempool_t *m, void *ptr) {
    if (memp_free(m, ptr) == -2) { // not from the pool
        free(ptr);
    }
}

static mempool_t pool;
static kvs_hash_t h;

static void setup(void) {
    assert(memp_create(&pool, 64 * 1024, 32) == 0);
    memset(&h, 0, sizeof(h));
    assert(kvs_hash_create(&pool, &h, 1024) == 0);
}

static void teardown(void) {
    kvs_hash_destory(&h);
    memp_destroy(&pool);
}

static void test_create_empty(void) {
    setup();
    assert(h.max_slots == 1024);
    assert(kvs_hash_count(&h) == 0);
    for (int i = 0; i < h.max_slots; i++) {
        assert(h.nodes[i] == NULL); // calloc: every slot is an empty list
    }
    assert(kvs_hash_get(&h, "nothing") == NULL);
    teardown();
}

static void test_set_get(void) {
    setup();
    assert(kvs_hash_set(&h, "Teacher", "King") == 0);
    assert(strcmp(kvs_hash_get(&h, "Teacher"), "King") == 0);
    assert(kvs_hash_count(&h) == 1);
    teardown();
}

static void test_set_duplicate_key(void) {
    setup();
    assert(kvs_hash_set(&h, "Teacher", "King") == 0);
    assert(kvs_hash_set(&h, "Teacher", "Darren") == 1); // already exists
    assert(strcmp(kvs_hash_get(&h, "Teacher"), "King") == 0); // not overwritten
    assert(kvs_hash_count(&h) == 1);
    teardown();
}

static void test_anagrams_do_not_collide(void) {
    // the old "sum of chars" hash put these in the same slot
    setup();
    assert(kvs_hash_set(&h, "ab", "1") == 0);
    assert(kvs_hash_set(&h, "ba", "2") == 0);
    int slot_ab = -1, slot_ba = -1;
    for (int i = 0; i < h.max_slots; i++) {
        for (hashnode_t *n = h.nodes[i]; n; n = n->next) {
            if (strcmp(n->key, "ab") == 0) slot_ab = i;
            if (strcmp(n->key, "ba") == 0) slot_ba = i;
        }
    }
    assert(slot_ab >= 0 && slot_ba >= 0 && slot_ab != slot_ba);
    teardown();
}

static void test_real_collision(void) {
    // djb2: "ab" and "bA" hash to the same value: 33*'a'+'b' == 33*'b'+'A'
    setup();
    assert(kvs_hash_set(&h, "ab", "first") == 0);
    assert(kvs_hash_set(&h, "bA", "second") == 0);

    // both live in the same slot's list
    hashnode_t *head = h.nodes[0];
    for (int i = 0; i < h.max_slots && !head; i++) head = h.nodes[i];
    assert(head && head->next && head->next->next == NULL);

    // strcmp on the full key keeps them apart
    assert(strcmp(kvs_hash_get(&h, "ab"), "first") == 0);
    assert(strcmp(kvs_hash_get(&h, "bA"), "second") == 0);
    teardown();
}

static void test_mod(void) {
    setup();
    assert(kvs_hash_mod(&h, "Teacher", "X") == 1); // not exist
    assert(kvs_hash_set(&h, "Teacher", "King") == 0);
    assert(kvs_hash_mod(&h, "Teacher", "Darren") == 0);
    assert(strcmp(kvs_hash_get(&h, "Teacher"), "Darren") == 0);
    assert(kvs_hash_mod(&h, "Teacher", NULL) < 0);
    assert(kvs_hash_count(&h) == 1);
    teardown();
}

static void test_del_not_exist(void) {
    setup();
    assert(kvs_hash_del(&h, "nothing") == 1); // > 0: not exist, not an error
    assert(kvs_hash_set(&h, "Teacher", "King") == 0);
    assert(kvs_hash_del(&h, "Other") == 1);
    assert(kvs_hash_count(&h) == 1);
    teardown();
}

/*
 * Shrink max_slots to 1 so every key lands in nodes[0]. With head insertion,
 * after setting a, b, c the list is: c -> b -> a.
 */
static void setup_one_chain(void) {
    setup();
    h.max_slots = 1;
    assert(kvs_hash_set(&h, "a", "1") == 0);
    assert(kvs_hash_set(&h, "b", "2") == 0);
    assert(kvs_hash_set(&h, "c", "3") == 0);
    assert(strcmp(h.nodes[0]->key, "c") == 0);
}

static void check_chain(const char *k1, const char *k2) {
    hashnode_t *n = h.nodes[0];
    assert(n && strcmp(n->key, k1) == 0);
    assert(n->next && strcmp(n->next->key, k2) == 0);
    assert(n->next->next == NULL);
    assert(kvs_hash_count(&h) == 2);
}

static void test_del_head(void) {
    setup_one_chain();
    assert(kvs_hash_del(&h, "c") == 0);
    check_chain("b", "a");
    assert(kvs_hash_get(&h, "c") == NULL);
    teardown();
}

static void test_del_middle(void) {
    setup_one_chain();
    assert(kvs_hash_del(&h, "b") == 0);
    check_chain("c", "a");
    teardown();
}

static void test_del_tail(void) {
    setup_one_chain();
    assert(kvs_hash_del(&h, "a") == 0);
    check_chain("c", "b");
    teardown();
}

static void test_exist(void) {
    setup();
    assert(kvs_hash_exist(&h, "Teacher") == 1);
    assert(kvs_hash_set(&h, "Teacher", "King") == 0);
    assert(kvs_hash_exist(&h, "Teacher") == 0);
    teardown();
}

static void test_utf8_key(void) {
    // bytes >= 0x80: must not produce a negative slot index
    setup();
    assert(kvs_hash_set(&h, "老师", "King") == 0);
    assert(strcmp(kvs_hash_get(&h, "老师"), "King") == 0);
    teardown();
}

static void test_long_value_uses_malloc(void) {
    // block size is 32, so this value falls back to malloc.
    // ASan's leak check verifies del/destroy free it correctly.
    setup();
    const char *big = "this value is definitely longer than thirty-two bytes";
    assert(kvs_hash_set(&h, "k1", (char *)big) == 0);
    assert(kvs_hash_set(&h, "k2", (char *)big) == 0);
    assert(strcmp(kvs_hash_get(&h, "k1"), big) == 0);
    assert(kvs_hash_del(&h, "k1") == 0);
    teardown(); // k2 is freed by destroy
}

static void test_bad_args(void) {
    setup();
    assert(kvs_hash_set(NULL, "k", "v") < 0);
    assert(kvs_hash_set(&h, NULL, "v") < 0);
    assert(kvs_hash_set(&h, "k", NULL) < 0);
    assert(kvs_hash_get(&h, NULL) == NULL);
    assert(kvs_hash_del(&h, NULL) < 0);

    kvs_hash_t other = {0};
    assert(kvs_hash_create(NULL, &other, 16) < 0);   // no pool
    assert(kvs_hash_create(&pool, &other, 0) < 0);   // no buckets
    teardown();
}

/*
 * traversal
 *
 * collect_ctx_t records every (key, value) the callback sees, so a test can
 * check that each live key is visited exactly once with the right value.
 */
#define COLLECT_MAX 256

typedef struct {
    int  calls;
    char keys[COLLECT_MAX][16];
    char values[COLLECT_MAX][16];
} collect_ctx_t;

static int collect_cb(const char *key, const char *value, void *ctx) {
    collect_ctx_t *c = ctx;
    assert(c->calls < COLLECT_MAX);
    snprintf(c->keys[c->calls], sizeof(c->keys[0]), "%s", key);
    snprintf(c->values[c->calls], sizeof(c->values[0]), "%s", value);
    c->calls++;
    return 0;
}

// how many times key was visited; *value is set to what the callback saw
static int seen(collect_ctx_t *c, const char *key, const char **value) {
    int n = 0;
    for (int i = 0; i < c->calls; i++) {
        if (strcmp(c->keys[i], key) == 0) {
            n++;
            if (value) *value = c->values[i];
        }
    }
    return n;
}

static void test_traversal_empty(void) {
    setup();
    collect_ctx_t c = {0};
    assert(kvs_hash_traversal(&h, collect_cb, &c) == 0);
    assert(c.calls == 0); // callback never called on an empty table
    teardown();
}

static void test_traversal_visits_every_key_once(void) {
    setup();
    char key[16], value[16];
    for (int i = 0; i < 100; i++) {
        sprintf(key, "k%d", i);
        sprintf(value, "v%d", i);
        assert(kvs_hash_set(&h, key, value) == 0);
    }
    assert(kvs_hash_del(&h, "k0") == 0);
    assert(kvs_hash_del(&h, "k50") == 0);
    assert(kvs_hash_mod(&h, "k7", "changed") == 0);

    collect_ctx_t c = {0};
    assert(kvs_hash_traversal(&h, collect_cb, &c) == 0);
    assert(c.calls == kvs_hash_count(&h));
    assert(c.calls == 98);

    for (int i = 0; i < 100; i++) {
        sprintf(key, "k%d", i);
        const char *got = NULL;
        if (i == 0 || i == 50) {
            assert(seen(&c, key, NULL) == 0); // deleted keys are not visited
            continue;
        }
        assert(seen(&c, key, &got) == 1);     // exactly once
        if (i == 7) {
            assert(strcmp(got, "changed") == 0); // sees the value after mod
        } else {
            sprintf(value, "v%d", i);
            assert(strcmp(got, value) == 0);
        }
    }
    teardown();
}

static void test_traversal_walks_whole_chain(void) {
    // all keys in nodes[0]: c -> b -> a. The inner loop must follow ->next.
    setup_one_chain();
    collect_ctx_t c = {0};
    assert(kvs_hash_traversal(&h, collect_cb, &c) == 0);
    assert(c.calls == 3);
    assert(strcmp(c.keys[0], "c") == 0);
    assert(strcmp(c.keys[1], "b") == 0);
    assert(strcmp(c.keys[2], "a") == 0);
    teardown();
}

static int stop_at_second_cb(const char *key, const char *value, void *ctx) {
    (void)key;
    (void)value;
    int *calls = ctx;
    (*calls)++;
    return *calls == 2 ? 99 : 0;
}

static void test_traversal_early_stop(void) {
    setup_one_chain();
    int calls = 0;
    // the callback's non-zero return value comes back to the caller unchanged
    assert(kvs_hash_traversal(&h, stop_at_second_cb, &calls) == 99);
    assert(calls == 2); // the 3rd node is never visited
    teardown();
}

static int null_ctx_cb(const char *key, const char *value, void *ctx) {
    (void)key;
    (void)value;
    assert(ctx == NULL);
    return 0;
}

static void test_traversal_null_ctx(void) {
    // ctx belongs to the caller; NULL is a valid choice
    setup();
    assert(kvs_hash_set(&h, "k", "v") == 0);
    assert(kvs_hash_traversal(&h, null_ctx_cb, NULL) == 0);
    teardown();
}

static void test_traversal_bad_args(void) {
    setup();
    collect_ctx_t c = {0};
    assert(kvs_hash_traversal(NULL, collect_cb, &c) < 0);
    assert(kvs_hash_traversal(&h, NULL, &c) < 0);
    teardown();

    // after destroy, nodes is NULL
    assert(kvs_hash_traversal(&h, collect_cb, &c) < 0);
    assert(c.calls == 0);
}

int main(void) {
    test_create_empty();
    test_set_get();
    test_set_duplicate_key();
    test_anagrams_do_not_collide();
    test_real_collision();
    test_mod();
    test_del_not_exist();
    test_del_head();
    test_del_middle();
    test_del_tail();
    test_exist();
    test_utf8_key();
    test_long_value_uses_malloc();
    test_bad_args();
    test_traversal_empty();
    test_traversal_visits_every_key_once();
    test_traversal_walks_whole_chain();
    test_traversal_early_stop();
    test_traversal_null_ctx();
    test_traversal_bad_args();
    printf("all hash tests passed\n");
    return 0;
}
