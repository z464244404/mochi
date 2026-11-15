#include "fontpack.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "fontpack";

#define FP_PATH "/sdcard/fonts/fontpack.bin"
#define FP_ENT 20
#define FP_SLOT_PX 48
#define FP_SLOTS 12

static FILE *s_f;
static uint8_t *s_idx;
static int s_count;
static uint16_t s_px;
static uint32_t s_data_off;

static uint8_t *s_cache; /* FP_SLOTS * FP_SLOT_PX * FP_SLOT_PX */
static uint32_t s_cache_cp[FP_SLOTS];
static int s_cache_next;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

bool fp_init(void)
{
    if (s_f) {
        fclose(s_f);
        s_f = NULL;
    }
    if (s_idx) {
        heap_caps_free(s_idx);
        s_idx = NULL;
    }
    s_count = 0;

    if (!s_cache) {
        s_cache = heap_caps_malloc((size_t)FP_SLOTS * FP_SLOT_PX * FP_SLOT_PX, MALLOC_CAP_SPIRAM);
        if (!s_cache) {
            ESP_LOGE(TAG, "no memory for glyph cache");
            return false;
        }
        for (int i = 0; i < FP_SLOTS; i++) s_cache_cp[i] = 0;
    }

    s_f = fopen(FP_PATH, "rb");
    if (!s_f) {
        ESP_LOGI(TAG, "no %s, 内置字体照常工作", FP_PATH);
        return false;
    }

    uint8_t hdr[20];
    if (fread(hdr, 1, 20, s_f) != 20 || memcmp(hdr, "FTP1", 4) != 0) {
        ESP_LOGW(TAG, "bad header");
        fclose(s_f);
        s_f = NULL;
        return false;
    }
    s_px = rd16(hdr + 4);
    s_count = (int)rd32(hdr + 8);
    uint32_t index_off = rd32(hdr + 12);
    s_data_off = rd32(hdr + 16);

    if (s_count <= 0 || s_count > 20000) {
        ESP_LOGW(TAG, "bad count %d", s_count);
        fclose(s_f);
        s_f = NULL;
        return false;
    }

    s_idx = heap_caps_malloc((size_t)s_count * FP_ENT, MALLOC_CAP_SPIRAM);
    if (!s_idx) {
        ESP_LOGE(TAG, "no memory for index");
        fclose(s_f);
        s_f = NULL;
        s_count = 0;
        return false;
    }
    if (fseek(s_f, (long)index_off, SEEK_SET) != 0 ||
        fread(s_idx, FP_ENT, s_count, s_f) != (size_t)s_count) {
        ESP_LOGE(TAG, "index read failed");
        heap_caps_free(s_idx);
        s_idx = NULL;
        fclose(s_f);
        s_f = NULL;
        s_count = 0;
        return false;
    }

    ESP_LOGI(TAG, "%d glyphs @ %dpx from SD", s_count, (int)s_px);
    return true;
}

int fp_count(void)
{
    return s_count;
}

/* 二分查找，返回条目指针 */
static const uint8_t *fp_find(uint32_t cp)
{
    if (!s_idx || s_count <= 0) return NULL;
    int lo = 0, hi = s_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const uint8_t *p = s_idx + (size_t)mid * FP_ENT;
        uint32_t c = rd32(p);
        if (c == cp) return p;
        if (c < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

bool fp_adv(uint32_t cp, uint16_t *adv)
{
    const uint8_t *e = fp_find(cp);
    if (!e) return false;
    *adv = rd16(e + 16);
    return true;
}

bool fp_glyph(uint32_t cp, mochi_glyph_t *out, const uint8_t **bits)
{
    const uint8_t *e = fp_find(cp);
    if (!e) return false;

    uint32_t off = rd32(e + 4);
    uint16_t w = rd16(e + 8);
    uint16_t h = rd16(e + 10);
    int16_t bx = (int16_t)rd16(e + 12);
    int16_t by = (int16_t)rd16(e + 14);
    uint16_t adv = rd16(e + 16);

    out->cp = cp;
    out->w = w;
    out->h = h;
    out->bx = bx;
    out->by = by;
    out->adv = adv;
    out->off = 0;

    if (w == 0 || h == 0) {
        *bits = NULL;
        return true; /* 空格之类：只占宽度 */
    }
    if (w > FP_SLOT_PX || h > FP_SLOT_PX) return false;

    for (int i = 0; i < FP_SLOTS; i++) {
        if (s_cache_cp[i] == cp) {
            *bits = s_cache + (size_t)i * FP_SLOT_PX * FP_SLOT_PX;
            return true;
        }
    }

    int slot = s_cache_next;
    s_cache_next = (s_cache_next + 1) % FP_SLOTS;
    uint8_t *dst = s_cache + (size_t)slot * FP_SLOT_PX * FP_SLOT_PX;
    if (fseek(s_f, (long)(s_data_off + off), SEEK_SET) != 0) return false;
    if (fread(dst, 1, (size_t)w * h, s_f) != (size_t)w * h) return false;
    s_cache_cp[slot] = cp;
    *bits = dst;
    return true;
}
