#ifndef RENDER_H
#define RENDER_H
#include <android/native_window.h>
#include <android/asset_manager.h>
#include <android/rect.h>
#include <stdint.h>

void r_init(ANativeWindow *w, AAssetManager *a);
void r_deinit(void);
void r_begin(void);
void r_end(void);
void r_clear(void);
void r_clear_rect(int32_t l, int32_t t, int32_t r, int32_t b);
void r_draw_text(int32_t x, int32_t y, const uint32_t *s, int32_t n);
int32_t r_text_width(const uint32_t *s, int32_t n);
int32_t r_char_advance(uint32_t cp);
int32_t r_font_h(void);
void r_set_font_size(float s);
void r_clear_cache(void);
int32_t r_w(void);
int32_t r_h(void);
void r_set_dirty(const ARect *rect);
void r_update_window(ANativeWindow *w);
void r_set_theme(int32_t theme);
#endif
