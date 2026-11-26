#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "AOF format assumes a little-endian host"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kvstore.h"
#include "persistence/persist.h"


typedef enum {
    AOF_READ_OK = 0,   // read an entry
    AOF_READ_END,      // clean and end of file
    AOF_READ_TORN,     // file ends in the middle of a record
    AOF_READ_CORRUPT,  // complete but invalid record
    AOF_READ_ERROR,    // read error or out of memory
} aof_read_status_t;

typedef struct {
    uint8_t op;
    char *key;
    char *value;
} aof_record_t;

static int g_aof_fd = -1;  // -1 means no AOF file, append nothing

static int write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

int kvs_aof_open(const char *path) {
    if (path == NULL) {
        return -1;
    }

    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) {
        perror("aof open");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("aof fstat");
        close(fd);
        return -1;
    }

    if (st.st_size == 0) {
        unsigned char header[KVS_AOF_HEADER_SIZE];
        memcpy(header, KVS_AOF_MAGIC, 4);
        header[4] = KVS_AOF_VERSION;
        if (write_all(fd, header, sizeof(header)) < 0 || fsync(fd) != 0 || fsync_dir(path) != 0) {
            perror("aof write header");
            close(fd);
            return -1;
        }
    }

    g_aof_fd = fd;
    return 0;
}

int kvs_aof_append(uint8_t op, const char *key, const char *value) {
    if (g_aof_fd < 0) {
        return 0;
    }

    if (key == NULL) {
        return -1;
    }

    if (value == NULL) {
        value = "";  // DEL vlen=0
    }

    uint32_t klen = strlen(key);
    uint32_t vlen = strlen(value);
    size_t need = 1 + 4 + klen + 4 + vlen + 4;

    unsigned char stack_buf[512];
    unsigned char *buf = need <= sizeof(stack_buf) ? stack_buf : malloc(need);  // only malloc if the buffer size is greater than needed size
    if (buf == NULL) {
        return -1;
    }

    unsigned char *p = buf;

    // op byte 
    *p++ = op;

    // klen byte
    memcpy(p, &klen, 4);
    p += 4;

    // key byte
    memcpy(p, key, klen);
    p += klen;

    // vlen byte
    memcpy(p, &vlen, 4);
    p += 4;

    // value byte
    memcpy(p, value, vlen);
    p += vlen;

    // crc
    uint32_t crc = crc32_update(0, buf, (size_t)(p - buf));
    memcpy(p, &crc, 4);
    p += 4;

    int ret = 0;
    if (write_all(g_aof_fd, buf, need) < 0 || fsync(g_aof_fd) != 0) {
        perror("aof append");
        ret = -1;
    }


    if (buf != stack_buf) {
        free(buf);
    }

    return ret;
}

void kvs_aof_close(void) {
    if (g_aof_fd < 0) {
        return;
    }

    fsync(g_aof_fd);
    close(g_aof_fd);
    g_aof_fd = -1;
}

int kvs_aof_reset(void) {
    if (g_aof_fd < 0) {
        return 0;
    }

    // keep only the header. fd is O_APPEND, so the next write goes to the
    // new end of file automatically, no lseek needed
    if (ftruncate(g_aof_fd, KVS_AOF_HEADER_SIZE) != 0 || fsync(g_aof_fd) != 0) {
        perror("aof reset");
        return -1;
    }
    return 0;
}

static void aof_record_free(aof_record_t *rec) {
    free(rec->key);
    free(rec->value);
    rec->key = NULL;
    rec->value = NULL;
}

static aof_read_status_t short_read_status(FILE *fp) {
    return ferror(fp) ? AOF_READ_ERROR : AOF_READ_TORN;
}

static aof_read_status_t read_field(FILE *fp, char **out, uint32_t *crc) {
    uint32_t len;
    if (read_exact(fp, &len, sizeof(len)) < 0) {
        return short_read_status(fp);
    }

    if (len > KVS_SNAPSHOT_MAX_LEN) {
        return AOF_READ_CORRUPT;
    }

    *crc = crc32_update(*crc, &len, sizeof(len));

    char *s = malloc(len + 1);
    if (s == NULL) {
        return AOF_READ_ERROR;
    }

    if (read_exact(fp, s, len) < 0) {
        free(s);
        return short_read_status(fp);
    }

    *crc = crc32_update(*crc, s, len);
    s[len] = '\0';
    *out = s;
    return AOF_READ_OK;
}

