#include "fontpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "fontpack";

#define FP_MAX 2
#define FP_ENT 20
#define FP_SLOT_PX 48
#define FP_SLOTS 12

typedef struct {
    FILE *f;
    uint8_t *idx;
    int count;
    uint16_t px;
    uint32_t data_off;
} pack_t;

static pack_t s_packs[FP_MAX];
static int s_npacks;

static uint8_t *s_cache;
static struct {
    uint32_t cp;
    int8_t pack;
    uint8_t used;
} s_cache_key[FP_SLOTS];
static int s_cache_next;

static const char *PACK_FILES[FP_MAX] = {"/sdcard/fonts/fontpack.bin",
                                         "/sdcard/fonts/fontpack22.bin"};

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void pack_close(pack_t *p)
{
    if (p->f) fclose(p->f);
    if (p->idx) heap_caps_free(p->idx);
    memset(p, 0, sizeof(*p));
}

bool fp_init(void)
{
    for (int i = 0; i < s_npacks; i++) pack_close(&s_packs[i]);
    s_npacks = 0;
    if (s_cache) {
        for (int i = 0; i < FP_SLOTS; i++) s_cache_key[i].used = 0;
    } else {
        s_cache = heap_caps_malloc((size_t)FP_SLOTS * FP_SLOT_PX * FP_SLOT_PX, MALLOC_CAP_SPIRAM);
        if (!s_cache) {
            ESP_LOGE(TAG, "no memory for glyph cache");
            return false;
        }
        for (int i = 0; i < FP_SLOTS; i++) s_cache_key[i].used = 0;
    }

    for (int fi = 0; fi < FP_MAX; fi++) {
        FILE *f = fopen(PACK_FILES[fi], "rb");
        if (!f) continue;

        uint8_t hdr[20];
        if (fread(hdr, 1, 20, f) != 20 || memcmp(hdr, "FTP1", 4) != 0) {
            ESP_LOGW(TAG, "%s: bad header", PACK_FILES[fi]);
            fclose(f);
            continue;
        }
        uint16_t px = rd16(hdr + 4);
        int count = (int)rd32(hdr + 8);
        uint32_t index_off = rd32(hdr + 12);
        uint32_t data_off = rd32(hdr + 16);
        if (count <= 0 || count > 20000) {
            ESP_LOGW(TAG, "%s: bad count %d", PACK_FILES[fi], count);
            fclose(f);
            continue;
        }
        uint8_t *idx = heap_caps_malloc((size_t)count * FP_ENT, MALLOC_CAP_SPIRAM);
        if (!idx) {
            fclose(f);
            continue;
        }
        if (fseek(f, (long)index_off, SEEK_SET) != 0 ||
            fread(idx, FP_ENT, count, f) != (size_t)count) {
            ESP_LOGE(TAG, "%s: index read failed", PACK_FILES[fi]);
            heap_caps_free(idx);
            fclose(f);
            continue;
        }

        pack_t *p = &s_packs[s_npacks++];
        p->f = f;
        p->idx = idx;
        p->count = count;
        p->px = px;
        p->data_off = data_off;
        ESP_LOGI(TAG, "%s: %d glyphs @ %dpx", PACK_FILES[fi], count, (int)px);
    }

    if (s_npacks == 0) ESP_LOGI(TAG, "没有字体包，只用内置字形");
    return s_npacks > 0;
}

int fp_count(void)
{
    int n = 0;
    for (int i = 0; i < s_npacks; i++) n += s_packs[i].count;
    return n;
}

int fp_count_px(int px)
{
    for (int i = 0; i < s_npacks; i++) {
        if (s_packs[i].px == px) return s_packs[i].count;
    }
    return 0;
}

/* 选字号最接近的一套；同距离优先大的 */
static int pick_pack(int size)
{
    int best = -1, best_diff = 1 << 20;
    for (int i = 0; i < s_npacks; i++) {
        int diff = abs((int)s_packs[i].px - size);
        if (diff < best_diff || (diff == best_diff && best >= 0 &&
                                 s_packs[i].px > s_packs[best].px)) {
            best = i;
            best_diff = diff;
        }
    }
    return best;
}

static const uint8_t *pack_find(int pi, uint32_t cp)
{
    const pack_t *p = &s_packs[pi];
    int lo = 0, hi = p->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const uint8_t *e = p->idx + (size_t)mid * FP_ENT;
        uint32_t c = rd32(e);
        if (c == cp) return e;
        if (c < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

/* 先在最合适的包里找，找不到再试另一套 */
static const uint8_t *fp_find(uint32_t cp, int size, int *out_pack)
{
    if (s_npacks == 0) return NULL;
    int pi = pick_pack(size);
    for (int k = 0; k < s_npacks; k++) {
        int i = (k == 0) ? pi : (pi + k) % s_npacks;
        if (i < 0) continue;
        const uint8_t *e = pack_find(i, cp);
        if (e) {
            if (out_pack) *out_pack = i;
            return e;
        }
    }
    return NULL;
}

bool fp_adv(uint32_t cp, int size, uint16_t *adv)
{
    const uint8_t *e = fp_find(cp, size, NULL);
    if (!e) return false;
    *adv = rd16(e + 16);
    return true;
}

bool fp_glyph(uint32_t cp, int size, mochi_glyph_t *out, const uint8_t **bits)
{
    int pi = 0;
    const uint8_t *e = fp_find(cp, size, &pi);
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
        return true;
    }
    if (w > FP_SLOT_PX || h > FP_SLOT_PX) return false;

    for (int i = 0; i < FP_SLOTS; i++) {
        if (s_cache_key[i].used && s_cache_key[i].cp == cp && s_cache_key[i].pack == pi) {
            *bits = s_cache + (size_t)i * FP_SLOT_PX * FP_SLOT_PX;
            return true;
        }
    }

    int slot = s_cache_next;
    s_cache_next = (s_cache_next + 1) % FP_SLOTS;
    uint8_t *dst = s_cache + (size_t)slot * FP_SLOT_PX * FP_SLOT_PX;
    FILE *f = s_packs[pi].f;
    if (fseek(f, (long)(s_packs[pi].data_off + off), SEEK_SET) != 0) return false;
    if (fread(dst, 1, (size_t)w * h, f) != (size_t)w * h) return false;

    s_cache_key[slot].cp = cp;
    s_cache_key[slot].pack = (int8_t)pi;
    s_cache_key[slot].used = 1;
    *bits = dst;
    return true;
}

size_t fontpack_mem_bytes(void)
{
    size_t n = 0;
    for (int i = 0; i < s_npacks; i++) n += (size_t)s_packs[i].count * FP_ENT;
    if (s_cache) n += (size_t)FP_SLOTS * FP_SLOT_PX * FP_SLOT_PX;
    return n;
}
