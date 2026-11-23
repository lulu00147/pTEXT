#include "ui_reading.h"
#include "reader.h"
#ifdef PTEXT_HOST_TEST
#include "render_host.h"
#else
#include "render.h"
#endif
#include "ui_util.h"
#include "affinity.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef PTEXT_MT
#include <pthread.h>
#endif

#define UI_LEN(arr) (sizeof(arr)/sizeof(uint32_t) - 1)
#define HEADER_MARGIN_X ui_px(14)
#define FOOTER_MARGIN_X ui_px(13)
#define COLOR_HEADER_TEXT 0x630Cu

#ifdef PTEXT_MT
typedef struct {
    const Line *line;
    int32_t y;
} BodyItem;

typedef struct {
    const BodyItem *items;
    int32_t start;
    int32_t end;
    const PageLayout *pl;
} BodyJob;

static void draw_body_item(const BodyItem *it, const PageLayout *pl)
{
    int32_t x = pl->margin_x + it->line->indent;
    int32_t target = pl->text_width - it->line->indent;
    if (it->line->justify && it->line->len > 1) {
        r_draw_text_justified(x, it->y, it->line->text, it->line->len, target);
    } else {
        r_draw_text(x, it->y, it->line->text, it->line->len);
    }
}

static void *draw_body_thread(void *arg)
{
    affinity_bind_current();
    const BodyJob *job = (const BodyJob *)arg;
    for (int32_t i = job->start; i < job->end; i++) {
        draw_body_item(&job->items[i], job->pl);
    }
    return NULL;
}

static void body_split(const BodyItem *items, int32_t count,
                       int32_t *split1, int32_t *split2)
{
    if (count < 3) {
        *split1 = count / 3;
        *split2 = (*split1) * 2;
        return;
    }

    int64_t total = 0;
    for (int32_t i = 0; i < count; i++) total += items[i].line->len + 1;

    int64_t acc = 0;
    int32_t a = 0, b = 0;
    for (int32_t i = 0; i < count; i++) {
        acc += items[i].line->len + 1;
        if (a == 0 && acc * 3 >= total) a = i + 1;
        if (b == 0 && acc * 3 >= total * 2) b = i + 1;
    }

    if (a > count - 1) a = count - 1;
    if (b <= a) b = a + 1;
    if (b > count - 1) b = count - 1;
    *split1 = a;
    *split2 = b;
}

#define PAGE_SEEN_SLOTS 1024
static uint32_t s_page_seen_key[PAGE_SEEN_SLOTS];
static uint32_t s_page_seen_gen[PAGE_SEEN_SLOTS];
static uint32_t s_page_gen;

#ifdef PTEXT_HOST_TEST
void ui_test_reset_page_gen(void)
{
    s_page_gen = 0xFFFFFFFFu;
}
#endif
#endif

