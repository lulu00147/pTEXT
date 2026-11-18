#include "render_host.h"

static int32_t host_w = 600;
static int32_t host_h = 800;
static float host_scale = 1.0f;
static int32_t host_cn_advance = 20;

float r_ui_scale(void) { return host_scale; }
int32_t r_w(void) { return host_w; }
int32_t r_h(void) { return host_h; }
void render_host_set_size(int32_t w, int32_t h)
{
    host_w = w;
    host_h = h;
}
void render_host_set_scale(float scale) { host_scale = scale; }
void render_host_set_cn_advance(int32_t advance) { host_cn_advance = advance; }
int32_t r_font_h(void) { return 20; }
int32_t r_char_advance(uint32_t cp)
{
    if (cp == 0x4E01) return 5;
    if (cp < 0x80) return 10;
    if (cp == 0x4E2D) return host_cn_advance;
    return 20;
}
void r_set_font_size(float size) { (void)size; }
void r_set_theme(int32_t theme) { (void)theme; }
