#include <android/native_activity.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/choreographer.h>
#include <android/configuration.h>
#include "android_native_app_glue.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "render.h"
#include "reader.h"
#include "utf8.h"
#include <android/log.h>

#define ST_FILELIST 0
#define ST_READING  1
#define ST_SETTINGS 2
#define ST_BMARKS   3
#define ST_DELETE   4

static int32_t ui(int32_t px)
{
    return (int32_t)(px * r_ui_scale() + 0.5f);
}

/* 布局常量，单位 dp */
#define LX_MARGIN       ui(30)   /* 水平边距 */
#define LY_TITLE        ui(50)   /* 标题 Y */
#define LY_ITEM0        ui(120)  /* 第一个列表项 Y */
#define ITEM_H          ui(80)   /* 文件列表项高度 */
#define BM_ITEM_H       ui(60)   /* 书签列表项高度 */
#define LY_BOTTOM_BTN   ui(80)   /* 底部按钮距底边距 */
#define BOTTOM_BTN_ZONE ui(100)  /* 底部按钮区域高度 */
#define HEADER_MARGIN_X ui(14)  /* Legado 页眉左右边距 */
#define BODY_MARGIN_X   ui(5)   /* Legado 正文左右边距 */
#define FOOTER_MARGIN_X ui(13)  /* Legado 页脚左右边距 */
#define READ_TEXT_Y0    ui(53)  /* 阅读页正文起始 Y */
#define SETTINGS_W      ui(360)  /* 设置面板宽 */
#define SETTINGS_H      ui(240)  /* 设置面板高 */
#define SETTINGS_ROW_H  ui(50)   /* 设置行高 */
#define SETTINGS_TITLE_H ui(20)  /* 标题内边距 */
#define DELETE_W        ui(320)
#define DELETE_H        ui(200)

static struct android_app *s_app;
static int32_t s_state = ST_FILELIST;
static int32_t s_need_redraw = 1;
static int32_t s_last_min = -1;
static int64_t s_center_down_ns = 0;
static int32_t s_center_long_fired = 0;
static int32_t s_file_scroll = 0;
static int32_t s_file_touch_active = 0;
static int32_t s_file_dragging = 0;
static int32_t s_file_down_y = 0;
static int32_t s_file_down_scroll = 0;
static int64_t s_file_hold_start_ns = 0;
static int32_t s_file_hold_idx = -1;
static int32_t s_file_hold_fired = 0;
static int32_t s_delete_idx = -1;
static int32_t s_file_error = 0;
static Bookmark s_bm_view[50];
static int32_t s_bm_view_count = 0;
static int32_t s_bm_scroll = 0;
static int32_t s_bm_touch_active = 0;
static int32_t s_bm_dragging = 0;
static int32_t s_bm_down_y = 0;
static int32_t s_bm_down_scroll = 0;
static char s_bm_path[512];
void pickFile(void);
int32_t takeRescanFlag(void);

static AChoreographer *s_choreographer = NULL;
static int32_t s_vsync_posted = 0;

static void vsync_prefetch_cb(int64_t frameTimeNanos, void* data)
{
    (void)frameTimeNanos; (void)data;
    s_vsync_posted = 0;
    if (rd_is_prefetching()) {
        rd_prefetch_step();
        if (rd_is_prefetching()) {
            AChoreographer_postFrameCallback64(s_choreographer,
                                               vsync_prefetch_cb, NULL);
            s_vsync_posted = 1;
        }
    }
}

static void trigger_vsync_prefetch(void)
{
    if (!s_vsync_posted && s_choreographer) {
        AChoreographer_postFrameCallback64(s_choreographer,
                                           vsync_prefetch_cb, NULL);
        s_vsync_posted = 1;
    }
}

static const uint32_t UI_TITLE[] = {0x70,0x54,0x45,0x58,0x54,0};
static const uint32_t UI_EMPTY[] = {0x6682,0x65E0,0x56FE,0x4E66,0xFF0C,0x70B9,
                                    0x51FB,0x4E0B,0x65B9,0x5BFC,0x5165,0};
static const uint32_t UI_IMPORT[] = {0x5B,0x20,0x2B,0x20,0x5D,0};
static const uint32_t UI_SETTINGS_TITLE[] = {0x2D,0x2D,0x20,0x9605,0x8BFB,0x8BBE,
                                             0x7F6E,0x20,0x2D,0x2D,0};
static const uint32_t UI_BOOKMARKS[] = {0x2A,0};                                  /* 书签标记 */
static const uint32_t UI_BM_TITLE[] = {0x2D,0x2D,0x20,0x4E66,0x7B7E,0x5217,
                                       0x8868,0x20,0x2D,0x2D,0};
static const uint32_t UI_BM_EMPTY[] = {0x6682,0x65E0,0x4E66,0x7B7E,0};
static const uint32_t UI_BM_MARK[] = {0x5B,0x20,0x2A,0x20,0x5D,0};
static const uint32_t UI_OPEN_FAIL[] = {0x65E0,0x6CD5,0x6253,0x5F00,0x6587,0x4EF6};
static const uint32_t UI_DELETE_FAIL[] = {0x5220,0x9664,0x5931,0x8D25};
#define UI_LEN(arr) (sizeof(arr)/sizeof(uint32_t) - 1)

