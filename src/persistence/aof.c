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

#include "persistence/persist.h"


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

void kvs_aod_close(void) {
    if (g_aof_fd < 0) {
        return;
    }

    fsync(g_aof_fd);
    close(g_aof_fd);
    g_aof_fd = -1;
}
