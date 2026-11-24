/**
* Format of the file save format
* [magic "KVS1"][version u8][count u32]
*  [klen u32][key bytes][vlen u32][value bytes]
* [checksum u32]

*/

#ifndef __PERSIST_H__
#define __PERSIST_H__

#include <stdint.h>

#define KVS_SNAPSHOT_PATH "dump.kvs"
#define KVS_SNAPSHOT_MAGIC "KVS1"
#define KVS_SNAPSHOT_VERSION 1
#define KVS_SNAPSHOT_MAX_LEN  (1u << 20)  // limit key/value to be 1M,

// 0 success, < 0 error
int kvs_snapshot_save(const char *path);

// 0 loaded, 1 file does not exist (fresh start), < 0 corrupt / io err
int kvs_snapshot_load(const char *path);

#endif