static int32_t clamp_i(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* UTF-8 -> UTF-32；非法/截断序列输出 U+FFFD。 */
static int32_t utf8_to_utf32(uint32_t *dst, int32_t max, const char *src)
{
    if (!dst || !src || max <= 0) return 0;
    const uint8_t *p = (const uint8_t *)src;
    const uint8_t *end = p + strlen(src);
    int32_t n = 0;

    while (n < max && p < end) {
        uint32_t cp = 0;
        Utf8Result result = utf8_next(&p, end, &cp);
        if (result == UTF8_END) break;
        dst[n++] = result == UTF8_OK ? cp : 0xFFFD;
    }

    return n;
}

static int32_t file_index_at(int32_t y)
{
    int32_t count;
    rd_get_files(&count);
    int32_t list_top = LY_ITEM0 - ITEM_H / 2;
    int32_t iy = y + s_file_scroll;
    if (count <= 0 || iy < list_top || iy >= list_top + count * ITEM_H) return -1;
    int32_t idx = (iy - list_top) / ITEM_H;
    return idx >= 0 && idx < count ? idx : -1;
}

static int32_t file_list_max_scroll(int32_t count)
{
    int32_t top = LY_ITEM0 - ITEM_H / 2;
    int32_t bottom = r_h() - BOTTOM_BTN_ZONE;
    int32_t visible = bottom - top;
    if (visible < ITEM_H) visible = ITEM_H;
    int32_t max_scroll = count * ITEM_H - visible;
    return max_scroll > 0 ? max_scroll : 0;
}

static void draw_file_list(void)
{
    r_clear();
    int32_t count;
    FileItem *files = rd_get_files(&count);
    int32_t max_scroll = file_list_max_scroll(count);
    if (s_file_scroll > max_scroll) s_file_scroll = max_scroll;
    if (s_file_scroll < 0) s_file_scroll = 0;

    r_draw_text(LX_MARGIN, LY_TITLE, UI_TITLE, UI_LEN(UI_TITLE));
    int32_t list_top = LY_ITEM0 - ITEM_H / 2;
    int32_t list_bottom = r_h() - BOTTOM_BTN_ZONE;
    int32_t max_text_w = r_w() - LX_MARGIN * 2;
    int32_t bm_w = r_text_width(UI_BM_MARK, UI_LEN(UI_BM_MARK));

    if (count == 0) {
        r_draw_text(LX_MARGIN, LY_ITEM0 + ITEM_H / 2, UI_EMPTY, UI_LEN(UI_EMPTY));
    } else {
        for (int32_t i = 0; i < count; i++) {
            int32_t row_y = LY_ITEM0 + i * ITEM_H - s_file_scroll;
            int32_t y = row_y + (ITEM_H - r_font_h()) / 2;
            if (row_y + ITEM_H <= list_top || row_y >= list_bottom) continue;
            if (y < list_top || y + r_font_h() > list_bottom) continue;

            int32_t row_max_w = max_text_w;
            if (files[i].bookmarks > 0) row_max_w -= bm_w + ui(16);

            uint32_t txt[256];
            int32_t n = utf8_to_utf32(txt, 255, files[i].name);
            int32_t full_n = n;
            while (n > 1 && r_text_width(txt, n) > row_max_w) n--;
            if (full_n > n && n > 0) txt[n - 1] = 0x2026; /* … */
            r_draw_text(LX_MARGIN, y, txt, n);

            if (files[i].bookmarks > 0) {
                r_draw_text(r_w() - bm_w - LX_MARGIN, y,
                            UI_BM_MARK, UI_LEN(UI_BM_MARK));
            }
        }
    }
    int32_t import_w = r_text_width(UI_IMPORT, UI_LEN(UI_IMPORT));
    r_draw_text(r_w() - import_w - LX_MARGIN, r_h() - LY_BOTTOM_BTN,
                UI_IMPORT, UI_LEN(UI_IMPORT));
    if (s_file_error == 1) {
        r_draw_text(LX_MARGIN, r_h() - BOTTOM_BTN_ZONE,
                    UI_OPEN_FAIL, UI_LEN(UI_OPEN_FAIL));
    } else if (s_file_error == 2) {
        r_draw_text(LX_MARGIN, r_h() - BOTTOM_BTN_ZONE,
                    UI_DELETE_FAIL, UI_LEN(UI_DELETE_FAIL));
    }
}

static void get_delete_rect(int32_t *l, int32_t *t, int32_t *r, int32_t *b)
{
    *l = r_w() / 2 - DELETE_W / 2;
    *t = r_h() / 2 - DELETE_H / 2;
    *r = *l + DELETE_W;
    *b = *t + DELETE_H;
}

static void draw_delete_dialog(void)
{
    int32_t count;
    FileItem *files = rd_get_files(&count);
    if (s_delete_idx < 0 || s_delete_idx >= count) return;

    int32_t l, t, r, b;
    get_delete_rect(&l, &t, &r, &b);
    r_clear_rect(l, t, r, b);
    r_stroke_rect(l, t, r, b, ui(2));

    const uint32_t title[] = {0x5220,0x9664,0x6587,0x4EF6,0xFF1F};
    const uint32_t cancel[] = {0x53D6,0x6D88};
    const uint32_t del[] = {0x5220,0x9664};
    r_draw_text(l + ui(24), t + ui(28), title, 5);

    uint32_t name[256];
    int32_t n = utf8_to_utf32(name, 255, files[s_delete_idx].name);
    int32_t full_n = n;
    int32_t maxw = (r - l) - ui(48);
    while (n > 1 && r_text_width(name, n) > maxw) n--;
    if (full_n > n && n > 0) name[n - 1] = 0x2026;
    r_draw_text(l + ui(24), t + ui(90), name, n);

    r_draw_text(l + ui(40), b - ui(56), cancel, 2);
    int32_t dw = r_text_width(del, 2);
    r_draw_text(r - dw - ui(40), b - ui(56), del, 2);
}

static int32_t bm_list_max_scroll(void)
{
    int32_t top = LY_ITEM0 - BM_ITEM_H / 2;
    int32_t bottom = r_h() - BOTTOM_BTN_ZONE;
    int32_t visible = bottom - top;
    if (visible < BM_ITEM_H) visible = BM_ITEM_H;
    int32_t max_scroll = s_bm_view_count * BM_ITEM_H - visible;
    return max_scroll > 0 ? max_scroll : 0;
}

static void draw_bookmarks(void)
{
    r_clear();
    r_draw_text(LX_MARGIN, LY_TITLE, UI_BM_TITLE, UI_LEN(UI_BM_TITLE));
    int32_t max_scroll = bm_list_max_scroll();
    if (s_bm_scroll > max_scroll) s_bm_scroll = max_scroll;
    if (s_bm_scroll < 0) s_bm_scroll = 0;

    int32_t list_top = LY_ITEM0 - BM_ITEM_H / 2;
    int32_t list_bottom = r_h() - BOTTOM_BTN_ZONE;
    int32_t max_text_w = r_w() - LX_MARGIN * 2;

    if (s_bm_view_count == 0) {
        r_draw_text(LX_MARGIN, LY_ITEM0 + BM_ITEM_H / 2, UI_BM_EMPTY, UI_LEN(UI_BM_EMPTY));
    } else {
        for (int32_t i = 0; i < s_bm_view_count; i++) {
            int32_t row_y = LY_ITEM0 + i * BM_ITEM_H - s_bm_scroll;
            int32_t y = row_y + (BM_ITEM_H - r_font_h()) / 2;
            if (row_y + BM_ITEM_H <= list_top || row_y >= list_bottom) continue;
            if (y < list_top || y + r_font_h() > list_bottom) continue;

            char buf[64];
            snprintf(buf, sizeof(buf), "%d. %s", i + 1, s_bm_view[i].preview);
            uint32_t txt[64];
            int32_t n = utf8_to_utf32(txt, 63, buf);
            int32_t full_n = n;
            while (n > 1 && r_text_width(txt, n) > max_text_w) n--;
            if (full_n > n && n > 0) txt[n - 1] = 0x2026;
            r_draw_text(LX_MARGIN, y, txt, n);
        }
    }
}

static int32_t draw_line_blank(const Line *line)
{
    if (line->len == 0) return 1;
    for (int32_t i = 0; i < line->len; i++) {
        uint32_t cp = line->text[i];
        if (cp != ' ' && cp != '\t' && cp != 0x3000) return 0;
    }
    return 1;
}

static void draw_reading(void)
{
    if (!rd_get_file_len()) return;
    r_clear();

    float hs = (float)ui(15);
    float hts = (float)ui(14);
    float fs = (float)ui(14);
    const uint16_t hf_color = 0x630C; /* #606060 in RGB565 */
    const char *book = rd_get_book_name();
    uint32_t title[128];
    int32_t tn = utf8_to_utf32(title, 127, book ? book : "");
    char clock[16];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    snprintf(clock, sizeof(clock), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    uint32_t utime[16];
    int32_t un = utf8_to_utf32(utime, 15, clock);
    int32_t tw = r_text_width_at_size(utime, un, hts);
    int32_t max_title_w = r_w() - HEADER_MARGIN_X * 2 - tw - ui(12);
    while (tn > 0 && r_text_width_at_size(title, tn, hs) > max_title_w) tn--;
    r_draw_text_at_size_color(HEADER_MARGIN_X, ui(28), title, tn, hs, hf_color);
    r_draw_text_at_size_color(r_w() - tw - HEADER_MARGIN_X, ui(28), utime, un, hts, hf_color);

    Page *p = rd_get_page();
    int32_t footer_zone_top = r_h() - ui(10);
    int32_t fy = r_h() - ui(6);

    /* 先画页脚，再画正文；正文不得进入页脚带 */
    r_clear_rect(0, footer_zone_top, r_w(), r_h());
    char foot[64];
    int32_t page_no = rd_get_page_no();
    if (page_no > 0) snprintf(foot, sizeof(foot), "第 %d 页", page_no);
    else snprintf(foot, sizeof(foot), "页码 --");
    uint32_t utxt[64];
    int32_t n = utf8_to_utf32(utxt, 63, foot);
    r_draw_text_at_size_color(FOOTER_MARGIN_X, fy, utxt, n, fs, hf_color);

    float pct = (float)p->start_offset / (float)rd_get_file_len() * 100.0f;
    char pct_buf[32];
    snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", pct);
    n = utf8_to_utf32(utxt, 63, pct_buf);
    r_draw_text_at_size_color(r_w() - r_text_width_at_size(utxt, n, fs) - FOOTER_MARGIN_X,
                              fy, utxt, n, fs, hf_color);

    if (rd_page_bookmarked()) {
        const uint32_t bm[] = {0x4E66, 0x7B7E}; /* 书签 */
        int32_t bw = r_text_width_at_size(bm, 2, fs);
        r_draw_text_at_size_color((r_w() - bw) / 2, fy, bm, 2, fs, hf_color);
    }

    int32_t vstep_x10 = rd_get_line_h() * 10 + 7;
    int32_t draw_i = 0;
    for (int32_t i = 0; i < p->count; i++) {
        Line *line = &p->lines[i];
        if (draw_line_blank(line)) continue;
        int32_t x = BODY_MARGIN_X + line->indent;
        int32_t target = r_w() - BODY_MARGIN_X * 2 - line->indent;
        int32_t y = READ_TEXT_Y0 + draw_i * vstep_x10 / 10;
        if (y > footer_zone_top) break;
        if (line->justify && line->len > 1) {
            r_draw_text_justified(x, y, line->text, line->len, target);
        } else {
            r_draw_text(x, y, line->text, line->len);
        }
        draw_i++;
    }
}

static void get_settings_rect(int32_t *l, int32_t *t, int32_t *r, int32_t *b)
{
    *l = r_w()/2 - SETTINGS_W/2; *t = r_h()/2 - SETTINGS_H/2;
    *r = *l + SETTINGS_W;        *b = *t + SETTINGS_H;
}

static void draw_toggle_buttons(int32_t r, int32_t y, uint32_t left, uint32_t right)
{
    uint32_t lbtn[3] = {'[', left, ']'};
    uint32_t rbtn[3] = {'[', right, ']'};
    r_draw_text(r - ui(130), y, lbtn, 3);
    r_draw_text(r - ui(65), y, rbtn, 3);
}

static void draw_settings(void)
{
    int32_t l, t, r, b;
    get_settings_rect(&l, &t, &r, &b);
    r_clear_rect(l, t, r, b);
    r_stroke_rect(l, t, r, b, ui(2));
    uint32_t utxt[128];
    int32_t n;
    Settings *s = rd_get_settings();
    int32_t font_idx = clamp_i(s->font_idx, 0, 6);
    int32_t indent_idx = clamp_i(s->indent_idx, 0, 2);
    int32_t gap_idx = clamp_i(s->gap_idx, 0, 2);
    int32_t theme = clamp_i(s->theme, 0, 1);
    int32_t y = t + ui(50);
    char buf[64];
    snprintf(buf, sizeof(buf), "字号: %d", 18 + font_idx * 2);
    n = utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l+ui(30), y, utxt, n);
    draw_toggle_buttons(r, y, '-', '+'); y += SETTINGS_ROW_H;
    snprintf(buf, sizeof(buf), "缩进: %d字符", indent_idx * 2);
    n = utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l+ui(30), y, utxt, n);
    draw_toggle_buttons(r, y, '-', '+'); y += SETTINGS_ROW_H;
    const char *gaps[] = {"0", "0.5", "1.0"};
    snprintf(buf, sizeof(buf), "行距: %s", gaps[gap_idx]);
    n = utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l+ui(30), y, utxt, n);
    draw_toggle_buttons(r, y, '-', '+'); y += SETTINGS_ROW_H;
    snprintf(buf, sizeof(buf), "主题: %s", theme ? "日间" : "夜间");
    n = utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l+ui(30), y, utxt, n);
    draw_toggle_buttons(r, y, '-', '+');
}

