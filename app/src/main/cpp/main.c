#include <android/native_activity.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/choreographer.h>
#include <android/configuration.h>
#include "android_native_app_glue.h"
#include <string.h>
#include <stdio.h>
#include "render.h"
#include "reader.h"
#include "utf8.h"
#include <android/log.h>

#define ST_FILELIST 0
#define ST_READING  1
#define ST_SETTINGS 2
#define ST_BMARKS   3

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
#define READ_TITLE_Y    ui(20)   /* 阅读页标题 Y */
#define READ_TEXT_Y0    ui(60)   /* 阅读页正文起始 Y */
#define SETTINGS_W      ui(360)  /* 设置面板宽 */
#define SETTINGS_H      ui(320)  /* 设置面板高 */
#define SETTINGS_ROW_H  ui(50)   /* 设置行高 */
#define SETTINGS_TITLE_H ui(20)  /* 标题内边距 */
#define TOUCH_ZONE_L     (r_w() / 3)       /* 左触摸区 */
#define TOUCH_ZONE_R     (r_w() * 2 / 3)   /* 右触摸区 */

static struct android_app *s_app;
static int32_t s_state = ST_FILELIST;
static int32_t s_need_redraw = 1;
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

static const uint32_t UI_TITLE[] = {0x70,0x54,0x45,0x58,0x54,0x20,
                                    0x2D,0x20,0x672C,0x5730,0x4E66,0x5E93,0};
static const uint32_t UI_EMPTY[] = {0x6682,0x65E0,0x56FE,0x4E66,0xFF0C,0x70B9,
                                    0x51FB,0x4E0B,0x65B9,0x5BFC,0x5165,0};
static const uint32_t UI_IMPORT[] = {0x5B,0x20,0x2B,0x20,0x5BFC,0x5165,0x56FE,
                                     0x4E66,0x20,0x5D,0};
static const uint32_t UI_SETTINGS_TITLE[] = {0x2D,0x2D,0x20,0x9605,0x8BFB,0x8BBE,
                                             0x7F6E,0x20,0x2D,0x2D,0};
static const uint32_t UI_BM_TITLE[] = {0x2D,0x2D,0x20,0x4E66,0x7B7E,0x5217,
                                       0x8868,0x20,0x2D,0x2D,0};
static const uint32_t UI_BM_EMPTY[] = {0x6682,0x65E0,0x4E66,0x7B7E,0};
static const uint32_t UI_BACK_READ[] = {0x8FD4,0x56DE,0x9605,0x8BFB,0};          /* 返回阅读 */
static const uint32_t UI_BOOKMARKS[] = {0x4E66,0x7B7E,0};                        /* 书签 */
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

static void draw_file_list(void)
{
    r_clear();
    int32_t count;
    FileItem *files = rd_get_files(&count);
    r_draw_text(LX_MARGIN, LY_TITLE, UI_TITLE, UI_LEN(UI_TITLE));
    if (count == 0) r_draw_text(LX_MARGIN, LY_ITEM0 + ITEM_H / 2, UI_EMPTY, UI_LEN(UI_EMPTY));
    for (int32_t i = 0; i < count; i++) {
        int32_t y = LY_ITEM0 + i * ITEM_H;
        if (y >= r_h()) break;
        uint32_t txt[256];
        int32_t n = utf8_to_utf32(txt, 255, files[i].name);
        r_draw_text(LX_MARGIN, y, txt, n);
    }
    r_draw_text(LX_MARGIN, r_h() - LY_BOTTOM_BTN, UI_IMPORT, UI_LEN(UI_IMPORT));
}

static void draw_reading(void)
{
    if (!rd_get_file_len()) return;
    r_clear();
    Page *p = rd_get_page();
    int32_t lh = rd_get_line_h();
    r_draw_text(LX_MARGIN, READ_TITLE_Y, UI_TITLE, UI_LEN(UI_TITLE));
    float pct = (rd_get_file_len() > 0) ?
                ((float)p->start_offset / rd_get_file_len() * 100.0f) : 0.0f;
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%.1f%%", pct);
    uint32_t utxt[64];
    int32_t n = utf8_to_utf32(utxt, 63, hdr);
    int32_t hdr_x = r_w() - r_text_width(utxt, n) - LX_MARGIN;
    r_draw_text(hdr_x, READ_TITLE_Y, utxt, n);
    for (int32_t i = 0; i < p->count; i++) {
        r_draw_text(LX_MARGIN, READ_TEXT_Y0 + i * lh, p->lines[i].text, p->lines[i].len);
    }
}

