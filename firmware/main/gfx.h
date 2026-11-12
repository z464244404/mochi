#pragma once

#include <stdint.h>

/**
 * gfx —— 480x480 RGB565 软件渲染器
 *
 * 设计要点：
 *  - 只做「插件/内置应用需要的那几种图元」，全部带 1px 抗锯齿（SDF / 距离场）
 *  - 直接画进 LCD 的 PSRAM 帧缓冲，零拷贝
 *  - 不用 LVGL：全屏重绘时 LVGL 的对象/刷新机制反而是负担
 *  - 所有坐标是 480x480 逻辑坐标，浮点；颜色 0xAARRGGBB
 */

#define MOCHI_W 480
#define MOCHI_H 480

typedef struct {
    uint16_t *px;
    int w, h;
    uint8_t alpha; /* 全局不透明度 0~255，用于页面切换淡入淡出 */
} gfx_t;

typedef struct {
    uint32_t c0, c1;
    uint8_t gradient; /* 0 = 纯色 c0；1 = 纵向 c0→c1 */
} gfx_paint_t;

#define GFX_RGB(r, g, b) \
    (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define GFX_RGBA(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static inline gfx_paint_t gfx_solid(uint32_t c)
{
    gfx_paint_t p = {c, c, 0};
    return p;
}

static inline gfx_paint_t gfx_vgrad(uint32_t top, uint32_t bottom)
{
    gfx_paint_t p = {top, bottom, 1};
    return p;
}

void gfx_init(gfx_t *g, uint16_t *buf, int w, int h);

/* 整屏纵向三段渐变（近似径向氛围光，比逐像素算半径快一个数量级） */
void gfx_background(gfx_t *g, uint32_t top, uint32_t mid, uint32_t bottom);

void gfx_clear(gfx_t *g, uint32_t argb);

void gfx_round_rect(gfx_t *g, float cx, float cy, float w, float h, float r, gfx_paint_t p);

/* 体块外发光：只在形体外面 spread 像素的环带里画，alpha 从内到外平方衰减 */
void gfx_glow_round_rect(gfx_t *g, float cx, float cy, float w, float h, float r, float spread,
                         uint32_t argb);
void gfx_circle(gfx_t *g, float cx, float cy, float r, gfx_paint_t p);
void gfx_ellipse(gfx_t *g, float cx, float cy, float rx, float ry, gfx_paint_t p);

/* 角度制，0° = +x，顺时针为正（屏幕坐标 y 向下） */
void gfx_arc(gfx_t *g, float cx, float cy, float r, float thick, float a0, float a1, uint32_t argb);
void gfx_line(gfx_t *g, float x0, float y0, float x1, float y1, float thick, uint32_t argb);
void gfx_polyline(gfx_t *g, const float *pts, int n, float thick, uint32_t argb);

/* ---------- 字体 ---------- */

typedef struct {
    uint32_t cp;
    uint16_t w, h;
    int16_t bx, by; /* 相对笔位 / 基线的偏移 */
    uint16_t adv;
    uint32_t off;   /* 在 bitmap 里的字节偏移，8bit 灰度 */
} mochi_glyph_t;

typedef struct {
    const mochi_glyph_t *glyphs;
    uint16_t count;
    uint16_t line_h;
    const uint8_t *bitmap;
} mochi_font_t;

enum { GFX_ALIGN_LEFT = 0, GFX_ALIGN_CENTER = 1, GFX_ALIGN_RIGHT = 2 };

void gfx_text(gfx_t *g, const mochi_font_t *f, const char *utf8, float x, float y, int align,
              uint32_t argb);
float gfx_text_width(const mochi_font_t *f, const char *utf8);

/* 内置字体（tools/gen_font.py 生成） */
extern const mochi_font_t font_digits_106;
extern const mochi_font_t font_num_56;
extern const mochi_font_t font_num_22;
extern const mochi_font_t font_ui_29;
