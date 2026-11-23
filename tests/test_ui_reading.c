#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifdef PTEXT_MT
#include <pthread.h>
#endif

#include "reader.h"
#include "render_host.h"
#include "ui_reading.h"

static Page kPage;
static PageLayout kLayout = {20, 53, 10, 24, 560, 800};
static int64_t kFileLen = 100;
static int32_t kPageNo = 2;
static int32_t kBookmarked;
static int32_t kPreloadOk = 1;
static int32_t kBookNameNull;
static char kBookName[128] = "book";
static _Atomic int32_t clear_calls;
static _Atomic int32_t text_calls;
static _Atomic int32_t justified_calls;
static _Atomic int32_t size_text_calls;
static _Atomic int32_t preload_calls;
static _Atomic int32_t bg_on_calls;
static _Atomic int32_t bg_off_calls;
#ifdef PTEXT_MT
static int32_t fail_threads;
int __real_pthread_create(pthread_t *, const pthread_attr_t *,
                          void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg)
{
    if (fail_threads) return 11; /* EAGAIN */
    return __real_pthread_create(thread, attr, start_routine, arg);
}
#endif

float r_ui_scale(void) { return 1.0f; }
int32_t r_w(void) { return 600; }
int32_t r_h(void) { return 800; }
int32_t r_font_h(void) { return 20; }
int32_t r_char_advance(uint32_t cp) { return cp < 0x80 ? 10 : 20; }
int32_t r_text_width(const uint32_t *s, int32_t n)
{
    int32_t w = 0;
    for (int32_t i = 0; i < n; i++) w += r_char_advance(s[i]);
    return w;
}
int32_t r_text_width_at_size(const uint32_t *s, int32_t n, float size)
{
    (void)size;
    return r_text_width(s, n);
}
void r_clear(void) { atomic_fetch_add(&clear_calls, 1); }
void r_draw_text(int32_t x, int32_t y, const uint32_t *s, int32_t n)
{
    (void)x; (void)y; (void)s; (void)n;
    atomic_fetch_add(&text_calls, 1);
}
void r_set_bg_only(int32_t on)
{
    if (on) atomic_fetch_add(&bg_on_calls, 1);
    else atomic_fetch_add(&bg_off_calls, 1);
}
void r_draw_text_justified(int32_t x, int32_t y, const uint32_t *s, int32_t n,
                           int32_t target_width)
{
    (void)x; (void)y; (void)s; (void)n; (void)target_width;
    atomic_fetch_add(&justified_calls, 1);
}
void r_draw_text_at_size_color(int32_t x, int32_t y, const uint32_t *s,
                               int32_t n, float size, uint16_t color)
{
    (void)x; (void)y; (void)s; (void)n; (void)size; (void)color;
    atomic_fetch_add(&size_text_calls, 1);
}
void r_prepare_parallel(void) {}
int32_t r_preload_text(const uint32_t *s, int32_t n)
{
    (void)s;
    atomic_fetch_add(&preload_calls, 1);
    return kPreloadOk && n >= 0;
}
int32_t r_preload_cp(uint32_t cp)
{
    (void)cp;
    atomic_fetch_add(&preload_calls, 1);
    return kPreloadOk;
}
void r_fast_begin(void) {}
int r_fast_prepare(void) { return 1; }
void r_fast_end(void) {}
void affinity_bind_current(void) {}

int64_t rd_get_file_len(void) { return kFileLen; }
Page *rd_get_page(void) { return &kPage; }
PageLayout rd_get_layout(void) { return kLayout; }
const char *rd_get_book_name(void) { return kBookNameNull ? NULL : kBookName; }
int32_t rd_get_page_no(void) { return kPageNo; }
int32_t rd_page_bookmarked(void) { return kBookmarked; }
int32_t rd_line_blank(const Line *line) { return line->len == 0; }

int main(void)
{
    memset(&kPage, 0, sizeof(kPage));
    kPage.count = 2;
    kPage.start_offset = 50;
    kPage.lines[0].len = 3;
    kPage.lines[0].text[0] = 'A';
    kPage.lines[0].text[1] = 0x4E2D;
    kPage.lines[0].text[2] = 'B';
    kPage.lines[1].len = 0;

    ui_draw_reading();
    assert(clear_calls == 1);
    assert(text_calls > 0);
    assert(size_text_calls > 0);
    assert(atomic_load(&bg_on_calls) > 0);
    assert(atomic_load(&bg_on_calls) == atomic_load(&bg_off_calls));

    kBookmarked = 1;
    ui_draw_reading();
    assert(size_text_calls > 0);

    kPage.lines[0].justify = 1;
    int32_t justified_before = justified_calls;
    ui_draw_reading();
    assert(justified_calls > justified_before);

    kPage.count = 0;
    ui_draw_reading();

    kPageNo = 0;
    kPage.start_offset = 0;
    kPage.count = 1;
    kPage.lines[0].len = 1;
    kPage.lines[0].text[0] = 'X';
    ui_draw_reading();

    int32_t clear_before_empty = clear_calls;
    kFileLen = 0;
    ui_draw_reading();
    assert(clear_calls == clear_before_empty);

    kFileLen = 100;
    kPage.count = 1;
    kPage.lines[0].len = 1;
    kPage.lines[0].text[0] = 'Y';
    kPreloadOk = 0;
    int32_t preload_before = preload_calls;
    ui_draw_reading();
#ifdef PTEXT_MT
    assert(preload_calls > preload_before);
#else
    assert(preload_calls == preload_before);
#endif

    memset(kBookName, 'T', sizeof(kBookName) - 1);
    kBookName[sizeof(kBookName) - 1] = '\0';
    kPreloadOk = 1;
    ui_draw_reading();
    snprintf(kBookName, sizeof(kBookName), "book");

    kLayout.margin_top = 900;
    ui_draw_reading();
    kLayout.margin_top = 53;

    kBookName[0] = '\0';
    kBookNameNull = 1;
    kPage.count = 1;
    kPage.lines[0].len = 1;
    kPage.lines[0].text[0] = 'Z';
    ui_draw_reading();
    kBookNameNull = 0;
    snprintf(kBookName, sizeof(kBookName), "book");

#ifdef PTEXT_MT
    kPreloadOk = 1;
    fail_threads = 1;
    ui_draw_reading();
    fail_threads = 0;
#endif

    puts("ui reading tests passed");
    return 0;
}
