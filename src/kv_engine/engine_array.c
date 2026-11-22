#include "kvstore.h"

static kvs_array_t inst;

const char *kvs_engine_name(void) {
    return "array";
}

int kvs_engine_init(mempool_t *m) {
    int kvs_array_size = (int)(m->block_count / 2);
    return kvs_array_create(m, &inst, kvs_array_size);
}

void kvs_engine_destroy(void) {
    kvs_array_destory(&inst);
}

int kvs_engine_set(char *k, char *v) {
    return kvs_array_set(&inst, k, v);
}

char *kvs_engine_get(char *k) {
    return kvs_array_get(&inst, k);
}

int kvs_engine_del(char *k) {
    return kvs_array_del(&inst, k);
}

int kvs_engine_mod(char *k, char *v) {
    return kvs_array_mod(&inst, k, v);
}

int kvs_engine_exist(char *k) {
    return kvs_array_exists(&inst, k);
}