static int32_t reading_cell(int32_t x, int32_t y)
{
    int32_t w = r_w();
    int32_t h = r_h();
    if (w <= 0 || h <= 0) return 0;
    int32_t col = x * 3 / w;
    int32_t row = y * 3 / h;
    if (col < 0) col = 0;
    if (col > 2) col = 2;
    if (row < 0) row = 0;
    if (row > 2) row = 2;
    return row * 3 + col + 1;
}

static void reading_grid_action(int32_t cell)
{
    if (cell == 1 || cell == 2 || cell == 4 || cell == 7) {
        if (rd_can_prev()) rd_prev_page();
    } else if (cell == 3 || cell == 6 || cell == 8 || cell == 9) {
        if (rd_can_next()) rd_next_page();
    } else {
        s_state = ST_SETTINGS;
    }
}

static void handle_touch_reading(int32_t x, int32_t y)
{
    int32_t l, t, r, b;
    get_settings_rect(&l, &t, &r, &b);
    if (s_state == ST_SETTINGS) {
        if (x < l || x > r || y < t || y > b) {
            s_state = ST_READING;
            s_need_redraw = 1;
            return;
        }
        Settings *s = rd_get_settings();
        int32_t font_idx = clamp_i(s->font_idx, 0, 6);
        int32_t indent_idx = clamp_i(s->indent_idx, 0, 2);
        int32_t gap_idx = clamp_i(s->gap_idx, 0, 2);
        int32_t rel = y - t - ui(50);
        int32_t row = rel >= 0 ? (rel + SETTINGS_ROW_H / 2) / SETTINGS_ROW_H : -1;
        int32_t need_rebuild = 0;
        /* 字号行: [-] [+] */
        if (row == 0) {
            if (x < r - ui(97)) rd_set_font_idx(font_idx > 0 ? font_idx - 1 : 0);
            else rd_set_font_idx(font_idx < 6 ? font_idx + 1 : 6);
            need_rebuild = 1;
        /* 缩进行: [<] [>] */
        } else if (row == 1) {
            if (x < r - ui(97)) rd_set_indent_idx(indent_idx > 0 ? indent_idx - 1 : 0);
            else rd_set_indent_idx(indent_idx < 2 ? indent_idx + 1 : 2);
            need_rebuild = 1;
        /* 行距行: [<] [>] */
        } else if (row == 2) {
            if (x < r - ui(97)) rd_set_gap_idx(gap_idx > 0 ? gap_idx - 1 : 0);
            else rd_set_gap_idx(gap_idx < 2 ? gap_idx + 1 : 2);
            need_rebuild = 1;
        /* 主题行: [夜间] [日间] */
        } else if (row == 3) {
            if (x < r - ui(97)) rd_set_theme(0);
            else rd_set_theme(1);
            s_need_redraw = 1;
        }
        if (need_rebuild) {
            rd_reset_prefetch();
            rd_goto_offset(rd_get_page()->start_offset);
        }
        s_need_redraw = 1; return;
    }
    reading_grid_action(reading_cell(x, y));
    s_need_redraw = 1;
}

