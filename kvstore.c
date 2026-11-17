#include "kvstore.h"

extern kvs_array_t global_array;
extern kvs_rbtree_t global_rbtree;
// extern kvs_hash_t global_hash;

void *kvs_malloc(size_t size) {
    return malloc(size);
}

void kvs_free(void *ptr) {
    return free(ptr);
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
    //rbtree
    KVS_CMD_RSET,
    KVS_CMD_RGET,
    KVS_CMD_RDEL,
    KVS_CMD_RMOD,
    KVS_CMD_REXIST,
    //hash
    KVS_CMD_HSET,
    KVS_CMD_HGET,
    KVS_CMD_HDEL,
    KVS_CMD_HMOD,
    KVS_CMD_HEXIST,

    KVS_CMD_COUNT,
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
        case KVS_CMD_RSET:
            ret = kvs_rbtree_set(&global_rbtree, key, value);
            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0) {
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "EXIST\r\n");
            }
            break;
        case KVS_CMD_RGET: {
            char *result;
            result = kvs_rbtree_get(&global_rbtree, key);
            if (result == NULL) {
                length = sprintf(response, "NO EXIST\r\n"); 
            } else {
                length = sprintf(response, "%s\r\n", result);
            }
            break;
        }
        case KVS_CMD_RDEL:
            ret = kvs_rbtree_del(&global_rbtree, key);
            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0){ 
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
        case KVS_CMD_RMOD:
            ret = kvs_rbtree_mod(&global_rbtree, key, value);
            if (ret < 0) {
                length = sprintf(response, "ERROR\r\n");
            } else if (ret == 0){
                length = sprintf(response, "OK\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
        case KVS_CMD_REXIST:
            ret = kvs_rbtree_exist(&global_rbtree, key);
            if (ret == 0) {
                length = sprintf(response, "EXIST\r\n");
            } else {
                length = sprintf(response, "NO EXIST\r\n");
            }
            break;
            return 0;
        // case KVS_CMD_HSET:
        //     ret = kvs_hash_set(&global_array, key, value);
        //     if (ret < 0) {
        //         length = sprintf(response, "ERROR\r\n");
        //     } else if (ret == 0) {
        //         length = sprintf(response, "OK\r\n");
        //     } else {
        //         length = sprintf(response, "EXIST\r\n");
        //     }
        //     break;
        // case KVS_CMD_HGET:
        //     result = kvs_hash_get(&global_array, key);
        //     if (result == NULL) {
        //         length = sprintf(response, "NO EXIST\r\n"); 
        //     } else {
        //         length = sprintf(response, "%s\r\n", result);
        //     }
        //     break;
        // case KVS_CMD_HDEL:
        //     ret = kvs_hash_del(&global_array, key);
        //     if (ret < 0) {
        //         length = sprintf(response, "ERROR\r\n");
        //     } else if (ret == 0){ 
        //         length = sprintf(response, "OK\r\n");
        //     } else {
        //         length = sprintf(response, "NO EXIST\r\n");
        //     }
        //     break;
        // case KVS_CMD_HMOD:
        //     ret = kvs_hash_mod(&global_array, key);
        //     if (ret == 0) {
        //         length = sprintf(response, "EXIST\r\n");
        //     } else {
        //         length = sprintf(response, "NO EXIST\r\n");
        //     }
        //     break;
        //
        // case KVS_CMD_HEXIST:
        //     ret = kvs_hash_exists(&global_array, key);
        //     if (ret == 0) {
        //         length = sprintf(response, "EXIST\r\n");
        //     } else {
        //         length = sprintf(response, "NO EXIST\r\n");
        //     }
        //     break;
        defualt:
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

int init_kvengine(void) {
    memset(&global_array, 0, sizeof(kvs_array_t));
    kvs_array_create(&global_array);

    memset(&global_rbtree, 0, sizeof(kvs_rbtree_t)); 
    kvs_rbtree_create(&global_rbtree);

    // memset(&global_hash, 0, sizeof(kvs_hash_t));
    // kvs_hash_create(&global_hash);

    return 0;
}

void dest_kvengine(void) {
    kvs_array_destory(&global_array);
    kvs_rbtree_destory(&global_rbtree);
    // kvs_hash_destory(&global_hash);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return -1;
    }

    int port = atoi(argv[1]);
    init_kvengine();

    // reactor_start(port, kvs_protocol);
    // ntyco_start(port, kvs_protocol);
    proactor_start(port, kvs_protocol);

    dest_kvengine();
}

