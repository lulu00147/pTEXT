#include "persist.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int32_t persist_write_atomic(const char *path, const void *buf, size_t len)
{
    if (!path || (!buf && len > 0)) return -1;

    char tmp[1100];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) return -1;

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;

    const uint8_t *p = (const uint8_t *)buf;
    size_t done = 0;
    while (done < len) {
        ssize_t w = write(fd, p + done, len - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            return -1;
        }
        if (w == 0) {
            close(fd);
            unlink(tmp);
            return -1;
        }
        done += (size_t)w;
    }

    int32_t ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok && rename(tmp, path) == 0) return 0;
    unlink(tmp);
    return -1;
}
