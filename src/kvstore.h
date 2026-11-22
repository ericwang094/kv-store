#ifndef __KV_STORE_H__
#define __KV_STORE_H__

#include "mem_pool/mem_pool.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KVS_MAX_TOKENS 128
// network
typedef int (*msg_handler)(char *msg, int length, char *response);

extern int reactor_start(unsigned short port, msg_handler handler);
extern int proactor_start(unsigned short port, msg_handler handler);
extern int ntyco_start(unsigned short port, msg_handler handler);

// kvs_array

typedef struct kvs_array_item_s {
    char *key;
    char *value;
} kvs_array_item_t;

typedef struct kvs_array_s {

    kvs_array_item_t *table;
    int               kvs_array_size;
    mempool_t        *pool;
    int               idx;
    int               total;
} kvs_array_t;

/**
 * mempool_t *m mempool
 * kvs_array_t kvs_array_t *inst kvs array instance
 * int kvs_array_size total number of elements
 */
int  kvs_array_create(mempool_t *m, kvs_array_t *inst, int kvs_array_size);
void kvs_array_destory(kvs_array_t *inst);

int   kvs_array_set(kvs_array_t *inst, char *key, char *value);
char *kvs_array_get(kvs_array_t *inst, char *key);
int   kvs_array_del(kvs_array_t *inst, char *key);
int   kvs_array_mod(kvs_array_t *inst, char *key, char *value);
int   kvs_array_exists(kvs_array_t *inst, char *key);

// btrees

#define RED 1
#define BLACK 2

#define ENABLE_KEY_CHAR 1

#if ENABLE_KEY_CHAR
typedef char *KEY_TYPE;
#else
typedef int KEY_TYPE; // key
#endif

typedef struct _rbtree_node {
    unsigned char        color;
    struct _rbtree_node *right;
    struct _rbtree_node *left;
    struct _rbtree_node *parent;
    KEY_TYPE             key;
    void                *value;
} rbtree_node;

typedef struct _rbtree {
    rbtree_node *root;
    rbtree_node *nil;
} rbtree;

typedef struct _rbtree kvs_rbtree_t;

int   kvs_rbtree_create(kvs_rbtree_t *inst);
void  kvs_rbtree_destory(kvs_rbtree_t *inst);
int   kvs_rbtree_set(kvs_rbtree_t *inst, char *key, char *value);
char *kvs_rbtree_get(kvs_rbtree_t *inst, char *key);
int   kvs_rbtree_del(kvs_rbtree_t *inst, char *key);
int   kvs_rbtree_mod(kvs_rbtree_t *inst, char *key, char *value);
int   kvs_rbtree_exist(kvs_rbtree_t *inst, char *key);

// hash
typedef struct hashnode_s {
    char              *key;
    char              *value;
    struct hashnode_s *next;

} hashnode_t;

typedef struct hashtable_s {

    hashnode_t **nodes;
    mempool_t   *pool;      // pool for nodes and key/value strings

    int max_slots;          // number of buckets (length of the nodes array)
    int count;              // number of keys stored

} kvs_hash_t;

/**
 * mempool_t *m     pool used for every node and key/value string of this table
 * kvs_hash_t *hash hash table instance
 * int max_slots    number of buckets
 */
int   kvs_hash_create(mempool_t *m, kvs_hash_t *hash, int max_slots);
void  kvs_hash_destory(kvs_hash_t *hash);
int   kvs_hash_set(kvs_hash_t *hash, char *key, char *value);
char *kvs_hash_get(kvs_hash_t *hash, char *key);
int   kvs_hash_mod(kvs_hash_t *hash, char *key, char *value);
int   kvs_hash_del(kvs_hash_t *hash, char *key);
int   kvs_hash_exist(kvs_hash_t *hash, char *key);
int   kvs_hash_count(kvs_hash_t *hash);

// engine interface: exactly one engine_xxx.c provides these
int kvs_engine_init(mempool_t *m);
void kvs_engine_destroy(void);
int kvs_engine_set(char *key, char *value);
char *kvs_engine_get(char *key);
int kvs_engine_del(char *key);
int kvs_engine_mod(char *key, char *value);
int kvs_engine_exist(char *key);
const char *kvs_engine_name(void);


// general

void *kvs_malloc(mempool_t *m, size_t size);
void  kvs_free(mempool_t *m, void *ptr);

#endif
