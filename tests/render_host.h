#ifndef RENDER_HOST_H
#define RENDER_HOST_H

#include <stdint.h>

float r_ui_scale(void);
int32_t r_w(void);
int32_t r_h(void);
int32_t r_font_h(void);
int32_t r_char_advance(uint32_t cp);
void r_set_font_size(float size);
void r_set_theme(int32_t theme);

#endif