static void get_settings_rect(int32_t *l, int32_t *t, int32_t *r, int32_t *b)
{
    *l = r_w()/2 - SETTINGS_W/2; *t = r_h()/2 - SETTINGS_H/2;
    *r = *l + SETTINGS_W;        *b = *t + SETTINGS_H;
}

static void draw_settings(void)
{
    int32_t l, t, r, b;
    get_settings_rect(&l, &t, &r, &b);
    r_clear_rect(l, t, r, b);
    uint32_t utxt[128];
    int32_t n;
    Settings *s = rd_get_settings();
    int32_t font_idx = clamp_i(s->font_idx, 0, 6);
    int32_t indent_idx = clamp_i(s->indent_idx, 0, 2);
    int32_t gap_idx = clamp_i(s->gap_idx, 0, 2);
    int32_t theme = clamp_i(s->theme, 0, 1);
    int32_t y = t + SETTINGS_TITLE_H;
    r_draw_text(l+ui(40), y, UI_SETTINGS_TITLE, UI_LEN(UI_SETTINGS_TITLE)); y += SETTINGS_ROW_H;
    char buf[64];
    snprintf(buf, sizeof(buf), "字号: %d  [-] [+]", 18 + font_idx * 2);
    n = utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l+ui(30), y, utxt, n); y += SETTINGS_ROW_H;
    snprintf(buf, sizeof(buf), "缩进: %d字符  [<] [>]", indent_idx * 2);
    n = utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l+ui(30), y, utxt, n); y += SETTINGS_ROW_H;
    const char *gaps[] = {"0", "0.5", "1.0"};
    snprintf(buf, sizeof(buf), "行距: %s  [<] [>]", gaps[gap_idx]);
    n = utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l+ui(30), y, utxt, n); y += SETTINGS_ROW_H;
    /* 主题行 */
    snprintf(buf, sizeof(buf), "主题: %s  [<] [>]", theme ? "日间" : "夜间");
    n = utf8_to_utf32(utxt, 127, buf);
    r_draw_text(l+ui(30), y, utxt, n); y += SETTINGS_ROW_H;
    r_draw_text(l+ui(20), y, UI_BACK_READ, UI_LEN(UI_BACK_READ));
    int32_t bm_w = r_text_width(UI_BOOKMARKS, UI_LEN(UI_BOOKMARKS));
    r_draw_text(l + SETTINGS_W - bm_w - ui(20), y, UI_BOOKMARKS, UI_LEN(UI_BOOKMARKS));
}

static void draw_bookmarks(void)
{
    r_clear();
    r_draw_text(LX_MARGIN, LY_TITLE, UI_BM_TITLE, UI_LEN(UI_BM_TITLE));
    int32_t count;
    Bookmark *bms = rd_get_bookmarks(&count);
    if (count == 0) r_draw_text(LX_MARGIN, LY_ITEM0, UI_BM_EMPTY, UI_LEN(UI_BM_EMPTY));
    for (int32_t i = 0; i < count; i++) {
        int32_t y = LY_ITEM0 + i * BM_ITEM_H;
        if (y >= r_h()) break;
        char buf[64];
        snprintf(buf, sizeof(buf), "%d. %s", i+1, bms[i].preview);
        uint32_t utxt[64];
        int32_t n = utf8_to_utf32(utxt, 63, buf);
        r_draw_text(LX_MARGIN, y, utxt, n);
    }
    r_draw_text(LX_MARGIN, r_h() - LY_BOTTOM_BTN, UI_BACK_READ, UI_LEN(UI_BACK_READ));
}

