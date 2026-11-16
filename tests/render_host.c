#include "render_host.h"

float r_ui_scale(void) { return 1.0f; }
int32_t r_w(void) { return 600; }
int32_t r_h(void) { return 800; }
int32_t r_font_h(void) { return 20; }
int32_t r_char_advance(uint32_t cp)
{
    if (cp < 0x80) return 10;
    if (cp == 0x4E2D) return 20;
    return 20;
}
void r_set_font_size(float size) { (void)size; }
void r_set_theme(int32_t theme) { (void)theme; }