static aof_read_status_t aof_read_record(FILE *fp, aof_record_t *rec) {
    rec->key = NULL;
    rec->value = NULL;

    // op is the first char, if we can't get anything, meaning EOF or error
    int c = fgetc(fp);
    if (c == EOF) {
        return ferror(fp) ? AOF_READ_ERROR : AOF_READ_END;
    }
    rec->op = (uint8_t)c;

    if (rec->op < KVS_AOF_OP_SET || rec->op > KVS_AOF_OP_DEL) {
        return AOF_READ_CORRUPT;
    }

    // crc must be accumulated in the same order kvs_aof_append writes: op, klen, key, vlen, value
    uint32_t crc = crc32_update(0, &rec->op, 1);

    aof_read_status_t st = read_field(fp, &rec->key, &crc);
    if (st != AOF_READ_OK) {
        aof_record_free(rec);
        return st;
    }

    st = read_field(fp, &rec->value, &crc);
    if (st != AOF_READ_OK) {
        aof_record_free(rec);
        return st;
    }

    uint32_t crc_in_file;
    if (read_exact(fp, &crc_in_file, sizeof(crc_in_file)) < 0) {
        aof_record_free(rec);
        return short_read_status(fp);
    }

    if (crc != crc_in_file) {
        aof_record_free(rec);
        return AOF_READ_CORRUPT;
    }

    return AOF_READ_OK;

}

static int aof_apply(const aof_record_t *rec) {
    switch(rec->op) {
        case KVS_AOF_OP_SET:
            return kvs_engine_set(rec->key, rec->value);
        case KVS_AOF_OP_MOD:
            return kvs_engine_mod(rec->key, rec->value);
        case KVS_AOF_OP_DEL:
            return kvs_engine_del(rec->key);
    }
    return -1;
}

// truncate the file to len bytes and make sure it reaches the disk
static int aof_truncate(const char *path, off_t len) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("aof truncate open");
        return -1;
    }

    int ret = (ftruncate(fd, len) == 0 && fsync(fd) == 0) ? 0 : -1;
    if (ret != 0) {
        perror("aof truncate");
    }
    close(fd);
    return ret;
}

static int aof_load_fail(FILE *fp, const char *reason, long offset) {
    fprintf(stderr, "aof load failed at offset %ld: %s\n", offset, reason);
    fclose(fp);
    return -1;
}

int kvs_aof_load(const char *path) {
    if (path == NULL) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        if (errno == ENOENT) {
            return 1;
        }
        perror("aof load fopen");
        return -1;
    }

    // 1. header
    unsigned char header[KVS_AOF_HEADER_SIZE];
    size_t n = fread(header, 1, sizeof(header), fp);
    if (n < sizeof(header)) {
        if (ferror(fp)) {
            return aof_load_fail(fp, "read error in header", 0);
        }
        // crashed while kvs_aof_open was writing the header: no records yet.
        // truncate to 0, kvs_aof_open will write a fresh header
        fclose(fp);
        fprintf(stderr, "aof: incomplete header (%zu bytes), resetting %s\n", n, path);
        return aof_truncate(path, 0) == 0 ? 0 : -1;
    }
    if (memcmp(header, KVS_AOF_MAGIC, 4) != 0) {
        return aof_load_fail(fp, "bad magic, not an aof file", 0);
    }
    if (header[4] != KVS_AOF_VERSION) {
        return aof_load_fail(fp, "unsupported version", 4);
    }

    // 2. replay records one by one
    long good_end = KVS_AOF_HEADER_SIZE;  // end of the last complete record
    uint32_t applied = 0, skipped = 0;
    int torn = 0;

    for (;;) {
        aof_record_t rec;
        aof_read_status_t st = aof_read_record(fp, &rec);

        if (st == AOF_READ_END) {
            break;
        }
        if (st == AOF_READ_TORN) {
            torn = 1;
            break;
        }
        if (st == AOF_READ_CORRUPT) {
            return aof_load_fail(fp, "corrupt record", good_end);
        }
        if (st == AOF_READ_ERROR) {
            return aof_load_fail(fp, "read error", good_end);
        }

        int ret = aof_apply(&rec);
        aof_record_free(&rec);
        if (ret < 0) {
            return aof_load_fail(fp, "engine rejected record", good_end);
        }
        // > 0: SET on an existing key, MOD/DEL on a missing key.
        // normal when the snapshot already contains this operation
        if (ret == 0) {
            applied++;
        } else {
            skipped++;
        }

        good_end = ftell(fp);
    }

    // on TORN the position is at end of file, so this is the file size
    long file_end = ftell(fp);
    fclose(fp);

    // 3. drop the half-written last record, so new appends start on a record boundary
    if (torn) {
        fprintf(stderr, "aof: torn last record, dropping %ld bytes at offset %ld\n",
                file_end - good_end, good_end);
        if (aof_truncate(path, good_end) != 0) {
            return -1;
        }
    }

    printf("aof replayed: %u applied, %u skipped from %s\n", applied, skipped, path);
    return 0;
}
