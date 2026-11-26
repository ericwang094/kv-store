
#ifndef __PERSIST_H__
#define __PERSIST_H__

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
int fsync_dir(const char *path);
int read_exact(FILE *fp, void *buf, size_t n);

// ------- SNAPSHOT -----

/**
* Format of the snapshot file save format
* [magic "KVS1"][version u8][count u32]
*  [klen u32][key bytes][vlen u32][value bytes]
* [checksum u32]
*/

// 0 replayed, 1 file does not exist <0 error
int kvs_snapshot_load(const char *path);


#define KVS_SNAPSHOT_PATH "dump.kvs"
#define KVS_SNAPSHOT_MAGIC "KVS1"
#define KVS_SNAPSHOT_VERSION 1
#define KVS_SNAPSHOT_MAX_LEN  (1u << 20)  // limit key/value to be 1M,

// 0 success, < 0 error
int kvs_snapshot_save(const char *path);

// 0 loaded, 1 file does not exist (fresh start), < 0 corrupt / io err

// ------ AOF ------
/**
 * [magic "KAOF" 4B][version u8]
 *   [op u8][klen u32][key][vlen u32][value][crc u32]
 * crc covers op .. value of that one record. DEL has vlen = 0.
 */

#define KVS_AOF_PATH "append.aof"
#define KVS_AOF_MAGIC "KAOF"
#define KVS_AOF_VERSION 1
#define KVS_AOF_HEADER_SIZE 5  // magic(4) + version(1)

enum {
    KVS_AOF_OP_SET = 1,
    KVS_AOF_OP_MOD = 2,
    KVS_AOF_OP_DEL = 3,
};

// open for appending, create with a header if is missing. 0 ok, < 0 error
int kvs_aof_open(const char *path);

// append one record and fsync. value is NULL for DEL.
// 0 ok, < 0 write fail
int kvs_aof_append(uint8_t, const char *key, const char *value);
void kvs_aof_close(void);
int kvs_aof_load(const char *path);

#endif
