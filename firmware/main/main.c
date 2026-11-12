#include <math.h>
#include <stdio.h>

#include "app.h"
#include "assets.h"
#include "board.h"
#include "builtin_plugins.h"
#include "driver/gpio.h"
#include "dsec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include <unistd.h>

#include "gfx.h"
#include "bg.h"
#include "icons.h"
#include "imu.h"
#include "io.h"
#include "lcd.h"
#include "lua_rt.h"
#include "net.h"
#include "rtc.h"
#include "sd.h"
#include "web.h"

static const char *TAG = "mochi";

static volatile int s_pending_ev;
static volatile int s_pending_count;
static int64_t s_last_tap_us;

static bool s_screen_on = true;
static uint16_t *s_last_fb;
static uint32_t s_frames;
static volatile float s_fps;

float mochi_fps(void)
{
    return s_fps;
}

/* ------------------------------------------------------------------ *
 * 调试用：把当前帧打成 ASCII 灰度图，没有摄像头也能确认「画出来是什么样」�? * ------------------------------------------------------------------ */
#ifndef MOCHI_DEBUG_DUMP
#define MOCHI_DEBUG_DUMP 0
#endif

static void dump_frame_ascii(void)
{
    static const char ramp[] = " .:-=+*#%@";
    if (!s_last_fb) return;

    printf("\n--- frame %dx%d ---\n", MOCHI_W, MOCHI_H);
    for (int ry = 0; ry < 30; ry++) {
        char line[61];
        for (int rx = 0; rx < 60; rx++) {
            uint32_t sum = 0;
            for (int y = 0; y < 16; y++) {
                const uint16_t *row = &s_last_fb[(ry * 16 + y) * MOCHI_W + rx * 8];
                for (int x = 0; x < 8; x++) {
                    uint16_t p = row[x];
                    sum += ((p >> 11) & 0x1F) * 8 + ((p >> 5) & 0x3F) * 4 + (p & 0x1F) * 8;
                }
            }
            int idx = (int)(sum / 128) * 10 / 256;
            if (idx > 9) idx = 9;
            line[rx] = ramp[idx];
        }
        line[60] = 0;
        printf("%s\n", line);
    }
    fflush(stdout);
}

#if MOCHI_DEBUG_DUMP
static void dump_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(4000));
    for (;;) {
        dump_frame_ascii();
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}
#endif

#if MOCHI_DEBUG_DUMP
static void bench_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2500));
    const int N = MOCHI_W * MOCHI_H;
    uint16_t *fb = lcd_begin_frame();
    uint16_t *ps = heap_caps_malloc(N * 2, MALLOC_CAP_SPIRAM);

    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < N / 2; i++) ((uint32_t *)fb)[i] = 0x12341234;
    int64_t t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "bench write fb: %.2f ms (%.1f MB/s)", (t1 - t0) / 1000.0,
             (N * 2.0) / (float)(t1 - t0));

    t0 = esp_timer_get_time();
    for (int i = 0; i < N / 2; i++) ((uint32_t *)ps)[i] = 0x56785678;
    t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "bench write psram: %.2f ms (%.1f MB/s)", (t1 - t0) / 1000.0,
             (N * 2.0) / (float)(t1 - t0));

    free(ps);
    lcd_end_frame(fb);
    vTaskDelete(NULL);
}
#endif

static void console_task(void *arg)
{
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (c == 'n') {
            apps_next();
        } else if (c == 's') {
            dump_frame_ascii();
        }
    }
}

static void on_imu(int ev)
{
    s_pending_ev = ev;
    s_pending_count++;
}

/* 单击 = 开心，双击 = 转晕；两个都在主任务里分发，避免和绘制抢状�?*/
static void dispatch_imu(int ev)
{
    if (ev == IMU_EV_TAP) {
        int64_t now = esp_timer_get_time();
        if (now - s_last_tap_us < 420000) {
            apps_event(APP_EV_DIZZY);
            s_last_tap_us = 0;
            return;
        }
        s_last_tap_us = now;
        apps_event(APP_EV_TAP);
        return;
    }

    if (ev == IMU_EV_SHAKE) {
        if (!s_screen_on) {
            s_screen_on = true;
            lcd_backlight_fade_to(85, 250);
            return;
        }
        apps_next();
        return;
    }

    if (ev == IMU_EV_FLIP_DOWN) {
        ESP_LOGW(TAG, "flip down -> screen off");
        if (s_screen_on) {
            s_screen_on = false;
            lcd_backlight_set(0);
        }
        return;
    }

    if (ev == IMU_EV_FLIP_UP) {
        ESP_LOGW(TAG, "flip up -> screen on");
        if (!s_screen_on) {
            s_screen_on = true;
            lcd_backlight_fade_to(85, 350);
        }
        return;
    }
}

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_KEY,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