static void handle_touch_reading(int32_t x, int32_t y)
{
    int32_t l, t, r, b;
    get_settings_rect(&l, &t, &r, &b);
    if (s_state == ST_SETTINGS) {
        if (x < l || x > r || y < t || y > b) {
            s_state = ST_READING; s_need_redraw = 1; return;
        }
        Settings *s = rd_get_settings();
        int32_t font_idx = clamp_i(s->font_idx, 0, 6);
        int32_t indent_idx = clamp_i(s->indent_idx, 0, 2);
        int32_t gap_idx = clamp_i(s->gap_idx, 0, 2);
        int32_t ry = y - t - SETTINGS_TITLE_H;
        int32_t need_rebuild = 0;
        /* 字号行: [-] [+] */
        if (ry >= SETTINGS_ROW_H && ry < SETTINGS_ROW_H * 2) {
            if (x < l + (r-l)/2) rd_set_font_idx(font_idx > 0 ? font_idx - 1 : 0);
            else rd_set_font_idx(font_idx < 6 ? font_idx + 1 : 6);
            need_rebuild = 1;
        /* 缩进行: [<] [>] */
        } else if (ry >= SETTINGS_ROW_H * 2 && ry < SETTINGS_ROW_H * 3) {
            if (x < l + (r-l)/2) rd_set_indent_idx(indent_idx > 0 ? indent_idx - 1 : 0);
            else rd_set_indent_idx(indent_idx < 2 ? indent_idx + 1 : 2);
            need_rebuild = 1;
        /* 行距行: [<] [>] */
        } else if (ry >= SETTINGS_ROW_H * 3 && ry < SETTINGS_ROW_H * 4) {
            if (x < l + (r-l)/2) rd_set_gap_idx(gap_idx > 0 ? gap_idx - 1 : 0);
            else rd_set_gap_idx(gap_idx < 2 ? gap_idx + 1 : 2);
            need_rebuild = 1;
        /* 主题行: [夜间] [日间] */
        } else if (ry >= SETTINGS_ROW_H * 4 && ry < SETTINGS_ROW_H * 5) {
            if (x < l + (r-l)/2) rd_set_theme(0);
            else rd_set_theme(1);
            s_need_redraw = 1;
        /* "返回阅读/书签" 行 */
        } else if (ry >= SETTINGS_ROW_H * 5 && ry < SETTINGS_ROW_H * 6) {
            if (x < l + (r-l)/2) { s_state = ST_READING; s_need_redraw = 1; return; }
            else { s_state = ST_BMARKS; s_need_redraw = 1; return; }
        }
        if (need_rebuild) {
            rd_reset_prefetch();
            rd_goto_offset(rd_get_page()->start_offset);
        }
        s_need_redraw = 1; return;
    }
    if (x < TOUCH_ZONE_L) {
        if (rd_can_prev()) { rd_prev_page(); s_need_redraw = 1; }
    } else if (x > TOUCH_ZONE_R) {
        if (rd_can_next()) { rd_next_page(); s_need_redraw = 1; }
    } else {
        s_state = ST_SETTINGS; s_need_redraw = 1;
    }
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
            if (s_state == ST_READING || s_state == ST_SETTINGS ||
                s_state == ST_BMARKS) {
                s_state = ST_FILELIST;
                rd_close();
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
        if ((action & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_UP) {
            int32_t x = (int32_t)AMotionEvent_getX(event, 0);
            int32_t y = (int32_t)AMotionEvent_getY(event, 0);

            int64_t down = AMotionEvent_getDownTime(event);
            int64_t up = AMotionEvent_getEventTime(event);
            if (s_state == ST_READING && down > 0 && up - down >= 500) {
                rd_toggle_bookmark();
                s_need_redraw = 1;
                return 1;
            }

            if (s_state == ST_FILELIST) {
                int32_t count;
                FileItem *files = rd_get_files(&count);
                if (y > r_h() - BOTTOM_BTN_ZONE) {
                    pickFile();
                } else if (y >= LY_ITEM0) {
                    int32_t fidx = (y - LY_ITEM0) / ITEM_H;
                    if (fidx >= 0 && fidx < count &&
                        rd_open_file(files[fidx].path) == 0) {
                        s_state = ST_READING;
                        s_need_redraw = 1;
                    }
                }
            } else if (s_state == ST_READING || s_state == ST_SETTINGS) {
                handle_touch_reading(x, y);
            } else if (s_state == ST_BMARKS) {
                int32_t count;
                rd_get_bookmarks(&count);
                if (y >= LY_ITEM0) {
                    int32_t bidx = (y - LY_ITEM0) / BM_ITEM_H;
                    if (bidx >= 0 && bidx < count) rd_jump_bookmark(bidx);
                }
                s_state = ST_READING;
                s_need_redraw = 1;
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

        if (ALooper_pollOnce(timeout, NULL, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
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
            if (s_state == ST_READING) {
                Page *p = rd_get_page();
                int32_t lh = rd_get_line_h();
                dirty.top = READ_TITLE_Y;
                dirty.bottom = READ_TEXT_Y0 + p->count * lh;
                if (dirty.bottom > r_h() - 50) dirty.bottom = r_h() - 50;
            } else if (s_state == ST_SETTINGS) {
                get_settings_rect(&dirty.left, &dirty.top,
                                  &dirty.right, &dirty.bottom);
            }
            r_set_dirty(&dirty);

            r_begin();
            if (s_state == ST_FILELIST) draw_file_list();
            else if (s_state == ST_READING) draw_reading();
            else if (s_state == ST_SETTINGS) { draw_reading(); draw_settings(); }
            else if (s_state == ST_BMARKS) draw_bookmarks();
            r_end();
            s_need_redraw = 0;

            if (rd_is_prefetching()) {
                trigger_vsync_prefetch();
            }
        }
    }
}