void ui_draw_reading(void)
{
    if (!rd_get_file_len()) return;
    r_clear();

    float hs = (float)ui_px(15);
    float hts = (float)ui_px(14);
    float fs = (float)ui_px(14);
    const uint16_t hf_color = COLOR_HEADER_TEXT;
    const char *book = rd_get_book_name();
    uint32_t title[128];
    int32_t tn = ui_utf8_to_utf32(title, 127, book ? book : "");
    char clock[16];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    snprintf(clock, sizeof(clock), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    uint32_t utime[16];
    int32_t un = ui_utf8_to_utf32(utime, 15, clock);
    int32_t tw = r_text_width_at_size(utime, un, hts);
    int32_t max_title_w = r_w() - HEADER_MARGIN_X * 2 - tw - ui_px(12);
    while (tn > 0 && r_text_width_at_size(title, tn, hs) > max_title_w) tn--;
    r_draw_text_at_size_color(HEADER_MARGIN_X, ui_px(28), title, tn, hs, hf_color);
    r_draw_text_at_size_color(r_w() - tw - HEADER_MARGIN_X, ui_px(28), utime, un, hts, hf_color);

    Page *p = rd_get_page();
    PageLayout pl = rd_get_layout();
    int32_t footer_zone_top = pl.page_height - pl.margin_bot;
    int32_t fy = r_h() - ui_px(6);

    char foot[64];
    int32_t page_no = rd_get_page_no();
    if (page_no > 0) snprintf(foot, sizeof(foot), "第 %d 页", page_no);
    else snprintf(foot, sizeof(foot), "页码 --");
    uint32_t utxt[64];
    int32_t n = ui_utf8_to_utf32(utxt, 63, foot);
    r_draw_text_at_size_color(FOOTER_MARGIN_X, fy, utxt, n, fs, hf_color);

    float pct = (float)p->start_offset / (float)rd_get_file_len() * 100.0f;
    char pct_buf[32];
    snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", pct);
    n = ui_utf8_to_utf32(utxt, 63, pct_buf);
    r_draw_text_at_size_color(r_w() - r_text_width_at_size(utxt, n, fs) - FOOTER_MARGIN_X,
                              fy, utxt, n, fs, hf_color);

    if (rd_page_bookmarked()) {
        const uint32_t bm[] = {0x2A};
        int32_t bw = r_text_width_at_size(bm, 1, fs);
        r_draw_text_at_size_color((r_w() - bw) / 2, fy, bm, 1, fs, hf_color);
    }

    int32_t vstep_x10 = pl.line_height * 10 + 7;
    r_set_bg_only(1);
#ifdef PTEXT_MT
    BodyItem items[80];
    int32_t item_count = 0;
    int32_t draw_i = 0;
    for (int32_t i = 0; i < p->count; i++) {
        Line *line = &p->lines[i];
        if (rd_line_blank(line)) continue;
        int32_t y = pl.margin_top + draw_i * vstep_x10 / 10;
        if (y > footer_zone_top) break;
        items[item_count].line = line;
        items[item_count].y = y;
        item_count++;
        draw_i++;
    }
    int32_t preload_ok = 1;
    s_page_gen++;
    if (s_page_gen == 0) {
        memset(s_page_seen_gen, 0, sizeof(s_page_seen_gen));
        s_page_gen = 1;
    }
#ifdef PTEXT_MT
    r_fast_begin();
#endif
    for (int32_t i = 0; i < item_count; i++) {
        const Line *line = items[i].line;
        for (int32_t j = 0; j < line->len; j++) {
            uint32_t cp = line->text[j];
            uint32_t slot = (cp * 2654435761u) & (PAGE_SEEN_SLOTS - 1);
            if (s_page_seen_gen[slot] == s_page_gen &&
                s_page_seen_key[slot] == cp) continue;
            s_page_seen_gen[slot] = s_page_gen;
            s_page_seen_key[slot] = cp;
            preload_ok &= r_preload_cp(cp);
        }
    }
#ifdef PTEXT_MT
    int32_t fast_ok = preload_ok && r_fast_prepare();
    (void)fast_ok;
#endif
    r_prepare_parallel();
    if (preload_ok) {
        int32_t split1, split2;
        body_split(items, item_count, &split1, &split2);
        BodyJob job0 = {items, 0, split1, &pl};
        BodyJob job1 = {items, split1, split2, &pl};
        BodyJob job2 = {items, split2, item_count, &pl};
        pthread_t t1, t2;
        int s1 = pthread_create(&t1, NULL, draw_body_thread, &job1);
        int s2 = pthread_create(&t2, NULL, draw_body_thread, &job2);
        draw_body_thread(&job0);
        if (s1 == 0) pthread_join(t1, NULL); else draw_body_thread(&job1);
        if (s2 == 0) pthread_join(t2, NULL); else draw_body_thread(&job2);
    } else {
        for (int32_t i = 0; i < item_count; i++) draw_body_item(&items[i], &pl);
    }
#ifdef PTEXT_MT
    r_fast_end();
#endif
#else
    int32_t draw_i = 0;
    for (int32_t i = 0; i < p->count; i++) {
        Line *line = &p->lines[i];
        if (rd_line_blank(line)) continue;
        int32_t x = pl.margin_x + line->indent;
        int32_t target = pl.text_width - line->indent;
        int32_t y = pl.margin_top + draw_i * vstep_x10 / 10;
        if (y > footer_zone_top) break;
        if (line->justify && line->len > 1) {
            r_draw_text_justified(x, y, line->text, line->len, target);
        } else {
            r_draw_text(x, y, line->text, line->len);
        }
        draw_i++;
    }
#endif
    r_set_bg_only(0);
}
