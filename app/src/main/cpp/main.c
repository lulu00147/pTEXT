#include <android/native_activity.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/choreographer.h>
#include <android/configuration.h>
#include "android_native_app_glue.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "autotest.h"
#include "render.h"
#include "reader.h"
#include "clamp.h"
#include "ui_lists.h"
#include "ui_reading.h"
#include "ui_settings.h"
#include "ui_util.h"
#include "affinity.h"

#define ST_FILELIST 0
#define ST_READING  1
#define ST_SETTINGS 2
#define ST_BMARKS   3
#define ST_DELETE   4

static struct android_app *s_app;
static int32_t s_state = ST_FILELIST;
static int32_t s_need_redraw = 1;
static int32_t s_last_min = -1;
static int64_t s_last_draw_ns;


static int64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
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
static Bookmark s_bm_view[MAX_BOOKMARKS];
static int32_t s_bm_view_count = 0;
static int32_t s_bm_scroll = 0;
static int32_t s_bm_touch_active = 0;
static int32_t s_bm_dragging = 0;
static int32_t s_bm_down_y = 0;
static int32_t s_bm_down_scroll = 0;
static char s_bm_path[1024];
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

enum {
    GRID_TOP_LEFT = 1,
    GRID_TOP_CENTER,
    GRID_TOP_RIGHT,
    GRID_MID_LEFT,
    GRID_CENTER,
    GRID_MID_RIGHT,
    GRID_BOTTOM_LEFT,
    GRID_BOTTOM_CENTER,
    GRID_BOTTOM_RIGHT
};

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
    if (cell == GRID_TOP_LEFT || cell == GRID_TOP_CENTER ||
        cell == GRID_MID_LEFT || cell == GRID_BOTTOM_LEFT) {
        if (rd_can_prev()) rd_prev_page();
    } else if (cell == GRID_TOP_RIGHT || cell == GRID_MID_RIGHT ||
               cell == GRID_BOTTOM_CENTER || cell == GRID_BOTTOM_RIGHT) {
        if (rd_can_next()) rd_next_page();
    } else {
        s_state = ST_SETTINGS;
    }
}

static void handle_touch_reading(int32_t x, int32_t y)
{
    if (s_state == ST_SETTINGS) {
        if (!ui_settings_contains(x, y)) {
            s_state = ST_READING;
            s_need_redraw = 1;
            return;
        }
        Settings *s = rd_get_settings();
        int32_t font_idx = clamp_i32(s->font_idx, 0, 6);
        int32_t indent_idx = clamp_i32(s->indent_idx, 0, 2);
        int32_t gap_idx = clamp_i32(s->gap_idx, 0, 2);
        int32_t affinity_idx = clamp_i32(s->affinity_idx, 0, 2);
        int32_t row = ui_settings_row_at(y);
        int32_t left = ui_settings_button_at(x) == 0;
        int32_t need_rebuild = 0;
        if (row == 0) {
            rd_set_font_idx(left ? (font_idx > 0 ? font_idx - 1 : 0)
                                 : (font_idx < 6 ? font_idx + 1 : 6));
            need_rebuild = 1;
        } else if (row == 1) {
            rd_set_indent_idx(left ? (indent_idx > 0 ? indent_idx - 1 : 0)
                                   : (indent_idx < 2 ? indent_idx + 1 : 2));
            need_rebuild = 1;
        } else if (row == 2) {
            rd_set_gap_idx(left ? (gap_idx > 0 ? gap_idx - 1 : 0)
                                : (gap_idx < 2 ? gap_idx + 1 : 2));
            need_rebuild = 1;
        } else if (row == 3) {
            rd_set_theme(left ? 0 : 1);
            s_need_redraw = 1;
        } else if (row == 4) {
            int32_t affinity_button = ui_settings_affinity_button_at(x);
            if (affinity_button >= 0) {
                rd_set_affinity_idx(affinity_button);
                affinity_set_mode(rd_get_settings()->affinity_idx);
                s_need_redraw = 1;
            }
        }
        if (need_rebuild) {
            rd_reset_prefetch();
            rd_goto_offset(rd_get_page()->start_offset);
        }
        s_need_redraw = 1;
        return;
    }
    reading_grid_action(reading_cell(x, y));
    s_need_redraw = 1;
}