/* ---- JNI 相关全局变量 ---- */
static JavaVM *s_jvm = NULL;
static jclass s_main_activity_class_global = NULL;
static jmethodID s_pick_file_mid = NULL;
static jmethodID s_take_rescan_mid = NULL;

static void ensure_jni(void) {
    if (!s_jvm && s_app) {
        s_jvm = s_app->activity->vm;
    }
}

void pickFile(void) {
    __android_log_print(ANDROID_LOG_INFO, "pTEXT", "pickFile() called");
    ensure_jni();
    if (!s_jvm) {
        __android_log_print(ANDROID_LOG_ERROR, "pTEXT", "pickFile: s_jvm is NULL");
        return;
    }
    JNIEnv *env = NULL;
    (*s_jvm)->AttachCurrentThread(s_jvm, &env, NULL);
    if (!env) {
        __android_log_print(ANDROID_LOG_ERROR, "pTEXT", "pickFile: JNIEnv is NULL");
        return;
    }
    if (!s_main_activity_class_global) {
        __android_log_print(ANDROID_LOG_ERROR, "pTEXT", "pickFile: s_main_activity_class_global is NULL");
        return;
    }
    if (!s_pick_file_mid) {
        s_pick_file_mid = (*env)->GetStaticMethodID(env, s_main_activity_class_global,
                                                    "pickFile", "()V");
        if (!s_pick_file_mid) {
            __android_log_print(ANDROID_LOG_ERROR, "pTEXT", "pickFile: method not found");
            (*env)->ExceptionClear(env);
            return;
        }
    }
    (*env)->CallStaticVoidMethod(env, s_main_activity_class_global, s_pick_file_mid);
    __android_log_print(ANDROID_LOG_INFO, "pTEXT", "pickFile: method called successfully");
}

