#include "lcd.h"

#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "io.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"

static const char *TAG = "lcd";

/* ------------------------------------------------------------------ */
/* ST7701S 初始化序列（来自微雪官方示例，2.8 寸屏）                     */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
    uint16_t delay_ms;
} st7701_cmd_t;

static const st7701_cmd_t st7701_init_seq[] = {
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}, 0},
    {0xEF, 1, {0x08}, 0},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10}, 0},
    {0xC0, 2, {0x3B, 0x00}, 0},
    {0xC1, 2, {0x10, 0x0C}, 0},
    {0xC2, 2, {0x07, 0x0A}, 0},
    {0xC7, 1, {0x00}, 0},
    {0xCC, 1, {0x10}, 0},
    {0xCD, 1, {0x08}, 0},
    {0xB0, 16, {0x05, 0x12, 0x98, 0x0E, 0x0F, 0x07, 0x07, 0x09, 0x09, 0x23, 0x05, 0x52, 0x0F, 0x67, 0x2C, 0x11}, 0},
    {0xB1, 16, {0x0B, 0x11, 0x97, 0x0C, 0x12, 0x06, 0x06, 0x08, 0x08, 0x22, 0x03, 0x51, 0x11, 0x66, 0x2B, 0x0F}, 0},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11}, 0},
    {0xB0, 1, {0x5D}, 0},
    {0xB1, 1, {0x3E}, 0},
    {0xB2, 1, {0x81}, 0},
    {0xB3, 1, {0x80}, 0},
    {0xB5, 1, {0x4E}, 0},
    {0xB7, 1, {0x85}, 0},
    {0xB8, 1, {0x20}, 0},
    {0xC1, 1, {0x78}, 0},
    {0xC2, 1, {0x78}, 0},
    {0xD0, 1, {0x88}, 0},
    {0xE0, 3, {0x00, 0x00, 0x02}, 0},
    {0xE1, 11, {0x06, 0x30, 0x08, 0x30, 0x05, 0x30, 0x07, 0x30, 0x00, 0x33, 0x33}, 0},
    {0xE2, 12, {0x11, 0x11, 0x33, 0x33, 0xF4, 0x00, 0x00, 0x00, 0xF4, 0x00, 0x00, 0x00}, 0},
    {0xE3, 4, {0x00, 0x00, 0x11, 0x11}, 0},
    {0xE4, 2, {0x44, 0x44}, 0},
    {0xE5, 16, {0x0D, 0xF5, 0x30, 0xF0, 0x0F, 0xF7, 0x30, 0xF0, 0x09, 0xF1, 0x30, 0xF0, 0x0B, 0xF3, 0x30, 0xF0}, 0},
    {0xE6, 4, {0x00, 0x00, 0x11, 0x11}, 0},
    {0xE7, 2, {0x44, 0x44}, 0},
    {0xE8, 16, {0x0C, 0xF4, 0x30, 0xF0, 0x0E, 0xF6, 0x30, 0xF0, 0x08, 0xF0, 0x30, 0xF0, 0x0A, 0xF2, 0x30, 0xF0}, 0},
    {0xE9, 2, {0x36, 0x01}, 0},
    {0xEB, 7, {0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40}, 0},
    {0xED, 16, {0xFF, 0x10, 0xAF, 0x76, 0x54, 0x2B, 0xCF, 0xFF, 0xFF, 0xFC, 0xB2, 0x45, 0x67, 0xFA, 0x01, 0xFF}, 0},
    {0xEF, 6, {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 0},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00}, 0},
    {0x11, 0, {0}, 120}, /* sleep out */
    {0x3A, 1, {0x66}, 0}, /* RGB565 */
    {0x36, 1, {0x00}, 0}, /* MADCTL */
    {0x35, 1, {0x00}, 0}, /* tearing effect line on */
    {0x29, 0, {0}, 20},   /* display on */
};

static spi_device_handle_t s_spi;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_vsync_sem;
static volatile uint32_t s_vsync_cnt;

