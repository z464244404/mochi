#include "gfx.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 基础：RGB565 混合                                                    */
/* ------------------------------------------------------------------ */

static inline uint16_t to565(uint32_t c)
{
    return (uint16_t)((((c >> 19) & 0x1F) << 11) | (((c >> 10) & 0x3F) << 5) | ((c >> 3) & 0x1F));
}

static inline uint16_t blend565(uint16_t dst, uint32_t rgb, int a)
{
    if (a <= 0) return dst;
    if (a >= 255) return to565(rgb);
    int sr = (rgb >> 19) & 0x1F, sg = (rgb >> 10) & 0x3F, sb = (rgb >> 3) & 0x1F;
    int dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
    dr += (sr - dr) * a / 256;
    dg += (sg - dg) * a / 256;
    db += (sb - db) * a / 256;
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

static inline void put(gfx_t *g, int x, int y, uint32_t c, int cov255)
{
    if ((unsigned)x >= (unsigned)g->w || (unsigned)y >= (unsigned)g->h || cov255 <= 0) return;
    int a = (int)(c >> 24);
    if (a == 0) return;
    a = a * g->alpha / 255;
    a = a * cov255 / 255;
    uint16_t *p = &g->px[y * g->w + x];
    *p = blend565(*p, c, a);
}

static inline int cov_from_dist(float d)
{
    float cov = 0.5f - d;
    if (cov <= 0.0f) return 0;
    if (cov >= 1.0f) return 255;
    return (int)(cov * 255.0f + 0.5f);
}

/**
 * 边缘距离的一阶近似：sqrt(d2) - r ≈ (d2 - r²) / (2r)
 *
 * 为什么不用 sqrtf：xtensa 工具链不会把它内联成 sqrt.s，每个像素一次
 * libm 调用，480×480 下直接把帧率打到个位数。这个近似在 AA 过渡带
 * （|sqrt(d2)-r| < 1px）内误差远小于 0.01px，肉眼完全看不出。
 */
static inline float dist_approx(float d2, float r)
{
    return (d2 - r * r) * (0.5f / r);
}

static inline uint32_t lerp_rgb(uint32_t a, uint32_t b, float t)
{
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

void gfx_init(gfx_t *g, uint16_t *buf, int w, int h)
{
    g->px = buf;
    g->w = w;
    g->h = h;
    g->alpha = 255;
}

void gfx_clear(gfx_t *g, uint32_t argb)
{
    uint16_t c = to565(argb);
    uint32_t c2 = ((uint32_t)c << 16) | c;
    int n = g->w * g->h / 2;
    uint32_t *p32 = (uint32_t *)g->px;
    for (int i = 0; i < n; i++) p32[i] = c2;
}

void gfx_background(gfx_t *g, uint32_t top, uint32_t mid, uint32_t bottom)
{
    int half = g->h / 2;
    int half_w = g->w / 2;
    for (int y = 0; y < g->h; y++) {
        float t = (y < half) ? (float)y / (float)half
                             : (float)(y - half) / (float)(g->h - half);
        uint32_t c = (y < half) ? lerp_rgb(top, mid, t) : lerp_rgb(mid, bottom, t);
        uint16_t c565 = to565(c);
        uint16_t *row = &g->px[y * g->w];
        if (g->alpha >= 255) {
            uint32_t c2 = ((uint32_t)c565 << 16) | c565;
            uint32_t *row32 = (uint32_t *)row;
            for (int x = 0; x < half_w; x++) row32[x] = c2;
        } else {
            /* 页面切换时整屏（含背景）一起淡入淡出，避免两个应用背景色硬跳 */
            for (int x = 0; x < g->w; x++) row[x] = blend565(row[x], c, g->alpha);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 图元                                                                 */
/* ------------------------------------------------------------------ */

void gfx_round_rect(gfx_t *g, float cx, float cy, float w, float h, float r0, gfx_paint_t p)
{
    float hw = w * 0.5f, hh = h * 0.5f;
    float r = r0;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    if (r < 0.5f) r = 0.5f;

    int y0 = (int)floorf(cy - hh - 1.0f);
    int y1 = (int)ceilf(cy + hh + 1.0f);
    if (y0 < 0) y0 = 0;
    if (y1 > g->h) y1 = g->h;

    const float rsq = r * r;
    const float rc = r - 1.0f;
    const float rcsq = rc > 0 ? rc * rc : 0.0f;
    const int base_a = (int)(p.c0 >> 24);

    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f - cy;
        float qy = fabsf(py) - hh + r;
        if (qy > r + 1.0f) continue;

        /* 这一行形状的半宽（d=0 处）和「完全覆盖」半宽（d<=-1） */
        float half_out, half_core;
        if (qy <= 0.0f) {
            half_out = hw;
            half_core = hw - 1.0f;
        } else {
            float s = rsq - qy * qy;
            half_out = (hw - r) + (s > 0.0f ? sqrtf(s) : 0.0f);
            half_core = (qy < rc) ? (hw - r) + sqrtf(rcsq - qy * qy) : -1.0f;
        }

        uint32_t rowc = p.c0;
        if (p.gradient) {
            float t = (py + hh) / h;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            rowc = lerp_rgb(p.c0, p.c1, t);
        }

        int lo = (int)floorf(cx - half_out) - 1;
        int hi = (int)ceilf(cx + half_out) + 1;
        if (lo < 0) lo = 0;
        if (hi > g->w) hi = g->w;

        int cl = (int)ceilf(cx - half_core);
        int cr = (int)floorf(cx + half_core);
        int a_full = base_a * g->alpha / 255;
        uint16_t *row = &g->px[y * g->w];

        /* 内部：整段直写，不做逐像素距离计算 */
        if (cl <= cr) {
            int l = cl < 0 ? 0 : cl;
            int rr = cr > g->w - 1 ? g->w - 1 : cr;
            if (l <= rr) {
                if (a_full >= 255) {
                    uint16_t c565 = to565(rowc);
                    for (int x = l; x <= rr; x++) row[x] = c565;
                } else if (a_full > 0) {
                    for (int x = l; x <= rr; x++) row[x] = blend565(row[x], rowc, a_full);
                }
            }
        }

        /* 只有两侧 1~2 个像素需要真算覆盖率 */
        for (int x = lo; x < hi; x++) {
            if (x >= cl && x <= cr) continue;
            float px = x + 0.5f - cx;
            float qx = fabsf(px) - hw + r;
            float d = (qx > 0.0f && qy > 0.0f) ? dist_approx(qx * qx + qy * qy, r)
                                               : (qx > qy ? qx : qy) - r;
            int cov = cov_from_dist(d);
            if (cov) put(g, x, y, rowc, cov);
        }
    }
}

void gfx_glow_round_rect(gfx_t *g, float cx, float cy, float w, float h, float r, float spread,
                         uint32_t argb)
{
    float hw = w * 0.5f, hh = h * 0.5f;
    if (r > hw) r = hw;
    if (r > hh) r = hh;

    int x0 = (int)floorf(cx - hw - spread - 1), x1 = (int)ceilf(cx + hw + spread + 1);
    int y0 = (int)floorf(cy - hh - spread - 1), y1 = (int)ceilf(cy + hh + spread + 1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;

    int base_a = (int)(argb >> 24);
    float inv = 1.0f / spread;

    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f - cy;
        float qy = fabsf(py) - hh + r;
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f - cx;
            float qx = fabsf(px) - hw + r;
            float d = (qx > 0.0f && qy > 0.0f) ? dist_approx(qx * qx + qy * qy, r)
                                               : (qx > qy ? qx : qy) - r;
            if (d < 0.0f || d > spread) continue;
            float t = 1.0f - d * inv;
            int a = (int)(base_a * t * t);
            if (a > 0) put(g, x, y, argb, a);
        }
    }
}

void gfx_circle(gfx_t *g, float cx, float cy, float r, gfx_paint_t p)
{
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;

    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f - cy;
        uint32_t rowc = p.c0;
        if (p.gradient) {
            float t = (py + r) / (2.0f * r > 0 ? 2.0f * r : 1.0f);
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            rowc = lerp_rgb(p.c0, p.c1, t);
        }
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f - cx;
            float d2 = px * px + py * py;
            float ri = r - 1.0f, ro = r + 1.0f;
            int cov;
            if (d2 < ri * ri) {
                cov = 255;
            } else if (d2 > ro * ro) {
                continue;
            } else {
                cov = cov_from_dist(dist_approx(d2, r));
            }
            if (cov > 0) put(g, x, y, rowc, cov);
        }
    }
}

void gfx_ellipse(gfx_t *g, float cx, float cy, float rx, float ry, gfx_paint_t p)
{
    if (rx <= 0 || ry <= 0) return;
    int x0 = (int)floorf(cx - rx - 1), x1 = (int)ceilf(cx + rx + 1);
    int y0 = (int)floorf(cy - ry - 1), y1 = (int)ceilf(cy + ry + 1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;
    float k = rx < ry ? rx : ry;
    float irx = 1.0f / rx, iry = 1.0f / ry;

    for (int y = y0; y < y1; y++) {
        float py = (y + 0.5f - cy) * iry;
        uint32_t rowc = p.c0;
        if (p.gradient) {
            float t = (y + 0.5f - cy + ry) / (2.0f * ry);
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            rowc = lerp_rgb(p.c0, p.c1, t);
        }
        for (int x = x0; x < x1; x++) {
            float px = (x + 0.5f - cx) * irx;
            float k2 = px * px + py * py;
            int cov;
            if (k2 < 0.90f) {
                cov = 255;
            } else if (k2 > 1.10f) {
                continue;
            } else {
                cov = cov_from_dist(dist_approx(k2, 1.0f) * k);
            }
            if (cov > 0) put(g, x, y, rowc, cov);
        }
    }
}

void gfx_line(gfx_t *g, float x0, float y0, float x1, float y1, float thick, uint32_t argb)
{
    float vx = x1 - x0, vy = y1 - y0;
    float len2 = vx * vx + vy * vy;
    float inv = len2 > 1e-6f ? 1.0f / len2 : 0.0f;
    float rad = thick * 0.5f;

    int bx0 = (int)floorf((x0 < x1 ? x0 : x1) - rad - 1);
    int bx1 = (int)ceilf((x0 > x1 ? x0 : x1) + rad + 1);
    int by0 = (int)floorf((y0 < y1 ? y0 : y1) - rad - 1);
    int by1 = (int)ceilf((y0 > y1 ? y0 : y1) + rad + 1);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > g->w) bx1 = g->w;
    if (by1 > g->h) by1 = g->h;

    for (int y = by0; y < by1; y++) {
        float py = y + 0.5f;
        for (int x = bx0; x < bx1; x++) {
            float px = x + 0.5f;
            float t = ((px - x0) * vx + (py - y0) * vy) * inv;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float dx = px - (x0 + t * vx);
            float dy = py - (y0 + t * vy);
            int cov = cov_from_dist(dist_approx(dx * dx + dy * dy, rad));
            if (cov) put(g, x, y, argb, cov);
        }
    }
}

void gfx_polyline(gfx_t *g, const float *pts, int n, float thick, uint32_t argb)
{
    for (int i = 0; i + 1 < n; i++) {
        gfx_line(g, pts[i * 2], pts[i * 2 + 1], pts[i * 2 + 2], pts[i * 2 + 3], thick, argb);
    }
}

void gfx_arc(gfx_t *g, float cx, float cy, float r, float thick, float a0, float a1, uint32_t argb)
{
    if (a1 < a0) {
        float t = a0;
        a0 = a1;
        a1 = t;
    }
    float span = a1 - a0;
    int segs = (int)(span / 2.0f);
    if (segs < 1) segs = 1;
    if (segs > 180) segs = 180;

    float prev_x = cx + cosf(a0 * 0.0174532925f) * r;
    float prev_y = cy + sinf(a0 * 0.0174532925f) * r;
    for (int i = 1; i <= segs; i++) {
        float a = (a0 + span * i / segs) * 0.0174532925f;
        float x = cx + cosf(a) * r;
        float y = cy + sinf(a) * r;
        gfx_line(g, prev_x, prev_y, x, y, thick, argb);
        prev_x = x;
        prev_y = y;
    }
}

/* ------------------------------------------------------------------ */
/* 文字                                                                 */
/* ------------------------------------------------------------------ */

static uint32_t utf8_next(const char **pp)
{
    const unsigned char *s = (const unsigned char *)*pp;
    uint32_t cp;
    if (s[0] < 0x80) {
        cp = s[0];
        *pp += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *pp += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *pp += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
             ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *pp += 4;
    } else {
        cp = '?';
        *pp += 1;
    }
    return cp;
}

static const mochi_glyph_t *find_glyph(const mochi_font_t *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

float gfx_text_width(const mochi_font_t *f, const char *utf8)
{
    float w = 0;
    while (*utf8) {
        uint32_t cp = utf8_next(&utf8);
        const mochi_glyph_t *gl = find_glyph(f, cp);
        if (gl) {
            w += gl->adv;
        } else {
            w += f->line_h * 0.5f;
        }
    }
    return w;
}

void gfx_text(gfx_t *g, const mochi_font_t *f, const char *utf8, float x, float y, int align,
              uint32_t argb)
{
    float pen = x;
    if (align == GFX_ALIGN_CENTER) {
        pen -= gfx_text_width(f, utf8) * 0.5f;
    } else if (align == GFX_ALIGN_RIGHT) {
        pen -= gfx_text_width(f, utf8);
    }
    int base = (int)lroundf(y);

    while (*utf8) {
        uint32_t cp = utf8_next(&utf8);
        const mochi_glyph_t *gl = find_glyph(f, cp);
        if (!gl) continue;

        int gx = (int)lroundf(pen) + gl->bx;
        int gy = base + gl->by;
        const uint8_t *src = f->bitmap + gl->off;

        int ca = (int)(argb >> 24) * g->alpha / 255;
        int y_start = 0, y_end = gl->h;
        int x_start = 0, x_end = gl->w;
        if (gy < 0) y_start = -gy;
        if (gy + gl->h > g->h) y_end = g->h - gy;
        if (gx < 0) x_start = -gx;
        if (gx + gl->w > g->w) x_end = g->w - gx;

        for (int row = y_start; row < y_end; row++) {
            const uint8_t *s = src + row * gl->w;
            uint16_t *dst = &g->px[(gy + row) * g->w + gx];
            for (int col = x_start; col < x_end; col++) {
                int a = s[col];
                if (!a) continue;
                dst[col] = blend565(dst[col], argb, ca * a / 255);
            }
        }
        pen += gl->adv;
    }
}
