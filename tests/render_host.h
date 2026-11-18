#ifndef RENDER_HOST_H
#define RENDER_HOST_H

#include <stdint.h>

float r_ui_scale(void);
int32_t r_w(void);
int32_t r_h(void);
void render_host_set_size(int32_t w, int32_t h);
void render_host_set_scale(float scale);
void render_host_set_cn_advance(int32_t advance);
int32_t r_font_h(void);
int32_t r_char_advance(uint32_t cp);
void r_set_font_size(float size);
void r_set_theme(int32_t theme);
void r_clear(void);
void r_clear_rect(int32_t l, int32_t t, int32_t r, int32_t b);
void r_stroke_rect(int32_t l, int32_t t, int32_t r, int32_t b, int32_t width);
void r_draw_text(int32_t x, int32_t y, const uint32_t *s, int32_t n);
void r_draw_text_justified(int32_t x, int32_t y, const uint32_t *s, int32_t n,
                           int32_t target_width);
void r_draw_text_at_size_color(int32_t x, int32_t y, const uint32_t *s,
                               int32_t n, float size, uint16_t color);
void r_prepare_parallel(void);
int32_t r_preload_text(const uint32_t *s, int32_t n);
int32_t r_text_width(const uint32_t *s, int32_t n);
int32_t r_text_width_at_size(const uint32_t *s, int32_t n, float size);

#endif