/* 驱动分配的多块帧缓冲（零拷贝渲染用） */
static uint16_t *s_fbs[3];
static int s_nfb;
static int s_write_idx = 1;

/* 背光 */
static esp_timer_handle_t s_bl_timer;
static int s_bl_cur;    /* 当前实际亮度 0~100 */
static int s_bl_from;
static int s_bl_to;
static int64_t s_bl_t0_us;
static uint32_t s_bl_dur_ms;

#define BL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_RES LEDC_TIMER_10_BIT
#define BL_LEDC_MAX ((1 << 10) - 1)

/* ------------------------------------------------------------------ */

static void st7701_write(uint8_t dc, uint8_t val)
{
    spi_transaction_t t = {
        .length = 0,
        .rxlength = 0,
        .cmd = dc,
        .addr = val,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void st7701_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    st7701_write(0, cmd);
    for (size_t i = 0; i < len; i++) {
        st7701_write(1, data[i]);
    }
}

static void backlight_apply(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    uint32_t duty = (uint32_t)(BL_LEDC_MAX * pct / 100);
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

static void bl_timer_cb(void *arg)
{
    int64_t el_ms = (esp_timer_get_time() - s_bl_t0_us) / 1000;
    int pct;
    if (el_ms >= (int64_t)s_bl_dur_ms || s_bl_dur_ms == 0) {
        pct = s_bl_to;
        esp_timer_stop(s_bl_timer);
    } else {
        pct = s_bl_from + (int)((int64_t)(s_bl_to - s_bl_from) * el_ms / (int64_t)s_bl_dur_ms);
    }
    if (pct != s_bl_cur) {
        s_bl_cur = pct;
        backlight_apply(pct);
    }
}

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    ledc_channel_config_t ch = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "ledc ch");

    const esp_timer_create_args_t targs = {
        .callback = bl_timer_cb,
        .name = "bl_fade",
    };
    return esp_timer_create(&targs, &s_bl_timer);
}

void lcd_backlight_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    if (s_bl_timer) esp_timer_stop(s_bl_timer);
    s_bl_cur = percent;
    backlight_apply(percent);
}

uint8_t lcd_backlight_get(void)
{
    return (uint8_t)s_bl_cur;
}

void lcd_backlight_fade_to(uint8_t percent, uint32_t ms)
{
    if (percent > 100) percent = 100;
    if (ms == 0 || s_bl_timer == NULL) {
        lcd_backlight_set(percent);
        return;
    }
    esp_timer_stop(s_bl_timer);
    s_bl_from = s_bl_cur;
    s_bl_to = percent;
    s_bl_dur_ms = ms;
    s_bl_t0_us = esp_timer_get_time();
    esp_timer_start_periodic(s_bl_timer, 15 * 1000);
}

/* ------------------------------------------------------------------ */

static bool on_vsync_cb(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    BaseType_t hp = pdFALSE;
    s_vsync_cnt++;
    if (s_vsync_sem) {
        xSemaphoreGiveFromISR(s_vsync_sem, &hp);
    }
    return hp == pdTRUE;
}

