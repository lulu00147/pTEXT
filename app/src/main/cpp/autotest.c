#include "autotest.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "reader.h"

#define AUTOTEST_PAGES 100

static FILE *s_auto_log;
static FILE *s_auto_ext_log;
static int32_t s_auto_active;
static int32_t s_auto_done;
static int32_t s_auto_pages;
static int64_t s_auto_start_ns;
static int64_t s_auto_draw_ns;
static int64_t s_auto_total_ns;
static int64_t s_auto_min_ns;
static int64_t s_auto_max_ns;

static int64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void auto_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (s_auto_log) {
        fputs(buf, s_auto_log);
        fflush(s_auto_log);
    }
    if (s_auto_ext_log) {
        fputs(buf, s_auto_ext_log);
        fflush(s_auto_ext_log);
    }
}

static void auto_open_log(struct android_app *app)
{
    if (s_auto_log || !app || !app->activity) return;
    const char *internal = app->activity->internalDataPath;
    const char *external = app->activity->externalDataPath;
    char path[1024];
    if (internal) {
        snprintf(path, sizeof(path), "%s/files/autotest.log", internal);
        s_auto_log = fopen(path, "w");
    }
    if (external) {
        mkdir(external, 0755);
        snprintf(path, sizeof(path), "%s/autotest.log", external);
        s_auto_ext_log = fopen(path, "w");
    }
    auto_log("autotest log start\n");
}

static void auto_close_log(void)
{
    if (s_auto_log) {
        fflush(s_auto_log);
        fclose(s_auto_log);
        s_auto_log = NULL;
    }
    if (s_auto_ext_log) {
        fflush(s_auto_ext_log);
        fclose(s_auto_ext_log);
        s_auto_ext_log = NULL;
    }
}

static void auto_finish(struct android_app *app)
{
    if (s_auto_done) return;
    s_auto_done = 1;
    int64_t elapsed = monotonic_ns() - s_auto_start_ns;
    int64_t avg = s_auto_pages > 0 ? s_auto_total_ns / s_auto_pages : 0;
    auto_log("summary pages=%d total_ms=%lld avg_ms=%.3f min_ms=%.3f max_ms=%.3f\n",
             s_auto_pages, (long long)(elapsed / 1000000LL),
             (double)avg / 1000000.0,
             (double)s_auto_min_ns / 1000000.0,
             (double)s_auto_max_ns / 1000000.0);
    auto_close_log();
    ANativeActivity_finish(app->activity);
}

int32_t autotest_begin(struct android_app *app)
{
    if (s_auto_active || s_auto_done) return 0;
    int32_t count = 0;
    FileItem *files = rd_get_files(&count);
    const char *wanted = "樱树抽芽时，想你.txt";
    const char *path = NULL;
    for (int32_t i = 0; i < count; i++) {
        if (files[i].name[0] && strstr(files[i].name, wanted)) {
            path = files[i].path;
            break;
        }
    }
    auto_open_log(app);
    if (!path) {
        auto_log("file not found: %s\n", wanted);
        auto_finish(app);
        return 0;
    }
    if (rd_open_file(path) != 0) {
        auto_log("open failed: %s\n", path);
        auto_finish(app);
        return 0;
    }
    s_auto_active = 1;
    s_auto_pages = 0;
    s_auto_start_ns = monotonic_ns();
    auto_log("opened %s len=%lld page=%d\n", path,
             (long long)rd_get_file_len(), rd_get_page_no());
    return 1;
}

void autotest_draw_start(void)
{
    if (s_auto_active && !s_auto_done) s_auto_draw_ns = monotonic_ns();
}

int32_t autotest_after_draw(struct android_app *app)
{
    if (!s_auto_active || s_auto_done) return 0;
    int64_t now = monotonic_ns();
    int64_t dt = s_auto_draw_ns > 0 ? now - s_auto_draw_ns : 0;
    s_auto_draw_ns = 0;
    s_auto_pages++;
    s_auto_total_ns += dt;
    if (dt < s_auto_min_ns || s_auto_min_ns == 0) s_auto_min_ns = dt;
    if (dt > s_auto_max_ns) s_auto_max_ns = dt;
    auto_log("page=%d draw_ms=%.3f\n", rd_get_page_no(),
             (double)dt / 1000000.0);
    if (s_auto_pages >= AUTOTEST_PAGES || !rd_can_next()) {
        auto_finish(app);
        return 0;
    }
    return 1;
}
