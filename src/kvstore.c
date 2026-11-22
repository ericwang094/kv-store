#include "kvstore.h"
#include "mem_pool/mem_pool.h"

static mempool_t g_pool;
extern kvs_array_t global_array;
// extern kvs_rbtree_t global_rbtree;
// extern kvs_hash_t global_hash;

void *kvs_malloc(mempool_t *m, size_t size) {
    void *ptr = memp_alloc(m, size);
    if (ptr == NULL) {
        printf("Failed to allocate a memory from memp.\n");
        ptr = malloc(size);
    }
    return ptr;
}

void kvs_free(mempool_t *m, void *ptr) {
    int ret = memp_free(m, ptr); 
    if (ret == -2) {
        // ret == -2 mean this ptr is not from the mempool, we need to clean it anyway
        free(ptr);
    }
}

const char *command[] = {
    "SET",
    "GET",
    "DEL",
    "MOD",
    "EXIST",
    "RSET",
    "RGET",
    "RDEL",
    "RMOD",
    "REXIST",
    "HSET",
    "HDEL",
    "HMOD",
    "HEXIST"
};


enum {
    KVS_CMD_START = 0,
    // arrary
    KVS_CMD_SET = KVS_CMD_START,
    KVS_CMD_GET,
    KVS_CMD_DEL,
    KVS_CMD_MOD,
    KVS_CMD_EXIST,

    KVS_CMD_COUNT,
};

enum {
    KV_ENGINE_ARRAY = 0,
    KV_ENGINE_RBTREE,
    KV_ENGINE_HASH,
    KV_ENGINE_SKIP
};

const char * response[] = {};

int kvs_split_token(char *msg, char *tokens[]) {
    if (msg == NULL || tokens == NULL) {
        return -1;
    }

    int idx = 0;

    // 1. First call passes the string
    char *token = strtok(msg, " ");

    // 2. Loop until tokens remain
    while(token != NULL) {
        tokens[idx++] = token;
        token = strtok(NULL, " ");
    }

    return idx;
}

// SET key value
// tokens[0]: SET
// tokens[1]: Key
// tokens[2]: Value
int kvs_filter_protocol(char **tokens, int count, char *response) {
    if (tokens[0] == NULL || count == 0 || response == NULL) {
        return -1;
    }

    int cmd = KVS_CMD_START;
    for (cmd = KVS_CMD_START; cmd < KVS_CMD_COUNT; cmd++) {
        if (strcmp(tokens[0], command[cmd]) == 0) {
            break;
        }
    }

    int   length = 0;
    int   ret    = 0;
    char *key    = tokens[1];
    char *value  = tokens[2];

    switch(cmd) {
        case KVS_CMD_SET:
            ret = kvs_array_set(&global_array, key, value);
            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0) {
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "EXIST\r\n");
            }
            break;
        case KVS_CMD_GET: {
            char *result;
            result = kvs_array_get(&global_array, key);
            if (result == NULL) {
                length = sprintf(response, "NO EXIST\r\n"); 
            } else {
                length = sprintf(response, "%s\r\n", result);
            }
            break;
        }
        case KVS_CMD_DEL:
            ret = kvs_array_del(&global_array, key);
            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0){ 
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
        case KVS_CMD_MOD:
            ret = kvs_array_mod(&global_array, key, value);
            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0){
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
        case KVS_CMD_EXIST:
            ret = kvs_array_exists(&global_array, key);
            if (ret == 0) {
                length = sprintf(response, "EXIST\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
        default:
            assert(0);
    }

    return length;
}


int kvs_protocol(char *msg, int length, char *response) {

    if (msg == NULL || length <= 0 || response == NULL) {
        return -1;
    }

    char *tokens[KVS_MAX_TOKENS] = {0};

    int count = kvs_split_token(msg, tokens);
    if (count == -1) {
        return -1;
    }

    return kvs_filter_protocol(tokens, count, response);
}

int init_kvengine(int engine_type) {

    // total mempool 10 * 1MB, each block is 128 byte 
    int memp_size = 10 * (1 << 20); 
    int memp_block_size = 128;

    int mem_ret = memp_create(&g_pool, memp_size, memp_block_size);
    if (mem_ret != 0) {
        return -1;
    }
    switch (engine_type) {
        case KV_ENGINE_ARRAY:
            printf("Init KV engine - %d Array: \n", KV_ENGINE_ARRAY);
            int num_element = memp_size / memp_block_size / 2;
            int kvs_array_ret = kvs_array_create(&g_pool, &global_array, num_element);
            if (kvs_array_ret != 0) {
                return -1;
            }
            break;
        case KV_ENGINE_RBTREE:
            printf("Init KV engine - %d RB Tree: \n", KV_ENGINE_RBTREE);
            // kvs_rbtree_create(&global_rbtree);

            break;
        case KV_ENGINE_HASH:
            printf("Init KV engine - %d Hash: \n", KV_ENGINE_HASH);
            // kvs_hash_create(&global_hash);
            break;
        case KV_ENGINE_SKIP:
            printf("Init KV engine - %d SKIP: \n", KV_ENGINE_SKIP);
            break;
    }

    return 0;
}

void dest_kvengine(void) {
    kvs_array_destory(&global_array);
    // kvs_rbtree_destory(&global_rbtree);
    // kvs_hash_destory(&global_hash);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return -1;
    }

    int port = atoi(argv[1]);
    init_kvengine(KV_ENGINE_ARRAY);

    // reactor_start(port, kvs_protocol);
    // ntyco_start(port, kvs_protocol);
    proactor_start(port, kvs_protocol);

    dest_kvengine();
    memp_destroy(&g_pool);
}