int32_t takeRescanFlag(void) {
    ensure_jni();
    if (!s_jvm) return 0;
    JNIEnv *env = NULL;
    (*s_jvm)->AttachCurrentThread(s_jvm, &env, NULL);
    if (!env || !s_main_activity_class_global) return 0;
    if (!s_take_rescan_mid) {
        s_take_rescan_mid = (*env)->GetStaticMethodID(env, s_main_activity_class_global,
                                                      "takeRescanFlag", "()I");
        if (!s_take_rescan_mid) {
            (*env)->ExceptionClear(env);
            return 0;
        }
    }
    return (*env)->CallStaticIntMethod(env, s_main_activity_class_global, s_take_rescan_mid);
}

static void update_ui_scale(struct android_app *app)
{
    AConfiguration *cfg = AConfiguration_new();
    if (!cfg) return;
    AConfiguration_fromAssetManager(cfg, app->activity->assetManager);
    int32_t density = AConfiguration_getDensity(cfg);
    AConfiguration_delete(cfg);
    if (density <= 0) density = 160;
    r_set_ui_scale((float)density / 160.0f);
}

static void handle_cmd(struct android_app *app, int32_t cmd)
{
    switch (cmd) {
case APP_CMD_INIT_WINDOW:
{
    JNIEnv *env = NULL;
    (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL);
    if (env) {
        if (s_main_activity_class_global) {
            (*env)->DeleteGlobalRef(env, s_main_activity_class_global);
            s_main_activity_class_global = NULL;
            s_pick_file_mid = NULL;
            s_take_rescan_mid = NULL;
        }
        /* activity->clazz 是 Activity 实例，不是 jclass；先取对象类。 */
        jclass clazz = (*env)->GetObjectClass(env, app->activity->clazz);
        if (clazz) {
            s_main_activity_class_global = (*env)->NewGlobalRef(env, clazz);
            (*env)->DeleteLocalRef(env, clazz);
        }
        if (s_main_activity_class_global) {
            __android_log_print(ANDROID_LOG_INFO, "pTEXT", "MainActivity class cached OK");
        } else {
            (*env)->ExceptionClear(env);
            __android_log_print(ANDROID_LOG_ERROR, "pTEXT", "Failed to cache MainActivity class");
        }
    }
    update_ui_scale(app);
    r_init(app->window);
    rd_init(app->activity->internalDataPath);
    rd_scan_dir();
    s_need_redraw = 1;
    s_choreographer = AChoreographer_getInstance();
    break;
}
        case APP_CMD_TERM_WINDOW:
            rd_save_progress();
            /* 释放全局引用 */
            if (s_main_activity_class_global) {
                JNIEnv *env = NULL;
                (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL);
                if (env) {
                    (*env)->DeleteGlobalRef(env, s_main_activity_class_global);
                    s_main_activity_class_global = NULL;
                    s_pick_file_mid = NULL;
                    s_take_rescan_mid = NULL;
                }
            }
            r_deinit(); break;
        case APP_CMD_GAINED_FOCUS:
            s_need_redraw = 1; break;
        case APP_CMD_WINDOW_RESIZED:
            if (app->window) {
                /* 仅更新尺寸，不重新加载字体和 mmap */
                int32_t new_w = ANativeWindow_getWidth(app->window);
                int32_t new_h = ANativeWindow_getHeight(app->window);
                if (new_w > 0 && new_h > 0 && (new_w != r_w() || new_h != r_h())) {
                    r_update_window(app->window);
                    rd_reset_prefetch();
                    if (rd_get_file_len() > 0)
                        rd_goto_offset(rd_get_page()->start_offset);
                    s_need_redraw = 1;
                }
            } break;
        case APP_CMD_CONFIG_CHANGED:
            if (app->window) {
                update_ui_scale(app);
                if (rd_get_file_len() > 0)
                    rd_goto_offset(rd_get_page()->start_offset);
                s_need_redraw = 1;
            }
            break;
        case APP_CMD_LOST_FOCUS:
            /* 失去焦点时暂停预排版以省电 */
            rd_reset_prefetch();
            break;
        case APP_CMD_PAUSE:
            rd_save_progress();
            break;
        case APP_CMD_SAVE_STATE:
            /* 系统可能杀进程，保存阅读进度 */
            rd_save_progress();
            break;
    }
}

