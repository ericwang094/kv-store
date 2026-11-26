#include "kvstore.h"

static kvs_hash_t inst; // static variable, exist when system up, all 0

const char *kvs_engine_name(void) {
    return "hash";
}

int kvs_engine_init(mempool_t *m) {
    return kvs_hash_create(m, &inst, 1024);
}

void kvs_engine_destroy(void) {
    kvs_hash_destory(&inst);
}

int kvs_engine_set(char *k, char *v) {
    return kvs_hash_set(&inst, k, v);
}

char *kvs_engine_get(char *k) {
    return kvs_hash_get(&inst, k);
}

int kvs_engine_del(char *k) {
    return kvs_hash_del(&inst, k);
}

int kvs_engine_mod(char *k, char *v) {
    return kvs_hash_mod(&inst, k, v);
}

int kvs_engine_exist(char *k) {
    return kvs_hash_exist(&inst, k);
}

int kvs_engine_traversal(kvs_traversal_cb cb, void *ctx) {
    return kvs_hash_traversal(&inst, cb, ctx);
}
