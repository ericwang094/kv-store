#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "snapshot format assumes a little-endian host"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>

#include "persistence/persist.h"
#include "kvstore.h"

#define KVS_SNAPSHOT_COUNT_OFFSET 5 // magic(4) + version(1)

typedef struct snapshot_ctx_s {
    FILE *fp;
    uint32_t crc;
    uint32_t count;
} snapshot_ctx_t;

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
    const unsigned char *p = data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
        }
    }

    return ~crc;
}

static int write_bytes(snapshot_ctx_t *ctx, const void *data, size_t len) {
    if (len > 0 && fwrite(data, 1, len, ctx->fp) != len) {
        return -1;
    }

    ctx->crc = crc32_update(ctx->crc, data, len);
    return 0;
}

static int snapshot_write_cb(const char *key, const char *value, void *ctx) {
    snapshot_ctx_t *ss_ctx = ctx;
    uint32_t k_len = strlen(key);
    uint32_t v_len = strlen(value);

    if (write_bytes(ss_ctx, &k_len, sizeof(k_len)) < 0 ||
        write_bytes(ss_ctx, key, k_len) < 0 ||
        write_bytes(ss_ctx, &v_len, sizeof(v_len)) < 0 ||
        write_bytes(ss_ctx, value, v_len) < 0) {

        return -1;
    }

    ss_ctx->count++;

    return 0;
}

static int fsync_dir(const char *path) {

    char buf[PATH_MAX];

    int ret;

    ret = snprintf(buf, sizeof(buf), "%s", path);

    // duplicate path to memory buffer.
    // if < 0, means writing failed
    // if >= (int)sizeof(buf), means the path lenth exceeds PATH_MAX,
    if (ret < 0 || ret >= (int)sizeof(buf)) {
        return -1;
    }

    const char *dir = dirname(buf);

    int dfd = open(dir, O_RDONLY);
    if (dfd < 0) {
        return -1;
    }

    ret = fsync(dfd);
    close(dfd);

    return ret;
}

static int snapshot_save_fail(FILE *fp, const char *tmp) {
    perror("snapshot save error");
    if (fp != NULL) {
        fclose(fp);
    }
    unlink(tmp);
    return -1;
}

int kvs_snapshot_save(const char *path) {
     if (path == NULL) {
        return -1;
    }

    char tmp[PATH_MAX];
    int ret;

    // prepare a tmp file path
    ret = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    //failed or given path length too long
    if (ret < 0 || ret >= (int)sizeof(tmp)) {
        return -1;
    }

    // open tmp file, use wb to clean the old tmp 
    FILE *fp = fopen(tmp, "wb");
    if (fp == NULL) {
        perror("snapshot fopen");
        return -1;
    }

    // magic + version + count placeholder
    uint8_t version = KVS_SNAPSHOT_VERSION;
    uint32_t count = 0;
    // fwrite(ptr, 
    //       size in bytes for individual element, 
    //       number of element to write,
    //       file stream
    //)
    if (fwrite(KVS_SNAPSHOT_MAGIC, 1, 4, fp) != 4 ||
        fwrite(&version, sizeof(version), 1, fp) != 1 ||
        fwrite(&count, sizeof(count), 1, fp) != 1) {
        return snapshot_save_fail(fp, tmp);
    }

    snapshot_ctx_t ctx = {
        .fp = fp,
        .crc = 0,
        .count = 0
    };

    if(kvs_engine_traversal(snapshot_write_cb, &ctx) != 0) {
        return snapshot_save_fail(fp, tmp);
    }

    if (fwrite(&ctx.crc, sizeof(ctx.crc), 1, fp) != 1) {
        return snapshot_save_fail(fp, tmp);
    }

    // use fseek to go back to position 5 to add count value in
    if (fseek(fp, KVS_SNAPSHOT_COUNT_OFFSET, SEEK_SET) != 0 ||
        fwrite(&ctx.count, sizeof(ctx.count), 1, fp) != 1) {
        return snapshot_save_fail(fp, tmp);
    }

    // fisr to call fflush and then fsync
    // fflush flush buffer to os kernel
    // flush kernel buffer to disk
    if (fflush(fp) != 0 || fsync(fileno(fp)) != 0) {
        return snapshot_save_fail(fp, tmp);
    }

    int close_ret = fclose(fp);
    fp = NULL;
    if (close_ret != 0) {
        return snapshot_save_fail(NULL, tmp);
    }

    if (rename(tmp, path) != 0) {
        return snapshot_save_fail(NULL, tmp);
    }

    if (fsync_dir(path) != 0) {
        perror("snapshot fsync dir");
        return -1;
    }

    return 0;
}