static int32_t handle_input(struct android_app *app, AInputEvent *event)
{
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
            AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
            if (s_state == ST_SETTINGS) {
                s_state = ST_READING;
                s_need_redraw = 1;
            } else if (s_state == ST_BMARKS) {
                s_state = ST_FILELIST;
                s_need_redraw = 1;
            } else if (s_state == ST_DELETE) {
                s_state = ST_FILELIST;
                s_delete_idx = -1;
                s_need_redraw = 1;
            } else if (s_state == ST_READING) {
                s_state = ST_FILELIST;
                rd_close();
                rd_scan_dir();
                s_need_redraw = 1;
            } else {
                ANativeActivity_finish(app->activity);
            }
            return 1;
        }
        return 0;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        int32_t x = (int32_t)AMotionEvent_getX(event, 0);
        int32_t y = (int32_t)AMotionEvent_getY(event, 0);

        if (s_center_down_ns > 0 && !s_center_long_fired &&
            ((masked == AMOTION_EVENT_ACTION_MOVE && reading_cell(x, y) != 5) ||
             masked == AMOTION_EVENT_ACTION_CANCEL)) {
            s_center_down_ns = 0;
        }

        if (masked == AMOTION_EVENT_ACTION_DOWN) {
            if (s_state == ST_BMARKS) {
                s_bm_touch_active = 1;
                s_bm_dragging = 0;
                s_bm_down_y = y;
                s_bm_down_scroll = s_bm_scroll;
            } else if (s_state == ST_FILELIST) {
                if (y < r_h() - BOTTOM_BTN_ZONE) {
                    s_file_touch_active = 1;
                    s_file_dragging = 0;
                    s_file_down_y = y;
                    s_file_down_scroll = s_file_scroll;
                    s_file_hold_idx = file_index_at(y);
                    s_file_hold_start_ns = s_file_hold_idx >= 0
                        ? AMotionEvent_getEventTime(event) : 0;
                    s_file_hold_fired = 0;
                }
            } else if (s_state == ST_READING && reading_cell(x, y) == 5) {
                s_center_down_ns = AMotionEvent_getEventTime(event);
                s_center_long_fired = 0;
            }
            return 1;
        }

        if (masked == AMOTION_EVENT_ACTION_MOVE) {
            if (s_state == ST_BMARKS && s_bm_touch_active) {
                int32_t dy = y - s_bm_down_y;
                if (!s_bm_dragging && (dy > ui(8) || dy < -ui(8))) {
                    s_bm_dragging = 1;
                }
                if (s_bm_dragging) {
                    int32_t max_scroll = bm_list_max_scroll();
                    int32_t v = s_bm_down_scroll - dy;
                    if (v < 0) v = 0;
                    if (v > max_scroll) v = max_scroll;
                    if (v != s_bm_scroll) {
                        s_bm_scroll = v;
                        s_need_redraw = 1;
                    }
                }
            } else if (s_state == ST_FILELIST && s_file_touch_active) {
                int32_t dy = y - s_file_down_y;
                if (!s_file_dragging && (dy > ui(8) || dy < -ui(8))) {
                    s_file_dragging = 1;
                    s_file_hold_start_ns = 0;
                    s_file_hold_idx = -1;
                }
                if (s_file_dragging) {
                    int32_t count;
                    rd_get_files(&count);
                    int32_t max_scroll = file_list_max_scroll(count);
                    int32_t v = s_file_down_scroll - dy;
                    if (v < 0) v = 0;
                    if (v > max_scroll) v = max_scroll;
                    if (v != s_file_scroll) {
                        s_file_scroll = v;
                        s_need_redraw = 1;
                    }
                }
            }
            return 1;
        }

        if (masked == AMOTION_EVENT_ACTION_CANCEL) {
            s_bm_touch_active = 0;
            s_bm_dragging = 0;
            s_file_touch_active = 0;
            s_file_dragging = 0;
            s_file_hold_start_ns = 0;
            s_file_hold_idx = -1;
            return 1;
        }

        if (masked == AMOTION_EVENT_ACTION_UP) {
            if (s_center_long_fired) {
                s_center_down_ns = 0;
                s_center_long_fired = 0;
                s_need_redraw = 1;
                return 1;
            }
            if (s_file_hold_fired) {
                s_file_hold_start_ns = 0;
                s_file_hold_fired = 0;
                s_need_redraw = 1;
                return 1;
            }

            if (s_state == ST_BMARKS) {
                int32_t was_dragging = s_bm_dragging;
                s_bm_touch_active = 0;
                s_bm_dragging = 0;
                if (!was_dragging) {
                    int32_t list_top = LY_ITEM0 - BM_ITEM_H / 2;
                    int32_t iy = y + s_bm_scroll;
                    if (iy >= list_top) {
                        int32_t idx = (iy - list_top) / BM_ITEM_H;
                        if (idx >= 0 && idx < s_bm_view_count) {
                            int64_t offset = s_bm_view[idx].offset;
                            if (rd_open_file(s_bm_path) == 0) {
                                rd_goto_offset(offset);
                                s_file_error = 0;
                                s_state = ST_READING;
                            } else {
                                s_file_error = 1;
                                s_state = ST_FILELIST;
                            }
                        }
                    }
                }
                s_need_redraw = 1;
                return 1;
            }

            if (s_state == ST_DELETE) {
                int32_t count;
                FileItem *files = rd_get_files(&count);
                int32_t l, t, r, b;
                get_delete_rect(&l, &t, &r, &b);
                int32_t button_y = b - ui(76);
                if (x >= l && x <= r && y >= button_y && y <= b - ui(20) &&
                    s_delete_idx >= 0 && s_delete_idx < count) {
                    if (x < (l + r) / 2) {
                        s_state = ST_FILELIST;
                        s_delete_idx = -1;
                    } else {
                        s_file_error = rd_delete_file(files[s_delete_idx].path) == 0 ? 0 : 2;
                        s_delete_idx = -1;
                        s_state = ST_FILELIST;
                    }
                } else if (x < l || x > r || y < t || y > b) {
                    s_state = ST_FILELIST;
                    s_delete_idx = -1;
                }
                s_need_redraw = 1;
                return 1;
            }

            if (s_state == ST_FILELIST) {
                int32_t was_dragging = s_file_dragging;
                s_file_touch_active = 0;
                s_file_dragging = 0;
                s_file_hold_start_ns = 0;
                s_file_hold_idx = -1;
                if (was_dragging) return 1;

                int32_t count;
                FileItem *files = rd_get_files(&count);
                if (y > r_h() - BOTTOM_BTN_ZONE) {
                    s_file_error = 0;
                    pickFile();
                } else {
                    int32_t list_top = LY_ITEM0 - ITEM_H / 2;
                    int32_t iy = y + s_file_scroll;
                    if (count > 0 && iy >= list_top &&
                        iy < list_top + count * ITEM_H) {
                        int32_t fidx = (iy - list_top) / ITEM_H;
                        if (fidx >= 0 && fidx < count) {
                            int32_t bm_w = r_text_width(UI_BM_MARK, UI_LEN(UI_BM_MARK));
                            int32_t mark_left = r_w() - bm_w - LX_MARGIN;
                            if (files[fidx].bookmarks > 0 && x >= mark_left - ui(10)) {
                                int32_t n = rd_read_bookmarks(files[fidx].path,
                                                              s_bm_view, 50);
                                if (n > 0) {
                                    snprintf(s_bm_path, sizeof(s_bm_path), "%s",
                                             files[fidx].path);
                                    s_bm_view_count = n;
                                    s_bm_scroll = 0;
                                    s_state = ST_BMARKS;
                                    s_need_redraw = 1;
                                    return 1;
                                }
                            }
                            if (rd_open_file(files[fidx].path) == 0) {
                                s_file_error = 0;
                                s_state = ST_READING;
                                s_need_redraw = 1;
                            } else {
                                s_file_error = 1;
                            }
                        }
                    }
                }
            } else if (s_state == ST_READING && reading_cell(x, y) == 5) {
                rd_toggle_bookmark();
                s_center_down_ns = 0;
                s_need_redraw = 1;
            } else if (s_state == ST_READING || s_state == ST_SETTINGS) {
                s_center_down_ns = 0;
                handle_touch_reading(x, y);
            }
            return 1;
        }
    }
    return 0;
}