/* 33ms 轮询一次，5 次稳定才算按�?*/
static void poll_button(void)
{
    static int last = 1, stable = 1, cnt = 0;
    int v = gpio_get_level(PIN_BOOT_KEY);
    if (v != last) {
        last = v;
        cnt = 0;
    } else if (cnt < 5) {
        cnt++;
    }
    if (cnt == 5 && v != stable) {
        stable = v;
        if (v == 0) apps_next();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========== Mochi 0.1 ==========");

    io_init();
    rtc_clock_init();

    if (lcd_init() != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed");
    }

    if (sd_mount() == ESP_OK) {
        FILE *f = fopen("/sdcard/pet.mjp", "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fclose(f);
            ESP_LOGI(TAG, "SD: pet.mjp %ld KB", sz / 1024);
        } else {
            ESP_LOGW(TAG, "SD: pet.mjp not found");
        }

        /* 挂载后第一次写偶尔会失败，先写个探针把坑占�?*/
        mkdir("/sdcard/plugins", 0777);
        FILE *probe = sd_open_write("/sdcard/plugins/.probe", 10);
        ESP_LOGI(TAG, "SD write probe: %s", probe ? "ok" : "failed");
        if (probe) {
            fputc('x', probe);
            fclose(probe);
            remove("/sdcard/plugins/.probe");
        }

        builtin_sync();
        assets_sync();
        icons_init();
        bg_init();
    }

    net_init();
    dsec_init();
    web_start();

    button_init();

    /* 有保存的 WiFi 就给�?1.5 秒连上；连上直接进桌宠，没连上先显示网络状态屏 */
    bool sta = net_sta_up();
    if (net_have_creds() && !sta) {
        for (int i = 0; i < 15 && !sta; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            sta = net_sta_up();
        }
    }
    ESP_LOGI(TAG, "boot screen: %s (sta=%d creds=%d)", sta ? "pet" : "net", sta,
             net_have_creds());
    apps_init(sta ? "pet" : "net");

    if (imu_init() == ESP_OK) {
        imu_start(on_imu);
    }

    lcd_backlight_fade_to(85, 700);

    xTaskCreate(console_task, "console", 4096, NULL, 2, NULL);
#if MOCHI_DEBUG_DUMP
    xTaskCreate(dump_task, "dump", 4096, NULL, 2, NULL);
    xTaskCreate(bench_task, "bench", 4096, NULL, 2, NULL);
#endif
    int64_t last = esp_timer_get_time();
    int64_t last_log = last;
    uint32_t last_vs = lcd_vsync_count();
    uint32_t last_frames = 0;
    int64_t draw_us = 0;
    uint32_t draw_n = 0;
    int64_t fps_t0 = last;
    uint32_t fps_n = 0;
    for (;;) {
        lcd_wait_vsync(200);
        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        if (now - last_log > 30000000) {
            uint32_t vs = lcd_vsync_count();
            ESP_LOGI(TAG,
                     "vsync=%.1f render=%.1f draw=%.1fms app=%s heap=%u psram=%u "
                     "iram=%u dma=%u lua=%u stack0=%u",
                     (float)(vs - last_vs) * 1e6f / (float)(now - last_log),
                     (float)(s_frames - last_frames) * 1e6f / (float)(now - last_log),
                     draw_n ? (float)draw_us / (float)draw_n / 1000.0f : 0.0f,
                     apps_current()->id, (unsigned)esp_get_free_heap_size(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                     (unsigned)lua_rt_heap_used(),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            last_log = now;
            last_vs = vs;
            last_frames = s_frames;
            draw_us = 0;
            draw_n = 0;
        }

        poll_button();

        if (s_pending_count) {
            int ev = s_pending_ev;
            s_pending_count = 0;
            dispatch_imu(ev);
        }

        if (!s_screen_on) continue;

        uint16_t *fb = lcd_begin_frame();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        gfx_t g;
        gfx_init(&g, fb, MOCHI_W, MOCHI_H);
        int64_t td0 = esp_timer_get_time();
        apps_draw(&g, dt);
        draw_us += esp_timer_get_time() - td0;
        draw_n++;
        lcd_end_frame(fb);
        s_last_fb = fb;
        s_frames++;

        fps_n++;
        if (now - fps_t0 >= 1000000) {
            s_fps = (float)fps_n * 1e6f / (float)(now - fps_t0);
            fps_n = 0;
            fps_t0 = now;
        }

        /* 解码一帧就超过一�?vsync 周期，信号量永远就绪，不主动让一�?         * IDLE0 会被饿死�? 秒后触发 task watchdog */
        vTaskDelay(1);
    }
}
