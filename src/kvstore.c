#include "kvstore.h"
#include "persistence/persist.h"

static mempool_t g_pool;

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
    "SAVE",
};


enum {
    KVS_CMD_START = 0,

    KVS_CMD_SET = KVS_CMD_START,
    KVS_CMD_GET,
    KVS_CMD_DEL,
    KVS_CMD_MOD,
    KVS_CMD_EXIST,
    KVS_CMD_SAVE,

    KVS_CMD_COUNT,
};

int kvs_split_token(char *msg, char *tokens[]) {
    if (msg == NULL || tokens == NULL) {
        return -1;
    }

    int idx = 0;

    // 1. First call passes the string
    char *token = strtok(msg, " \r\n");

    // 2. Loop until tokens remain
    while(token != NULL) {
        tokens[idx++] = token;
        token = strtok(NULL, " \r\n");
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
            ret = kvs_engine_set(key, value);

            // save aof file
            if (ret == 0 && kvs_aof_append(KVS_AOF_OP_SET, key, value) < 0) {
                ret = -1;
            }

            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0) {
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "EXIST\r\n");
            }
            break;
        case KVS_CMD_GET: {
            char *result = NULL;
            result = kvs_engine_get(key);

            if (result == NULL) {
                length = sprintf(response, "NO EXIST\r\n"); 
            } else {
                length = sprintf(response, "%s\r\n", result);
            }
            break;
        }
        case KVS_CMD_DEL:
            ret = kvs_engine_del(key);

            if (ret == 0 && kvs_aof_append(KVS_AOF_OP_DEL, key, value) < 0) {
                ret = -1;
            }

            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0){ 
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
        case KVS_CMD_MOD:
            ret = kvs_engine_mod(key, value);

            if (ret == 0 && kvs_aof_append(KVS_AOF_OP_MOD, key, value) < 0) {
                ret = -1;
            }

            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0){
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
        case KVS_CMD_EXIST:
            ret = kvs_engine_exist(key);

            if (ret == 0) {
                length = sprintf(response, "EXIST\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
        case KVS_CMD_SAVE:
            ret = kvs_snapshot_save(KVS_SNAPSHOT_PATH);
            if (ret == 0) {
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "ERROR\r\n");
            }
            break;
        default:
            length = sprintf(response, "ERROR\r\n");
            break;
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

int init_kvengine(void) {

    // total mempool 10 * 1MB, each block is 128 byte 
    int memp_size = 10 * (1 << 20); 
    int memp_block_size = 128;

    int mem_ret = memp_create(&g_pool, memp_size, memp_block_size);
    if (mem_ret != 0) {
        return -1;
    }
    printf("Init KV engine: %s\n", kvs_engine_name());
    return kvs_engine_init(&g_pool);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Not enough arg\n");
        return -1;
    }

    int port = atoi(argv[1]);
    int ret = init_kvengine();
    if (ret != 0) {
        return -1;
    }

    ret = kvs_snapshot_load(KVS_SNAPSHOT_PATH);
    if (ret < 0) {
        printf("refuse to start: %s is corrupt or unreadable\n", KVS_SNAPSHOT_PATH);
        kvs_engine_destroy();
        memp_destroy(&g_pool);
        return -1;
    }

    if (ret == 1) {
        printf("No snapshot found, start with empty store.\n");
    }

    if (kvs_aof_open(KVS_AOF_PATH) != 0) {
        printf("reufse to start: cannot open %s\n", KVS_AOF_PATH);
        kvs_engine_destroy();
        memp_destroy(&g_pool);
    }
    // reactor_start(port, kvs_protocol);
    // ntyco_start(port, kvs_protocol);
    proactor_start(port, kvs_protocol);

    kvs_engine_destroy();
    memp_destroy(&g_pool);
}

