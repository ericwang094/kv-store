#include "kvstore.h"

// djb2: h = h * 33 + c. Character position matters, so "ab" and "ba" differ.
// unsigned char: bytes >= 0x80 (e.g. UTF-8) must not become negative on x86.
static int _hash(const char *key, int size) {
    if (!key || size <= 0)
        return -1;

    unsigned int h = 5381;
    while (*key) {
        h = h * 33 + (unsigned char)*key++;
    }

    return (int)(h % (unsigned int)size);
}

static char *_dup_string(mempool_t *m, const char *str) {
    size_t len = strlen(str);
    char *copy = kvs_malloc(m, len + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, str, len + 1); // includes the trailing '\0'
    return copy;
}

static void _free_node(mempool_t *m, hashnode_t *node) {
    kvs_free(m, node->key);
    kvs_free(m, node->value);
    kvs_free(m, node);
}

static hashnode_t *_create_node(mempool_t *m, char *key, char *value) {
    hashnode_t *node = (hashnode_t *)kvs_malloc(m, sizeof(hashnode_t));
    if (!node)
        return NULL;

    node->key = _dup_string(m, key);
    if (node->key == NULL) {
        kvs_free(m, node);
        return NULL;
    }

    node->value = _dup_string(m, value);
    if (node->value == NULL) {
        kvs_free(m, node->key);
        kvs_free(m, node);
        return NULL;
    }

    node->next = NULL;
    return node;
}

int kvs_hash_create(mempool_t *m, kvs_hash_t *hash, int max_slots) {
    if (!m || !hash || max_slots <= 0)
        return -1;

    // calloc: every slot starts as NULL (an empty list)
    hash->nodes = (hashnode_t **)calloc(max_slots, sizeof(hashnode_t *));
    if (!hash->nodes)
        return -1;

    hash->pool      = m;
    hash->max_slots = max_slots;
    hash->count     = 0;

    return 0;
}

void kvs_hash_destory(kvs_hash_t *hash) {
    if (!hash || !hash->nodes)
        return;

    int i = 0;
    for (i = 0; i < hash->max_slots; i++) {
        hashnode_t *node = hash->nodes[i];
        while (node != NULL) {
            hashnode_t *tmp = node;
            node            = node->next;
            _free_node(hash->pool, tmp); // key, value and the node itself
        }
        hash->nodes[i] = NULL;
    }

    free(hash->nodes);
    hash->nodes     = NULL;
    hash->pool      = NULL;
    hash->max_slots = 0;
    hash->count     = 0;
}

// return < 0 error, == 0 success, > 0 already exists
int kvs_hash_set(kvs_hash_t *hash, char *key, char *value) {
    if (!hash || !hash->nodes || !key || !value)
        return -1;

    int idx = _hash(key, hash->max_slots);

    hashnode_t *node = hash->nodes[idx];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) // exist
            return 1;
        node = node->next;
    }

    hashnode_t *new_node = _create_node(hash->pool, key, value);
    if (new_node == NULL)
        return -2;

    // insert at head of the slot's list
    new_node->next   = hash->nodes[idx];
    hash->nodes[idx] = new_node;

    hash->count++;

    return 0;
}

char *kvs_hash_get(kvs_hash_t *hash, char *key) {
    if (!hash || !hash->nodes || !key)
        return NULL;

    int idx = _hash(key, hash->max_slots);

    hashnode_t *node = hash->nodes[idx];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0)
            return node->value;
        node = node->next;
    }

    return NULL;
}

// return < 0 error, == 0 success, > 0 not exist
int kvs_hash_mod(kvs_hash_t *hash, char *key, char *value) {
    if (!hash || !hash->nodes || !key || !value)
        return -1;

    int idx = _hash(key, hash->max_slots);

    hashnode_t *node = hash->nodes[idx];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0)
            break;
        node = node->next;
    }

    if (node == NULL)
        return 1;

    // allocate the new value first: on failure the old value is still valid
    char *kvalue = _dup_string(hash->pool, value);
    if (kvalue == NULL)
        return -2;

    kvs_free(hash->pool, node->value);
    node->value = kvalue;

    return 0;
}

int kvs_hash_count(kvs_hash_t *hash) { return hash ? hash->count : 0; }

// return < 0 error, == 0 success, > 0 not exist
int kvs_hash_del(kvs_hash_t *hash, char *key) {
    if (!hash || !hash->nodes || !key)
        return -1;

    int idx = _hash(key, hash->max_slots);

    // pp points to the pointer that links to the current node:
    // first &nodes[idx], then &prev->next. This handles the head node
    // and the middle/tail nodes the same way.
    hashnode_t **pp = &hash->nodes[idx];
    while (*pp != NULL) {
        if (strcmp((*pp)->key, key) == 0) {
            hashnode_t *tmp = *pp;
            *pp             = tmp->next; // unlink
            _free_node(hash->pool, tmp);
            hash->count--;
            return 0;
        }
        pp = &(*pp)->next;
    }

    return 1; // not exist
}

// return 0 exist, 1 not exist
int kvs_hash_exist(kvs_hash_t *hash, char *key) {
    char *value = kvs_hash_get(hash, key);
    if (!value)
        return 1;

    return 0;
}

int kvs_hash_traversal(kvs_hash_t *hash, kvs_traversal_cb cb, void *ctx) {
    if (!hash || !hash->nodes || !cb) {
        return -1;
    }

    for (int i = 0; i < hash->max_slots; i++) {
        for (hashnode_t *node = hash->nodes[i]; node != NULL; node = node->next) {
            // if ret is 0, means there are more items, continue
            int ret = cb(node->key, node->value, ctx);
            if (ret != 0) {
                return ret;
            }
        }
    }

    return 0;
}

