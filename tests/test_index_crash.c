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
#include <sys/wait.h>
#include <unistd.h>

#include "reader.h"
#include "render_host.h"

enum { CRASH_NONE = 0, CRASH_AFTER_WRITE, CRASH_AFTER_FSYNC, CRASH_AFTER_RENAME };

static int crash_phase;
static int idx_fd = -1;

static int is_idx_tmp(const char *path)
{
    return path && strstr(path, ".idx/") && strstr(path, ".tmp");
}

static int is_idx_target(const char *path)
{
    return path && strstr(path, ".idx/") && strstr(path, ".bin");
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
    int fd = (flags & O_CREAT) ? __real_open(path, flags, mode)
                               : __real_open(path, flags);
    if (fd >= 0 && is_idx_tmp(path)) idx_fd = fd;
    return fd;
}

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
    ssize_t n = __real_write(fd, buf, count);
    if (fd == idx_fd && crash_phase == CRASH_AFTER_WRITE) _exit(77);
    return n;
}

int __wrap_fsync(int fd)
{
    int n = __real_fsync(fd);
    if (fd == idx_fd && crash_phase == CRASH_AFTER_FSYNC) _exit(77);
    return n;
}

int __wrap_close(int fd)
{
    int n = __real_close(fd);
    if (fd == idx_fd) idx_fd = -1;
    return n;
}

int __wrap_rename(const char *oldpath, const char *newpath)
{
    int n = __real_rename(oldpath, newpath);
    if (n == 0 && crash_phase == CRASH_AFTER_RENAME && is_idx_target(newpath)) {
        _exit(77);
    }
    return n;
}

static void write_text(void)
{
    const char *unit = "索引崩溃一致性测试，正文。下一句。\n";
    FILE *f = fopen("idx-crash.txt", "wb");
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
    if (!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static void prepare_child(void)
{
    assert(rd_open_file("idx-crash.txt") == 0);
    rd_reset_page_cache();
    rd_test_page_checkpoint_add(0, 1);
    rd_test_page_checkpoint_add(300000, 65);
    rd_test_page_checkpoint_add(600000, 129);
}

static void run_crash_child(int phase)
{
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        crash_phase = phase;
        prepare_child();
        rd_close();
        _exit(90);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 77);
    crash_phase = CRASH_NONE;
    idx_fd = -1;
}

int main(void)
{
    char dir[] = "/tmp/ptext-index-crash-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    rd_init(dir);
    char files[512];
    snprintf(files, sizeof(files), "%s/files", dir);
    assert(chdir(files) == 0);
    write_text();

    assert(rd_open_file("idx-crash.txt") == 0);
    for (int i = 0; i < 200; i++) rd_next_page();
    rd_close();

    uint8_t baseline[32768];
    size_t baseline_len = read_file("../.idx/idx-crash.txt.bin", baseline, sizeof(baseline));
    assert(baseline_len > 48);

    run_crash_child(CRASH_AFTER_WRITE);
    uint8_t after_write[32768];
    size_t write_len = read_file("../.idx/idx-crash.txt.bin", after_write, sizeof(after_write));
    assert(write_len == baseline_len);
    assert(memcmp(after_write, baseline, baseline_len) == 0);

    run_crash_child(CRASH_AFTER_FSYNC);
    uint8_t after_fsync[32768];
    size_t fsync_len = read_file("../.idx/idx-crash.txt.bin", after_fsync, sizeof(after_fsync));
    assert(fsync_len == baseline_len);
    assert(memcmp(after_fsync, baseline, baseline_len) == 0);

    run_crash_child(CRASH_AFTER_RENAME);
    uint8_t after_rename[32768];
    size_t rename_len = read_file("../.idx/idx-crash.txt.bin", after_rename, sizeof(after_rename));
    assert(rename_len > 48);
    assert(rename_len != baseline_len || memcmp(after_rename, baseline, rename_len) != 0);

    assert(rd_open_file("idx-crash.txt") == 0);
    int64_t off = -1;
    int32_t page = -1;
    rd_reset_page_cache();
    assert(rd_test_idx_lookup(rd_get_file_len() - 1, &off, &page) > 0);
    assert(off >= 0 && off < rd_get_file_len());
    assert(page > 0);
    rd_close();

    (void)unlink("../.idx/idx-crash.txt.bin.tmp");
    (void)unlink("../.idx/idx-crash.txt.bin");
    (void)unlink("../.prog/idx-crash.txt.bin");
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
    assert(system(cleanup) == 0);
    puts("index crash tests passed");
    return 0;
}
