#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>

#include "persistence/persist.h"


uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
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

int fsync_dir(const char *path) {

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