/* ---- JNI 相关全局变量 ---- */
static JavaVM *s_jvm = NULL;
static jclass s_main_activity_class_global = NULL;
static jmethodID s_pick_file_mid = NULL;
static jmethodID s_take_rescan_mid = NULL;
static jmethodID s_reset_screen_mid = NULL;

static void ensure_jni(void)
{
    if (!s_jvm && s_app) s_jvm = s_app->activity->vm;
}

static void release_jni_refs(JNIEnv *env)
{
    if (!env || !s_main_activity_class_global) return;
    (*env)->DeleteGlobalRef(env, s_main_activity_class_global);
    s_main_activity_class_global = NULL;
    s_pick_file_mid = NULL;
    s_take_rescan_mid = NULL;
    s_reset_screen_mid = NULL;
}

void pickFile(void) {
    ensure_jni();
    if (!s_jvm) return;
    JNIEnv *env = NULL;
    (*s_jvm)->AttachCurrentThread(s_jvm, &env, NULL);
    if (!env) return;
    if (!s_main_activity_class_global) return;
    if (!s_pick_file_mid) {
        s_pick_file_mid = (*env)->GetStaticMethodID(env, s_main_activity_class_global,
                                                    "pickFile", "()V");
        if (!s_pick_file_mid) {
            (*env)->ExceptionClear(env);
            return;
        }
    }
    (*env)->CallStaticVoidMethod(env, s_main_activity_class_global, s_pick_file_mid);
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

static void reset_screen_on(void) {
    ensure_jni();
    if (!s_jvm || !s_main_activity_class_global) return;
    JNIEnv *env = NULL;
    (*s_jvm)->AttachCurrentThread(s_jvm, &env, NULL);
    if (!env) return;
    if (!s_reset_screen_mid) {
        s_reset_screen_mid = (*env)->GetStaticMethodID(env, s_main_activity_class_global,
                                                       "resetScreenOn", "()V");
        if (!s_reset_screen_mid) {
            (*env)->ExceptionClear(env);
            return;
        }
    }
    (*env)->CallStaticVoidMethod(env, s_main_activity_class_global, s_reset_screen_mid);
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

static void handle_init_window(struct android_app *app)
{
    JNIEnv *env = NULL;
    (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL);
    if (env) {
        release_jni_refs(env);
        /* activity->clazz 是 Activity 实例，不是 jclass；先取对象类。 */
        jclass clazz = (*env)->GetObjectClass(env, app->activity->clazz);
        if (clazz) {
            s_main_activity_class_global = (*env)->NewGlobalRef(env, clazz);
            (*env)->DeleteLocalRef(env, clazz);
        }
        if (!s_main_activity_class_global) (*env)->ExceptionClear(env);
    }
    update_ui_scale(app);
    r_init(app->window);
    rd_init(app->activity->internalDataPath);
    rd_scan_dir();
    affinity_set_mode(rd_get_settings()->affinity_idx);
#ifdef PTEXT_AUTOTEST
    if (autotest_begin(app)) {
        s_state = ST_READING;
        s_need_redraw = 1;
    }
#endif
    if (rd_get_file_len() > 0) rd_goto_offset(rd_get_page()->start_offset);
    s_need_redraw = 1;
    s_choreographer = AChoreographer_getInstance();
}

static void handle_term_window(struct android_app *app)
{
    rd_save_progress();
    JNIEnv *env = NULL;
    (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL);
    release_jni_refs(env);
    r_deinit();
}

static void handle_window_redraw(void)
{
    if (s_need_redraw ||
        (s_last_draw_ns > 0 && monotonic_ns() - s_last_draw_ns < 20000000LL))
        return;
    s_need_redraw = 1;
}

static void handle_window_resized(struct android_app *app)
{
    if (!app->window) return;
    int32_t new_w = ANativeWindow_getWidth(app->window);
    int32_t new_h = ANativeWindow_getHeight(app->window);
    if (new_w <= 0 || new_h <= 0 || (new_w == r_w() && new_h == r_h())) return;

    /* 仅更新尺寸，不重新加载字体和 mmap */
    r_update_window(app->window);
    rd_reset_prefetch();
    rd_reset_page_cache();
    if (rd_get_file_len() > 0) rd_goto_offset(rd_get_page()->start_offset);
    s_need_redraw = 1;
}

static void handle_config_changed(struct android_app *app)
{
    if (!app->window) return;
    update_ui_scale(app);
    Settings *cfg = rd_get_settings();
    r_set_font_size((18.0f + clamp_i32(cfg->font_idx, 0, 6) * 2.0f) * r_ui_scale());
    rd_reset_page_cache();
    if (rd_get_file_len() > 0) rd_goto_offset(rd_get_page()->start_offset);
    s_need_redraw = 1;
}

static void handle_cmd(struct android_app *app, int32_t cmd)
{
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:      handle_init_window(app); break;
    case APP_CMD_TERM_WINDOW:      handle_term_window(app); break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_START:
    case APP_CMD_RESUME:           s_need_redraw = 1; break;
    case APP_CMD_WINDOW_REDRAW_NEEDED: handle_window_redraw(); break;
    case APP_CMD_WINDOW_RESIZED:   handle_window_resized(app); break;
    case APP_CMD_CONFIG_CHANGED:   handle_config_changed(app); break;
    case APP_CMD_LOST_FOCUS:       rd_reset_prefetch(); break;
    case APP_CMD_PAUSE:
    case APP_CMD_SAVE_STATE:       rd_save_progress(); break;
    default:                       break;
    }
}

static int32_t handle_back_key(struct android_app *app)
{
    switch (s_state) {
    case ST_SETTINGS:
        s_state = ST_READING;
        break;
    case ST_BMARKS:
        s_state = ST_FILELIST;
        break;
    case ST_DELETE:
        s_state = ST_FILELIST;
        s_delete_idx = -1;
        break;
    case ST_READING:
        s_state = ST_FILELIST;
        rd_close();
        rd_scan_dir();
        break;
    default:
        ANativeActivity_finish(app->activity);
        return 1;
    }
    s_need_redraw = 1;
    return 1;
}

static void motion_center_cancel(int32_t masked, int32_t x, int32_t y)
{
    if (s_center_down_ns > 0 && !s_center_long_fired &&
        ((masked == AMOTION_EVENT_ACTION_MOVE && reading_cell(x, y) != GRID_CENTER) ||
         masked == AMOTION_EVENT_ACTION_CANCEL)) {
        s_center_down_ns = 0;
    }
}

static void motion_down(AInputEvent *event, int32_t x, int32_t y)
{
    reset_screen_on();
    if (s_state == ST_BMARKS) {
        s_bm_touch_active = 1;
        s_bm_dragging = 0;
        s_bm_down_y = y;
        s_bm_down_scroll = s_bm_scroll;
    } else if (s_state == ST_FILELIST) {
        if (!ui_file_import_hit(y)) {
            s_file_touch_active = 1;
            s_file_dragging = 0;
            s_file_down_y = y;
            s_file_down_scroll = s_file_scroll;
            s_file_hold_idx = ui_file_index_at(y, s_file_scroll);
            s_file_hold_start_ns = s_file_hold_idx >= 0
                ? AMotionEvent_getEventTime(event) : 0;
            s_file_hold_fired = 0;
        }
    } else if (s_state == ST_READING && reading_cell(x, y) == GRID_CENTER) {
        s_center_down_ns = AMotionEvent_getEventTime(event);
        s_center_long_fired = 0;
    }
}

static void motion_move(int32_t x, int32_t y)
{
    if (s_state == ST_BMARKS && s_bm_touch_active) {
        int32_t dy = y - s_bm_down_y;
        if (!s_bm_dragging && (dy > ui_px(8) || dy < -ui_px(8))) s_bm_dragging = 1;
        if (s_bm_dragging) {
            int32_t max_scroll = ui_bm_max_scroll(s_bm_view_count);
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
        if (!s_file_dragging && (dy > ui_px(8) || dy < -ui_px(8))) {
            s_file_dragging = 1;
            s_file_hold_start_ns = 0;
            s_file_hold_idx = -1;
        }
        if (s_file_dragging) {
            int32_t count;
            rd_get_files(&count);
            int32_t max_scroll = ui_file_max_scroll(count);
            int32_t v = s_file_down_scroll - dy;
            if (v < 0) v = 0;
            if (v > max_scroll) v = max_scroll;
            if (v != s_file_scroll) {
                s_file_scroll = v;
                s_need_redraw = 1;
            }
        }
    }
    (void)x;
}

static void motion_cancel(void)
{
    s_bm_touch_active = 0;
    s_bm_dragging = 0;
    s_file_touch_active = 0;
    s_file_dragging = 0;
    s_file_hold_start_ns = 0;
    s_file_hold_idx = -1;
}

static void motion_up(int32_t x, int32_t y)
{
    if (s_center_long_fired) {
        s_center_down_ns = 0;
        s_center_long_fired = 0;
        s_need_redraw = 1;
        return;
    }
    if (s_file_hold_fired) {
        s_file_hold_start_ns = 0;
        s_file_hold_fired = 0;
        s_need_redraw = 1;
        return;
    }

    if (s_state == ST_BMARKS) {
        int32_t was_dragging = s_bm_dragging;
        s_bm_touch_active = 0;
        s_bm_dragging = 0;
        if (!was_dragging) {
            int32_t idx = ui_bm_index_at(y, s_bm_scroll);
            if (idx >= 0 && idx < s_bm_view_count) {
                int64_t offset = s_bm_view[idx].offset;
                if (rd_open_file(s_bm_path) == 0) {
                    rd_jump_to_offset(offset);
                    s_file_error = 0;
                    s_state = ST_READING;
                } else {
                    s_file_error = 1;
                    s_state = ST_FILELIST;
                }
            }
        }
        s_need_redraw = 1;
        return;
    }

    if (s_state == ST_DELETE) {
        int32_t count;
        FileItem *files = rd_get_files(&count);
        int32_t l, t, r, b;
        ui_get_delete_rect(&l, &t, &r, &b);
        int32_t button_y = b - ui_px(76);
        if (x >= l && x <= r && y >= button_y && y <= b - ui_px(20) &&
            s_delete_idx >= 0 && s_delete_idx < count) {
            if (x < (l + r) / 2) {
                s_state = ST_FILELIST;
                s_delete_idx = -1;
            } else {
                int32_t result = rd_delete_file(files[s_delete_idx].path);
                s_file_error = result == 0 ? 0 : (result == -2 ? 3 : 2);
                s_delete_idx = -1;
                s_state = ST_FILELIST;
            }
        } else if (x < l || x > r || y < t || y > b) {
            s_state = ST_FILELIST;
            s_delete_idx = -1;
        }
        s_need_redraw = 1;
        return;
    }

    if (s_state == ST_FILELIST) {
        int32_t was_dragging = s_file_dragging;
        s_file_touch_active = 0;
        s_file_dragging = 0;
        s_file_hold_start_ns = 0;
        s_file_hold_idx = -1;
        if (!was_dragging) {
            int32_t count;
            FileItem *files = rd_get_files(&count);
            if (ui_file_import_hit(y)) {
                s_file_error = 0;
                pickFile();
            } else {
                int32_t fidx = ui_file_index_at(y, s_file_scroll);
                if (fidx >= 0 && fidx < count) {
                    int32_t mark_left = ui_file_mark_left();
                    if (files[fidx].bookmarks > 0 && x >= mark_left - ui_px(10)) {
                        int32_t n = rd_read_bookmarks(files[fidx].path,
                                                      s_bm_view, MAX_BOOKMARKS);
                        if (n > 0) {
                            snprintf(s_bm_path, sizeof(s_bm_path), "%s",
                                     files[fidx].path);
                            s_bm_view_count = n;
                            s_bm_scroll = 0;
                            s_state = ST_BMARKS;
                            s_need_redraw = 1;
                            return;
                        }
                    }
                    if (rd_open_file(files[fidx].path) == 0) {
                        s_file_error = 0;
                        s_state = ST_READING;
                    } else {
                        s_file_error = 1;
                    }
                    s_need_redraw = 1;
                }
            }
        }
    } else if (s_state == ST_READING && reading_cell(x, y) == GRID_CENTER) {
        rd_toggle_bookmark();
        s_center_down_ns = 0;
        s_need_redraw = 1;
    } else if (s_state == ST_READING || s_state == ST_SETTINGS) {
        s_center_down_ns = 0;
        handle_touch_reading(x, y);
    }
}

static int32_t handle_input(struct android_app *app, AInputEvent *event)
{
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
            AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
            return handle_back_key(app);
        }
        return 0;
    }

    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;

    int32_t masked = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    int32_t x = (int32_t)AMotionEvent_getX(event, 0);
    int32_t y = (int32_t)AMotionEvent_getY(event, 0);
    motion_center_cancel(masked, x, y);

    switch (masked) {
    case AMOTION_EVENT_ACTION_DOWN:
        motion_down(event, x, y);
        return 1;
    case AMOTION_EVENT_ACTION_MOVE:
        motion_move(x, y);
        return 1;
    case AMOTION_EVENT_ACTION_CANCEL:
        motion_cancel();
        return 1;
    case AMOTION_EVENT_ACTION_UP:
        motion_up(x, y);
        return 1;
    default:
        return 0;
    }
}

