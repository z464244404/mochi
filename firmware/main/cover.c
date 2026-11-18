#include "cover.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "cover";

static uint16_t *s_px;
static int s_side;
static long s_mtime;
static long s_size;

bool cover_load(const char *path, int side)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        if (s_px) {
            heap_caps_free(s_px);
            s_px = NULL;
            s_side = 0;
        }
        return false;
    }
    if (s_px && s_side == side && st.st_mtime == s_mtime && st.st_size == s_size) return true;

    size_t need = (size_t)side * side * 2;
    if ((size_t)st.st_size != need) {
        ESP_LOGW(TAG, "cover size %ld != %u (side %d)", (long)st.st_size, (unsigned)need, side);
        return false;
    }

    if (!s_px || s_side != side) {
        if (s_px) heap_caps_free(s_px);
        s_px = heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
        if (!s_px) {
            s_side = 0;
            return false;
        }
    }

    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(s_px, 1, need, f);
    fclose(f);
    if (got != need) {
        ESP_LOGW(TAG, "cover read %u/%u", (unsigned)got, (unsigned)need);
        return false;
    }

    s_side = side;
    s_mtime = st.st_mtime;
    s_size = st.st_size;
    ESP_LOGI(TAG, "cover loaded %dx%d", side, side);
    return true;
}

bool cover_draw(gfx_t *g, int cx, int cy, int size)
{
    if (!s_px || s_side <= 0) return false;

    int r = size / 2;
    if (r <= 0) return false;
    float scale = (float)s_side / (float)size;

    for (int dy = -r; dy < r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= g->h) continue;
        /* 圆形裁剪：这一行的可用半宽 */
        int half = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - half, x1 = cx + half;
        if (x0 < 0) x0 = 0;
        if (x1 > g->w) x1 = g->w;
        if (x0 >= x1) continue;

        int sy = (int)((dy + r) * scale);
        if (sy < 0) sy = 0;
        if (sy >= s_side) sy = s_side - 1;
        const uint16_t *row = s_px + (size_t)sy * s_side;

        uint16_t *dst = &g->px[(size_t)y * g->w];
        for (int x = x0; x < x1; x++) {
            int sx = (int)((x - (cx - r)) * scale);
            if (sx < 0) sx = 0;
            if (sx >= s_side) sx = s_side - 1;
            dst[x] = row[sx];
        }
    }
    return true;
}

/* 旋转版：封面当成唱片，按 deg 角度反向采样。
 * 先圆裁再旋转 —— 正方形图旋转会露出黑角，圆形遮罩天生无缝。 */
bool cover_draw_rot(gfx_t *g, int cx, int cy, int size, float deg)
{
    if (!s_px || s_side <= 0) return false;

    int r = size / 2;
    if (r <= 0) return false;

    /* 源坐标用 8.8 定点（最大 240*256 = 61440，不会溢出）：
     * 每行的起点用一次浮点算，之后逐像素只做整数加法。 */
    float sc = (float)s_side / (float)size;
    float a = deg * 3.14159265f / 180.0f;
    float ca = cosf(a), sa = sinf(a);
    const float half_src = (float)s_side * 0.5f;
    const int32_t step_x = (int32_t)(ca * sc * 256.0f);
    const int32_t step_y = (int32_t)(-sa * sc * 256.0f);

    for (int dy = -r; dy < r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= g->h) continue;

        int half = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - half, x1 = cx + half;
        if (x0 < 0) x0 = 0;
        if (x1 > g->w) x1 = g->w;
        if (x0 >= x1) continue;

        float dx = (float)(x0 - cx);
        float dyf = (float)dy;
        int32_t sxf = (int32_t)(((dx * ca + dyf * sa) * sc + half_src) * 256.0f);
        int32_t syf = (int32_t)(((-dx * sa + dyf * ca) * sc + half_src) * 256.0f);

        uint16_t *dst = &g->px[(size_t)y * g->w];

        for (int x = x0; x < x1; x++) {
            /* 圆内接于方形，采样点必然落在源图内；只有四舍五入的边角可能越界一像素，
             * 用钳制代替分支判断，少一次跳转。 */
            int sx = sxf >> 8;
            int sy = syf >> 8;
            sx = sx < 0 ? 0 : (sx >= s_side ? s_side - 1 : sx);
            sy = sy < 0 ? 0 : (sy >= s_side ? s_side - 1 : sy);
            dst[x] = s_px[(size_t)sy * s_side + sx];
            sxf += step_x;
            syf += step_y;
        }
    }
    return true;
}

size_t cover_mem_bytes(void)
{
    return s_px ? (size_t)s_side * s_side * 2 : 0;
}
