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

#include "persistence/persist.h"
#include "kvstore.h"

#define KVS_SNAPSHOT_COUNT_OFFSET 5 // magic(4) + version(1)

typedef struct snapshot_ctx_s {
    FILE *fp;
    uint32_t crc;
    uint32_t count;
} snapshot_ctx_t;


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



static char *read_string(FILE *fp, uint32_t *crc) {
    uint32_t len;
    if (read_exact(fp, &len, sizeof(len)) < 0) {
        return NULL;
    }

    if (len > KVS_SNAPSHOT_MAX_LEN) {
        return NULL;
    }

    *crc = crc32_update(*crc, &len, sizeof(len));

    char *s = malloc(len + 1);
    if (s == NULL) {
        return NULL;
    }

    if (read_exact(fp, s, len) < 0) {
        free(s);
        return NULL;
    }

    *crc = crc32_update(*crc, s, len);
    s[len] = '\0';
    return s; 
}

static int snapshot_save_fail(FILE *fp, const char *tmp) {
    perror("snapshot save error");
    if (fp != NULL) {
        fclose(fp);
    }
    unlink(tmp);
    return -1;
}

static int snapshot_load_fail(FILE *fp, const char *reason) {
    fprintf(stderr, "Snapshot load failed: %s\n", reason);
    fclose(fp);
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

int kvs_snapshot_load(const char *path) {
    if (path == NULL) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        if (errno == ENOENT) {
            return 1;
        }
        perror("snapshot load fopen");
        return -1;
    }

    char magic[4];
    uint8_t version;
    uint32_t count;
    
    // read header
    if (read_exact(fp, magic, sizeof(magic)) < 0 ||
        read_exact(fp, &version, sizeof(version)) < 0 ||
        read_exact(fp, &count, sizeof(count)) < 0) {
        return snapshot_load_fail(fp, "truncated header");
    }

    if (memcmp(magic, KVS_SNAPSHOT_MAGIC, sizeof(magic)) != 0) {
        return snapshot_load_fail(fp, "bad magic, not a snapshot file");
    }

    if (version != KVS_SNAPSHOT_VERSION) {
        return snapshot_load_fail(fp, "Unsupported version");
    }

    // read data entry by entry and put it into kvs engine
    uint32_t crc = 0;
    for (uint32_t i = 0; i < count; i++) {
        char *key = read_string(fp, &crc);
        if (key == NULL) {
            return snapshot_load_fail(fp, "bad key");
        }

        char  *value = read_string(fp, &crc);
        if (value == NULL) {
            free(key);
            return snapshot_load_fail(fp, "bad value");
        }

        int ret = kvs_engine_set(key, value);
        free(key);
        free(value);

        if (ret != 0) {
            return snapshot_load_fail(fp, "kvs engine set failed");
        }
    }

    // read checksum and validation
    uint32_t crc_in_file;
    if (read_exact(fp, &crc_in_file, sizeof(crc_in_file)) < 0) {
        return snapshot_load_fail(fp, "missing crc");
    }

    if (crc != crc_in_file) {
        return snapshot_load_fail(fp, "crc mismatch, file is corrupt");
    }

    if (fgetc(fp) != EOF) {
        return snapshot_load_fail(fp, "trailing bytes after crc");
    }

    fclose(fp);
    printf("snapshot loaded: %u keys from %s\n", count, path);
    return 0;

}
