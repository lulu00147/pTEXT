#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "reader.h"
#include "render_host.h"

enum {
    FAIL_NONE = 0,
    FAIL_OPEN,
    FAIL_WRITE,
    FAIL_SHORT_WRITE,
    FAIL_FSYNC,
    FAIL_CLOSE,
    FAIL_RENAME
};

static int fail_mode;
static int idx_fd = -1;

static int is_idx_tmp(const char *path)
{
    return path && strstr(path, ".idx/") && strstr(path, ".tmp");
}

static int is_idx_path(const char *path)
{
    return path && strstr(path, ".idx/");
}

int __real_open(const char *path, int flags, ...);
ssize_t __real_write(int fd, const void *buf, size_t count);
int __real_fsync(int fd);
int __real_close(int fd);
int __real_rename(const char *oldpath, const char *newpath);

int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (fail_mode == FAIL_OPEN && is_idx_tmp(path)) {
        errno = EACCES;
        return -1;
    }
    int fd = (flags & O_CREAT) ? __real_open(path, flags, mode)
                               : __real_open(path, flags);
    if (fd >= 0 && is_idx_tmp(path)) idx_fd = fd;
    return fd;
}

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
    if (fd == idx_fd && fail_mode == FAIL_WRITE) {
        errno = EIO;
        return -1;
    }
    if (fd == idx_fd && fail_mode == FAIL_SHORT_WRITE) return 0;
    return __real_write(fd, buf, count);
}

int __wrap_fsync(int fd)
{
    if (fd == idx_fd && fail_mode == FAIL_FSYNC) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

int __wrap_close(int fd)
{
    int result = __real_close(fd);
    if (fd == idx_fd) {
        idx_fd = -1;
        if (fail_mode == FAIL_CLOSE) return -1;
    }
    return result;
}

int __wrap_rename(const char *oldpath, const char *newpath)
{
    if (fail_mode == FAIL_RENAME && is_idx_path(newpath)) {
        errno = EIO;
        return -1;
    }
    return __real_rename(oldpath, newpath);
}

static void write_text(void)
{
    const char *unit = "索引故障测试，正文。下一句。\n";
    FILE *f = fopen("idx-large.txt", "wb");
    assert(f != NULL);
    for (int i = 0; i < 30000; i++) {
        size_t n = strlen(unit);
        assert(fwrite(unit, 1, n, f) == n);
    }
    fclose(f);
}

static size_t read_file(const char *path, uint8_t *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

int main(void)
{
    char dir[] = "/tmp/ptext-index-fault-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    rd_init(dir);
    char files[512];
    snprintf(files, sizeof(files), "%s/files", dir);
    assert(chdir(files) == 0);
    write_text();

    assert(rd_open_file("idx-large.txt") == 0);
    for (int i = 0; i < 200; i++) rd_next_page();
    rd_close();
    uint8_t baseline[32768];
    size_t baseline_len = read_file("../.idx/idx-large.txt.bin", baseline, sizeof(baseline));
    assert(baseline_len > 48);

    const int modes[] = {FAIL_OPEN, FAIL_WRITE, FAIL_SHORT_WRITE,
                         FAIL_FSYNC, FAIL_CLOSE, FAIL_RENAME};
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        fail_mode = FAIL_NONE;
        assert(rd_open_file("idx-large.txt") == 0);
        rd_reset_page_cache();
        rd_test_page_checkpoint_add(0, 1);
        rd_test_page_checkpoint_add(300000, 65);
        fail_mode = modes[m];
        rd_close();
        fail_mode = FAIL_NONE;
        assert(access("../.idx/idx-large.txt.bin.tmp", F_OK) != 0);
        uint8_t now[32768];
        size_t now_len = read_file("../.idx/idx-large.txt.bin", now, sizeof(now));
        assert(now_len == baseline_len);
        assert(memcmp(now, baseline, baseline_len) == 0);
    }

    assert(rd_open_file("idx-large.txt") == 0);
    rd_reset_page_cache();
    rd_test_page_checkpoint_add(0, 1);
    rd_test_page_checkpoint_add(400000, 80);
    rd_close();
    assert(access("../.idx/idx-large.txt.bin.tmp", F_OK) != 0);
    size_t final_len = read_file("../.idx/idx-large.txt.bin", baseline, sizeof(baseline));
    assert(final_len > 48);

    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);
    puts("index fault tests passed");
    return 0;
}
