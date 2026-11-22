#include "kvstore.h"
#include "mem_pool/mem_pool.h"

// singleton

kvs_array_t global_array = {0};

int kvs_array_create(mempool_t *m, kvs_array_t *inst, int kvs_array_size) {
    if (!inst || !m || kvs_array_size <= 0) return -1;
    if (inst->table) {
        return -1;
    }

    inst->table = calloc(kvs_array_size, sizeof(kvs_array_item_t));
    if (!inst->table) {
        return -1;
    }

    inst->pool = m;
    inst->total = 0;
    inst->kvs_array_size = kvs_array_size;

    return 0;
}


void kvs_array_destory(kvs_array_t *inst) {
    if (!inst) {
        return;
    }

    if (inst->table) {
        kvs_free(inst->pool, inst->table);
    }
}

int kvs_array_set(kvs_array_t *inst, char *key, char *value) {
    if (inst == NULL || key == NULL || value == NULL) {
        return -1;
    }

    if (inst->total == inst->kvs_array_size) {
        return -1;
    }

    char *str = kvs_array_get(inst, key);
    if (str) {
        return 1;
    }

    char *kcopy = kvs_malloc(inst->pool, strlen(key) + 1);  //NOTE strlen 不会copy内存里的\0, strlen(“ABC”) = 3, 但是我们需要4
    if (kcopy == NULL) {
        return -2;
    }
    strcpy(kcopy, key); // 这个function会copy \0

    char *kvalue = kvs_malloc(inst->pool, strlen(value) + 1);
    if (kvalue == NULL) {
        return -2;
    }
    strcpy(kvalue, value);


    int i = 0;
    for (i = 0; i < inst->total; i++) {
        if (inst->table[i].key != NULL) {
            continue;
        }
        inst->table[i].key = kcopy;
        inst->table[i].value = kvalue;
        inst->total++;

        return 0;
    }

    if (i== inst->total && i < inst->kvs_array_size) {
        inst->table[i].key = kcopy;
        inst->table[i].value = kvalue;
        inst->total++;
    }


    return 0;

}

char * kvs_array_get(kvs_array_t *inst, char *key) {
    if (inst == NULL || key == NULL) {
        return NULL;
    }

    int i = 0;
    for (i = 0; i < inst->total; i++) {
        if (inst->table[i].key == NULL) {
            continue;
        }

        if (strcmp(inst->table[i].key, key) == 0) {
            return inst->table[i].value;
        }
    }

    return NULL;
}


// return < 0 error, == 0 success, > 0 not exist
int kvs_array_del(kvs_array_t *inst, char *key) {
    if (inst == NULL || key == NULL) {
        return -1;
    }

    int i = 0;
    for (i = 0; i < inst->total; i++) {
        if (strcmp(inst->table[i].key, key) != 0) {
            continue;
        }

        kvs_free(inst->pool, inst->table[i].key);
        inst->table[i].key = NULL;

        kvs_free(inst->pool, inst->table[i].value);
        inst->table[i].value = NULL;

        if (inst->total - 1 == i) {
            inst->total--;
        }

        return 0;
    }

    return i;
}

// return <0 error, =0 success, >0 no exist
int kvs_array_mod(kvs_array_t *inst, char *key, char *value) {
    if (inst == NULL || key == NULL || value == NULL) {
        return -1;
    }

    if (inst->total == 0) {
        return inst->kvs_array_size;
    }

    int i = 0;
    for (i = 0; i < inst->total; i++) {
        if (inst->table[i].key == NULL) {
            continue;
        }

        if (strcmp(inst->table[i].key, key) != 0) {
            continue;
        }
        kvs_free(inst->pool, inst->table[i].value);

        char *kvalue = kvs_malloc(inst->pool, strlen(value) + 1);
        if (kvalue == NULL) {
            return 2;
        }
        strcpy(kvalue, value);
        inst->table[i].value = kvalue;

        return 0;
    }

    return i;
}


int kvs_array_exists(kvs_array_t *inst, char *key) {
    if (!inst || !key) {
        return -1;
    }

    char *str = kvs_array_get(inst, key);
    if (!str) {
        return 1;
    }

    return 0;
}