bool lcd_wait_vsync(uint32_t timeout_ms)
{
    if (s_vsync_sem == NULL) return false;
    return xSemaphoreTake(s_vsync_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

uint32_t lcd_vsync_count(void)
{
    return s_vsync_cnt;
}

esp_err_t lcd_init(void)
{
    ESP_LOGI(TAG, "panel reset");
    io_exio_set(EXIO_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    io_exio_set(EXIO_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    io_exio_set(EXIO_LCD_CS, 0); /* CS 低有效 */
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_LCD_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_LCD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    spi_device_interface_config_t devcfg = {
        .command_bits = 1, /* 1 = data, 0 = command */
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = 4 * 1000 * 1000,
        .spics_io_num = -1, /* CS 由 TCA9554 控制 */
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi), TAG, "spi dev");

    ESP_LOGI(TAG, "load ST7701S init sequence");
    for (size_t i = 0; i < sizeof(st7701_init_seq) / sizeof(st7701_init_seq[0]); i++) {
        const st7701_cmd_t *c = &st7701_init_seq[i];
        st7701_cmd(c->cmd, c->data, c->len);
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }

    ESP_LOGI(TAG, "install RGB panel driver");
    esp_lcd_rgb_panel_config_t cfg = {
        .data_width = 16,
        .psram_trans_align = 64,
        .num_fbs = 3,
        /* 关键：帧缓冲在 PSRAM，CPU 解码时会把 PSRAM 带宽吃满，
         * 直接用 EDMA 从 PSRAM 取数据会断流 → 整屏撕裂。
         * 用 DRAM 里的 bounce buffer 做中转，EDMA 只读内部 RAM。 */
        .bounce_buffer_size_px = 10 * LCD_H_RES,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = -1,
        .pclk_gpio_num = PIN_LCD_PCLK,
        .vsync_gpio_num = PIN_LCD_VSYNC,
        .hsync_gpio_num = PIN_LCD_HSYNC,
        .de_gpio_num = PIN_LCD_DE,
        .data_gpio_nums = {
            PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
            PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
            PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
        },
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = 10,
            .hsync_front_porch = 50,
            .hsync_pulse_width = 8,
            .vsync_back_porch = 18,
            .vsync_front_porch = 8,
            .vsync_pulse_width = 2,
            .flags.pclk_active_neg = false,
        },
        .flags.fb_in_psram = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "rgb panel");

    /* 取回驱动的多块帧缓冲，用于零拷贝渲染 */
    s_nfb = 0;
    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 3, (void **)&s_fbs[0], (void **)&s_fbs[1], (void **)&s_fbs[2]) == ESP_OK) {
        s_nfb = 3;
    } else if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, (void **)&s_fbs[0], (void **)&s_fbs[1]) == ESP_OK) {
        s_nfb = 2;
    }
    ESP_LOGI(TAG, "frame buffers in psram: %d x %u KB", s_nfb, (unsigned)(LCD_FRAME_BYTES / 1024));

    if (s_vsync_sem == NULL) {
        s_vsync_sem = xSemaphoreCreateBinary();
    }    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = on_vsync_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, NULL), TAG, "cbs");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");

    io_exio_set(EXIO_LCD_CS, 1); /* 释放 SPI */

    /* 屏幕初始化用的 3 线 SPI 到此结束：GPIO1/2 要让给 TF 卡 */
    spi_bus_remove_device(s_spi);
    spi_bus_free(SPI2_HOST);
    s_spi = NULL;

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    s_bl_cur = 0;
    backlight_apply(0);

    ESP_LOGI(TAG, "LCD ready (%dx%d)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

esp_lcd_panel_handle_t lcd_panel(void)
{
    return s_panel;
}

uint16_t *lcd_begin_frame(void)
{
    if (s_nfb < 2) return NULL;
    uint16_t *p = s_fbs[s_write_idx];
    s_write_idx = (s_write_idx + 1) % s_nfb;
    return p;
}

void lcd_end_frame(uint16_t *fb)
{
    if (!s_panel || !fb) return;
    /* 当缓冲就是驱动自己的帧缓冲时，esp_lcd 只切换显示指针，不做拷贝 */
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, fb);
}

int lcd_fb_count(void)
{
    return s_nfb;
}

void lcd_draw_frame(const uint16_t *frame565)
{
    if (!s_panel || !frame565) return;
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, frame565);
}

void lcd_fill(uint16_t color565)
{
    if (!s_panel) return;

    /* 有独立帧缓冲就整块填满再切换，绝对不要直接写正在显示的那块 */
    uint16_t *fb = lcd_begin_frame();
    if (fb) {
        for (int i = 0; i < LCD_PIXELS; i++) {
            fb[i] = color565;
        }
        lcd_end_frame(fb);
        return;
    }

    static uint16_t line[LCD_H_RES];
    for (int i = 0; i < LCD_H_RES; i++) line[i] = color565;
    for (int y = 0; y < LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + 1, line);
    }
}
