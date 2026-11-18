#include "ui_reading.h"
#include "reader.h"
#include "render.h"
#include "ui_util.h"

#include <stdio.h>
#include <time.h>
#ifdef PTEXT_MT
#include <pthread.h>
#endif

#define UI_LEN(arr) (sizeof(arr)/sizeof(uint32_t) - 1)
#define HEADER_MARGIN_X ui_px(14)
#define FOOTER_MARGIN_X ui_px(13)

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
    const BodyJob *job = (const BodyJob *)arg;
    for (int32_t i = job->start; i < job->end; i++) {
        draw_body_item(&job->items[i], job->pl);
    }
    return NULL;
}
#endif

void ui_draw_reading(void)
{
    if (!rd_get_file_len()) return;
    r_clear();

    float hs = (float)ui_px(15);
    float hts = (float)ui_px(14);
    float fs = (float)ui_px(14);
    const uint16_t hf_color = 0x630C;
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
    for (int32_t i = 0; i < item_count; i++) {
        preload_ok &= r_preload_text(items[i].line->text, items[i].line->len);
    }
    r_prepare_parallel();
    if (preload_ok) {
        int32_t split = item_count / 2;
        BodyJob job0 = {items, 0, split, &pl};
        BodyJob job1 = {items, split, item_count, &pl};
        pthread_t t0, t1;
        int s0 = pthread_create(&t0, NULL, draw_body_thread, &job0);
        int s1 = pthread_create(&t1, NULL, draw_body_thread, &job1);
        if (s0 != 0) draw_body_thread(&job0);
        if (s1 != 0) draw_body_thread(&job1);
        if (s0 == 0) pthread_join(t0, NULL);
        if (s1 == 0) pthread_join(t1, NULL);
    } else {
        for (int32_t i = 0; i < item_count; i++) draw_body_item(&items[i], &pl);
    }
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
}
