#include "icons.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "icons";

#define NAME_LEN 28
#define ENTRY_LEN (NAME_LEN + 24)
#define MAX_SIDE 128
#define MAX_FRAMES 16
#define NSLOT 4
#define PACK_PATH "/sdcard/icons.bin"

typedef struct {
    char key[NAME_LEN + 8];
    uint8_t *frames;
    int w, h, nf, delay;
    int64_t t0;
    uint32_t used;
} slot_t;

static FILE *s_f;
static int s_count;
static slot_t s_slot[NSLOT];
static uint32_t s_tick;

static inline uint16_t blend565(uint16_t dst, uint16_t src, int a)
{
    int sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;
    int dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
    dr += (sr - dr) * a / 256;
    dg += (sg - dg) * a / 256;
    db += (sb - db) * a / 256;
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool icons_init(void)
{
    if (s_f) {
        fclose(s_f);
        s_f = NULL;
    }
    s_f = fopen(PACK_PATH, "rb");
    if (!s_f) {
        ESP_LOGW(TAG, "no %s", PACK_PATH);
        return false;
    }
    uint8_t hdr[8];
    if (fread(hdr, 1, 8, s_f) != 8 || memcmp(hdr, "ICN1", 4) != 0) {
        fclose(s_f);
        s_f = NULL;
        return false;
    }
    s_count = (int)rd32(hdr + 4);
    if (s_count <= 0) {
        fclose(s_f);
        s_f = NULL;
        return false;
    }
    for (int i = 0; i < NSLOT; i++) s_slot[i].key[0] = 0;
    ESP_LOGI(TAG, "%d icons from SD", s_count);
    return true;
}

/* 在缓存里找，找不到就踢掉最久没用的槽位加载 */
static slot_t *lookup(const char *key)
{
    if (!s_f) return NULL;

    for (int i = 0; i < NSLOT; i++) {
        if (!strcmp(s_slot[i].key, key)) {
            s_slot[i].used = ++s_tick;
            return &s_slot[i];
        }
    }

    /* 选最久没用过的槽位 */
    int victim = 0;
    for (int i = 1; i < NSLOT; i++) {
        if (s_slot[i].used < s_slot[victim].used) victim = i;
    }
    slot_t *sl = &s_slot[victim];

    for (int i = 0; i < s_count; i++) {
        uint8_t e[ENTRY_LEN];
        if (fseek(s_f, 8 + (long)i * ENTRY_LEN, SEEK_SET) != 0) return NULL;
        if (fread(e, 1, ENTRY_LEN, s_f) != ENTRY_LEN) return NULL;

        char name[NAME_LEN + 1];
        memcpy(name, e, NAME_LEN);
        name[NAME_LEN] = 0;
        if (strcmp(name, key) != 0) continue;

        uint32_t off = rd32(e + 28);
        uint32_t size = rd32(e + 32);
        uint32_t w = rd32(e + 36);
        uint32_t h = rd32(e + 40);
        uint32_t nf = rd32(e + 44);
        uint32_t delay = rd32(e + 48);
        if (w == 0 || h == 0 || w > MAX_SIDE || h > MAX_SIDE) return NULL;
        if (nf == 0 || nf > MAX_FRAMES) return NULL;
        if (size != w * h * 3 * nf) return NULL;

        size_t need = MAX_SIDE * MAX_SIDE * 3 * MAX_FRAMES;
        if (!sl->frames) {
            sl->frames = heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
            if (!sl->frames) return NULL;
        }
        if (fseek(s_f, (long)off, SEEK_SET) != 0) return NULL;
        if (fread(sl->frames, 1, size, s_f) != size) return NULL;

        sl->w = (int)w;
        sl->h = (int)h;
        sl->nf = (int)nf;
        sl->delay = (int)delay;
        sl->t0 = esp_timer_get_time();
        sl->used = ++s_tick;
        snprintf(sl->key, sizeof(sl->key), "%s", key);
        return sl;
    }
    return NULL;
}

bool icons_draw(gfx_t *g, const char *name, int size, float cx, float cy)
{
    /* 包里只有 120 和 64 两档，按需要的尺寸就近取一份再缩放，避免无效查找 */
    int src = (size >= 96) ? 120 : 64;
    char key[NAME_LEN + 8];
    snprintf(key, sizeof(key), "%s@%d", name, src);
    slot_t *sl = lookup(key);
    if (!sl) return false;

    int idx = 0;
    if (sl->nf > 1 && sl->delay > 0) {
        int64_t ms = (esp_timer_get_time() - sl->t0) / 1000;
        idx = (int)((ms / sl->delay) % sl->nf);
    }

    const uint8_t *frame = sl->frames + (size_t)idx * sl->w * sl->h * 3;
    int dw = size, dh = size;
    int x0 = (int)(cx - dw / 2.0f);
    int y0 = (int)(cy - dh / 2.0f);

    for (int y = 0; y < dh; y++) {
        int dy = y0 + y;
        if (dy < 0 || dy >= g->h) continue;
        int sy = (sl->h == dh) ? y : y * sl->h / dh;
        const uint8_t *src = frame + (size_t)sy * sl->w * 3;
        uint16_t *dst = &g->px[dy * g->w];
        for (int x = 0; x < dw; x++) {
            int sx = (sl->w == dw) ? x : x * sl->w / dw;
            int a = src[sx * 3 + 2];
            if (a == 0) continue;
            int dx = x0 + x;
            if (dx < 0 || dx >= g->w) continue;
            uint16_t rgb = (uint16_t)(src[sx * 3] | (src[sx * 3 + 1] << 8));
            dst[dx] = (a >= 250) ? rgb : blend565(dst[dx], rgb, a);
        }
    }
    return true;
}
