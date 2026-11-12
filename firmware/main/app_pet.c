#include <stdio.h>
#include <string.h>

#include "app.h"
#include "dsec.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gfx.h"
#include "sd.h"

/**
 * 桌宠：播放 MJP1（连续 JPEG 帧）序列
 *
 * 素材在 TF 卡 /sdcard/pet.mjp。整段 5MB 塞进 PSRAM 太浪费，
 * 改成「12 个槽的环形缓冲 + 后台预读任务」：
 *   实测 40MHz 下单卡顺序读 1.23MB/s，播放消耗约 0.86MB/s，够喂。
 * 解码直接写 LCD 帧缓冲（零拷贝）。
 */

#define SLOT_MAX (48 * 1024) /* 最大帧 45.6KB */
#define RING 12
#define SD_PATH "/sdcard/pet.mjp"
#define MAX_CLIP (8 * 1024 * 1024)
#define CHUNK (128 * 1024)

/* 三段动画：吃饭 / 工作 / 睡觉 */
enum { CLIP_EAT = 0, CLIP_WORK, CLIP_SLEEP, CLIP_COUNT };
static const char *CLIP_PATH[CLIP_COUNT] = {
    "/sdcard/pet.mjp",
    "/sdcard/work.mjp",
    "/sdcard/sleep.mjp",
};
static int s_clip = -1;
static int s_want = CLIP_EAT;

static const char *TAG = "pet";

typedef enum { ST_CHECK, ST_READY, ST_ERROR } pet_state_t;

static pet_state_t s_state = ST_CHECK;

/* 素材表 */
static uint16_t s_n, s_fps;
static uint32_t *s_offs;
static uint32_t *s_sizes;

/* 环形缓冲：producer 读卡，consumer 解码 */
static uint8_t *s_ring;
static uint16_t s_slot_len[RING];
static int s_head, s_tail;
static SemaphoreHandle_t s_free_slots, s_filled;
static volatile bool s_stop;
static FILE *s_src;
static TaskHandle_t s_prod_task;

static jpeg_dec_handle_t s_dec;
static uint8_t *s_jpg;
static int64_t s_next_us; /* 按标称帧率节流，避免解码快于素材帧率时播放变快 */

/* ------------------------------------------------------------------ */

static void draw_error(gfx_t *g)
{
    gfx_background(g, GFX_RGB(0x1a, 0x0b, 0x0d), GFX_RGB(0x10, 0x08, 0x0a),
                   GFX_RGB(0x08, 0x05, 0x06));
    gfx_text(g, &font_ui_29, "NO ASSET", 240, 226, GFX_ALIGN_CENTER, GFX_RGB(0xff, 0x8a, 0x9a));
    gfx_text(g, &font_ui_29, "put pet.mjp in /", 240, 270, GFX_ALIGN_CENTER,
             GFX_RGB(0x8a, 0x7a, 0x7e));
}

/* ---------------- 环形缓冲 ---------------- */

static void producer_task(void *arg)
{
    int fi = 0;
    for (;;) {
        if (xSemaphoreTake(s_free_slots, portMAX_DELAY) != pdTRUE) break;
        if (s_stop) {
            xSemaphoreGive(s_free_slots);
            break;
        }
        int slot = s_head;
        uint32_t sz = s_sizes[fi];
        size_t n = 0;
        if (fseek(s_src, s_offs[fi], SEEK_SET) == 0) {
            n = fread(s_ring + (size_t)slot * SLOT_MAX, 1, sz, s_src);
        }
        s_slot_len[slot] = (uint16_t)n;
        s_head = (slot + 1) % RING;
        fi = (fi + 1) % s_n;
        xSemaphoreGive(s_filled);
        if (n != sz) {
            ESP_LOGW(TAG, "short read %u/%u", (unsigned)n, (unsigned)sz);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    s_prod_task = NULL;
    vTaskDelete(NULL);
}

/* 解析头 + 帧表，起 producer */
static bool clip_start(FILE *f)
{
    uint8_t hdr[32];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || memcmp(hdr, "MJP1", 4) != 0) return false;

    uint16_t w = (uint16_t)(hdr[4] | (hdr[5] << 8));
    uint16_t h = (uint16_t)(hdr[6] | (hdr[7] << 8));
    s_n = (uint16_t)(hdr[8] | (hdr[9] << 8));
    s_fps = (uint16_t)(hdr[10] | (hdr[11] << 8));
    uint32_t total = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8) | ((uint32_t)hdr[14] << 16) |
                     ((uint32_t)hdr[15] << 24);
    uint32_t table_off = (uint32_t)hdr[16] | ((uint32_t)hdr[17] << 8) |
                         ((uint32_t)hdr[18] << 16) | ((uint32_t)hdr[19] << 24);

    if (w != MOCHI_W || h != MOCHI_H || s_n == 0 || s_n > 2000 || total > MAX_CLIP ||
        table_off + s_n * 8 > total) {
        return false;
    }

    s_offs = heap_caps_malloc(s_n * 4, MALLOC_CAP_INTERNAL);
    s_sizes = heap_caps_malloc(s_n * 4, MALLOC_CAP_INTERNAL);
    if (!s_offs || !s_sizes) return false;

    if (fseek(f, table_off, SEEK_SET) != 0) return false;
    for (int i = 0; i < s_n; i++) {
        uint8_t e[8];
        if (fread(e, 1, 8, f) != 8) return false;
        s_offs[i] = (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) |
                    ((uint32_t)e[3] << 24);
        s_sizes[i] = (uint32_t)e[4] | ((uint32_t)e[5] << 8) | ((uint32_t)e[6] << 16) |
                     ((uint32_t)e[7] << 24);
        if (s_sizes[i] == 0 || s_sizes[i] > SLOT_MAX) return false;
    }

    if (!s_ring) {
        s_ring = heap_caps_malloc((size_t)RING * SLOT_MAX, MALLOC_CAP_SPIRAM);
        if (!s_ring) return false;
    }
    if (!s_free_slots) {
        s_free_slots = xSemaphoreCreateCounting(RING, RING);
        s_filled = xSemaphoreCreateCounting(RING, 0);
        if (!s_free_slots || !s_filled) return false;
    }

    s_src = f;
    s_head = s_tail = 0;
    s_stop = false;
    if (xTaskCreatePinnedToCore(producer_task, "pet_prod", 4096, NULL, 3, &s_prod_task, 1) !=
        pdPASS) {
        return false;
    }

    s_state = ST_READY;
    ESP_LOGI(TAG, "stream from SD: %u frames @ %u fps, ring %u KB", s_n, s_fps,
             (unsigned)((RING * SLOT_MAX) / 1024));
    return true;
}