void android_main(struct android_app *app)
{
    s_app = app;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    while (!app->destroyRequested) {
        int32_t events;
        struct android_poll_source *source;
        int32_t timeout = (app->window && s_need_redraw) ? 0 : -1;
        if (timeout < 0 && s_state == ST_READING) {
            time_t now = time(NULL);
            struct tm tmv;
            localtime_r(&now, &tmv);
            int32_t sec = 60 - tmv.tm_sec;
            if (sec < 1) sec = 1;
            timeout = sec * 1000;
        }
        if (s_center_down_ns > 0 && !s_center_long_fired) {
            int64_t now_ns = monotonic_ns();
            int64_t elapsed_ms = (now_ns - s_center_down_ns) / 1000000LL;
            int32_t wait_ms = (int32_t)(3000 - elapsed_ms);
            timeout = wait_ms > 0 ? wait_ms : 1;
        }
        if (s_file_hold_start_ns > 0 && !s_file_hold_fired &&
            s_state == ST_FILELIST) {
            int64_t now_ns = monotonic_ns();
            int64_t elapsed_ms = (now_ns - s_file_hold_start_ns) / 1000000LL;
            int32_t wait_ms = (int32_t)(3000 - elapsed_ms);
            if (wait_ms < 1) wait_ms = 1;
            if (timeout < 0 || wait_ms < timeout) timeout = wait_ms;
        }

        if (ALooper_pollOnce(timeout, NULL, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
        }

        if (s_center_down_ns > 0 && !s_center_long_fired) {
            int64_t now_ns = monotonic_ns();
            if (now_ns - s_center_down_ns >= 3000000000LL) {
                s_center_long_fired = 1;
                s_center_down_ns = 0;
                s_state = ST_SETTINGS;
                s_need_redraw = 1;
            }
        }

        if (s_file_hold_start_ns > 0 && !s_file_hold_fired &&
            s_state == ST_FILELIST) {
            int64_t now_ns = monotonic_ns();
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
                s_need_redraw = 1;
                struct timespec wait = {0, 16000000L};
                nanosleep(&wait, NULL);
                continue;
            }

            if (!r_begin()) {
                s_need_redraw = 1;
                struct timespec wait = {0, 16000000L};
                nanosleep(&wait, NULL);
                continue;
            }
#ifdef PTEXT_AUTOTEST
            autotest_draw_start();
#endif
            if (s_state == ST_FILELIST) ui_draw_file_list(&s_file_scroll, s_file_error);
            else if (s_state == ST_READING) ui_draw_reading();
            else if (s_state == ST_SETTINGS) { ui_draw_reading(); ui_draw_settings(); }
            else if (s_state == ST_BMARKS) ui_draw_bookmarks(s_bm_view, s_bm_view_count, &s_bm_scroll);
            else if (s_state == ST_DELETE) {
                int32_t count;
                FileItem *files = rd_get_files(&count);
                const char *name = (s_delete_idx >= 0 && s_delete_idx < count)
                    ? files[s_delete_idx].name : "";
                ui_draw_file_list(&s_file_scroll, s_file_error);
                ui_draw_delete_dialog(name);
            }
            r_end();
            s_need_redraw = 0;
            s_last_draw_ns = monotonic_ns();
#ifdef PTEXT_AUTOTEST
            if (autotest_after_draw(app)) {
                rd_next_page();
                s_need_redraw = 1;
            }
#endif

            if (rd_is_prefetching()) {
                trigger_vsync_prefetch();
            }
        }
    }

    rd_close();
    r_deinit();
}