void android_main(struct android_app *app)
{
    s_app = app;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    while (1) {
        int32_t events;
        struct android_poll_source *source;
        int32_t timeout = (app->window && s_need_redraw) ? 0 : -1;
        if (timeout < 0 && s_state == ST_READING) timeout = 1000;
        if (s_center_down_ns > 0 && !s_center_long_fired) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
            int64_t elapsed_ms = (now_ns - s_center_down_ns) / 1000000LL;
            int32_t wait_ms = (int32_t)(3000 - elapsed_ms);
            timeout = wait_ms > 0 ? wait_ms : 1;
        }
        if (s_file_hold_start_ns > 0 && !s_file_hold_fired &&
            s_state == ST_FILELIST) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
            int64_t elapsed_ms = (now_ns - s_file_hold_start_ns) / 1000000LL;
            int32_t wait_ms = (int32_t)(3000 - elapsed_ms);
            if (wait_ms < 1) wait_ms = 1;
            if (timeout < 0 || wait_ms < timeout) timeout = wait_ms;
        }

        if (ALooper_pollOnce(timeout, NULL, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
        }

        if (s_center_down_ns > 0 && !s_center_long_fired) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
            if (now_ns - s_center_down_ns >= 3000000000LL) {
                s_center_long_fired = 1;
                s_center_down_ns = 0;
                s_state = ST_SETTINGS;
                s_need_redraw = 1;
            }
        }

        if (s_file_hold_start_ns > 0 && !s_file_hold_fired &&
            s_state == ST_FILELIST) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
            if (now_ns - s_file_hold_start_ns >= 3000000000LL) {
                s_file_hold_fired = 1;
                s_file_hold_start_ns = 0;
                s_delete_idx = s_file_hold_idx;
                if (s_delete_idx >= 0) s_state = ST_DELETE;
                s_need_redraw = 1;
            }
        }

        if (s_state == ST_READING) {
            time_t now = time(NULL);
            struct tm tmv;
            localtime_r(&now, &tmv);
            if (tmv.tm_min != s_last_min) {
                s_last_min = tmv.tm_min;
                s_need_redraw = 1;
            }
        }

        if (takeRescanFlag() == 1) {
            rd_scan_dir();
            s_need_redraw = 1;
        }

        if (s_need_redraw && app->window) {
            if (r_w() <= 0 || r_h() <= 0) {
                s_need_redraw = 0;
                continue;
            }
            ARect dirty = {0, 0, r_w(), r_h()};
            if (s_state == ST_SETTINGS) {
                get_settings_rect(&dirty.left, &dirty.top,
                                  &dirty.right, &dirty.bottom);
            }
            r_set_dirty(&dirty);

            r_begin();
            if (s_state == ST_FILELIST) draw_file_list();
            else if (s_state == ST_READING) draw_reading();
            else if (s_state == ST_SETTINGS) { draw_reading(); draw_settings(); }
            else if (s_state == ST_BMARKS) draw_bookmarks();
            else if (s_state == ST_DELETE) { draw_file_list(); draw_delete_dialog(); }
            r_end();
            s_need_redraw = 0;

            if (rd_is_prefetching()) {
                trigger_vsync_prefetch();
            }
        }
    }
}