/* 停掉当前片段：让 producer 退出、关文件、把环形缓冲清干净 */
static void clip_stop(void)
{
    if (s_prod_task) {
        s_stop = true;
        xSemaphoreGive(s_free_slots); /* 唤醒卡在满队列上的 producer */
        for (int i = 0; i < 200 && s_prod_task; i++) vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (s_src) {
        fclose(s_src);
        s_src = NULL;
    }
    if (s_offs) {
        heap_caps_free(s_offs);
        s_offs = NULL;
    }
    if (s_sizes) {
        heap_caps_free(s_sizes);
        s_sizes = NULL;
    }
    if (s_filled) {
        while (xSemaphoreTake(s_filled, 0) == pdTRUE) {
        }
    }
    if (s_free_slots) {
        while (xSemaphoreTake(s_free_slots, 0) == pdTRUE) {
        }
        for (int i = 0; i < RING; i++) xSemaphoreGive(s_free_slots);
    }
    s_head = s_tail = 0;
    s_next_us = 0;
    s_clip = -1;
}

static bool clip_open(int idx)
{
    if (idx < 0 || idx >= CLIP_COUNT) return false;
    FILE *f = fopen(CLIP_PATH[idx], "rb");
    if (!f) {
        ESP_LOGW(TAG, "missing %s", CLIP_PATH[idx]);
        return false;
    }
    if (!clip_start(f)) {
        fclose(f);
        return false;
    }
    s_clip = idx;
    ESP_LOGI(TAG, "clip: %s", CLIP_PATH[idx]);
    return true;
}

/* 想要哪一段：余额在掉→工作，5 分钟没变→吃饭，30 分钟没变→睡觉 */
static int clip_from_mood(void)
{
    switch (dsec_mood()) {
    case DSEC_MOOD_WORK: return CLIP_WORK;
    case DSEC_MOOD_SLEEP: return CLIP_SLEEP;
    default: return CLIP_EAT;
    }
}

/* ---------------- 解码 ---------------- */

static bool decode_to(const uint8_t *jpg, uint32_t len, uint16_t *dst)
{
    if (!s_dec || len == 0 || len > SLOT_MAX) return false;

    jpeg_dec_io_t io;
    jpeg_dec_header_info_t info;
    memset(&io, 0, sizeof(io));
    memset(&info, 0, sizeof(info));
    io.inbuf = (uint8_t *)jpg;
    io.inbuf_len = (int)len;
    if (jpeg_dec_parse_header(s_dec, &io, &info) != JPEG_ERR_OK) return false;
    io.outbuf = (uint8_t *)dst;
    return jpeg_dec_process(s_dec, &io) == JPEG_ERR_OK;
}

static void dim(uint16_t *px, uint8_t a)
{
    int n = MOCHI_W * MOCHI_H;
    for (int i = 0; i < n; i++) {
        uint16_t p = px[i];
        int r = ((p >> 11) & 0x1F) * a / 255;
        int g = ((p >> 5) & 0x3F) * a / 255;
        int b = (p & 0x1F) * a / 255;
        px[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

/* ---------------- app ---------------- */

static void pet_enter(void)
{
    if (!s_dec) {
        jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
        cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
        if (jpeg_dec_open(&cfg, &s_dec) != JPEG_ERR_OK) s_dec = NULL;
    }
    if (!s_jpg) {
        s_jpg = heap_caps_malloc(64 * 1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_state == ST_ERROR) s_state = ST_CHECK;
    dsec_touch();
}

/* ---------------- app ---------------- */
/* 素材是黑底，文字用浅色 + 一层暗描边，不加底色块也看得清 */
static void hud_text(gfx_t *g, const char *s, float x, float y, uint32_t color)
{
    gfx_text(g, &font_ui_29, s, x + 1.5f, y + 1.5f, GFX_ALIGN_CENTER, 0xC0000000u);
    gfx_text(g, &font_ui_29, s, x, y, GFX_ALIGN_CENTER, color);
}

static void draw_hud(gfx_t *g)
{
    const float label_y = 208.0f, value_y = 244.0f;
    const float left_x = 46.0f, right_x = 434.0f;

    if (!dsec_has_key()) {
        hud_text(g, "SET API KEY", 240, 240, 0xFFFFC86B);
        return;
    }

    float bal = dsec_balance();
    if (bal < 0.0f) {
        hud_text(g, dsec_status(), 240, 240, 0xFFFFC86B);
        return;
    }

    char buf[28];

    /* 左：余额（标题一行 + 数字一行） */
    hud_text(g, "余额", left_x, label_y, 0xFFC8D2DA);
    if (bal < 100.0f) snprintf(buf, sizeof(buf), "%.2f", bal);
    else snprintf(buf, sizeof(buf), "%.1f", bal);
    hud_text(g, buf, left_x, value_y, 0xFFFFFFFF);

    /* 右：今日用量 */
    float td = dsec_today();
    hud_text(g, "今日", right_x, label_y, 0xFFC8D2DA);
    if (td < 0.0f) {
        snprintf(buf, sizeof(buf), "--");
    } else if (td < 100.0f) {
        snprintf(buf, sizeof(buf), "%.2f", td);
    } else {
        snprintf(buf, sizeof(buf), "%.1f", td);
    }
    hud_text(g, buf, right_x, value_y, 0xFFFFFFFF);

    /* 正下方：峰 / 谷 */
    bool peak = dsec_peak();
    gfx_round_rect(g, 240, 447, 104, 34, 17.0f, gfx_solid(peak ? 0xE0A8762A : 0xE01F5A4C));
    gfx_text(g, &font_ui_29, peak ? "峰" : "谷", 240, 447, GFX_ALIGN_CENTER,
             peak ? GFX_RGB(0xFF, 0xE6, 0xB8) : GFX_RGB(0xB8, 0xF5, 0xE4));
}

static void pet_draw(gfx_t *g, float dt)
{
    (void)dt;

    switch (s_state) {
    case ST_CHECK: {
        if (!sd_ready()) {
            s_state = ST_ERROR;
            break;
        }
        s_want = clip_from_mood();
        if (!clip_open(s_want) && !clip_open(CLIP_EAT)) {
            ESP_LOGE(TAG, "no clip on SD");
            s_state = ST_ERROR;
        }
        break;
    }

    case ST_ERROR:
        draw_error(g);
        return;

    case ST_READY:
        break;
    }

    if (s_state != ST_READY) {
        gfx_clear(g, 0xFF000000);
        return;
    }

    /* 余额状态变了就换片段（工作 / 吃饭 / 睡觉） */
    s_want = clip_from_mood();
    if (s_want != s_clip) {
        clip_stop();
        if (!clip_open(s_want) && !clip_open(CLIP_EAT)) {
            s_state = ST_ERROR;
            gfx_clear(g, 0xFF000000);
            return;
        }
    }

    /* 解码比素材帧率快时，等到该帧的时间点再画；面板会一直扫上一帧，不会闪 */
    int64_t now = esp_timer_get_time();
    if (s_next_us == 0) s_next_us = now;
    if (now < s_next_us) {
        vTaskDelay(pdMS_TO_TICKS((s_next_us - now + 999) / 1000));
        now = esp_timer_get_time();
    }

    if (xSemaphoreTake(s_filled, pdMS_TO_TICKS(120)) != pdTRUE) {
        s_next_us = 0;
        gfx_clear(g, 0xFF000000);
        return;
    }
    int slot = s_tail;
    s_tail = (slot + 1) % RING;
    uint32_t len = s_slot_len[slot];
    bool ok = decode_to(s_ring + (size_t)slot * SLOT_MAX, len, g->px);
    xSemaphoreGive(s_free_slots);

    s_next_us += 1000000 / s_fps;
    if (now - s_next_us > 3 * 1000000 / s_fps) s_next_us = now; /* 落后太多就重新对齐 */

    if (!ok) {
        gfx_clear(g, 0xFF000000);
        return;
    }
    draw_hud(g);
    if (g->alpha < 255) dim(g->px, g->alpha);
}

const mochi_app_t app_pet = {
    .id = "pet",
    .name = "敲玻璃",
    .enter = pet_enter,
    .leave = NULL,
    .draw = pet_draw,
    .event = NULL,
};
