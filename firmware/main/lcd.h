#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

/**
 * 480x480 ST7701S + RGB565 并口
 * 只用 esp_lcd 的 RGB panel 驱动，不依赖 LVGL
 */

esp_err_t lcd_init(void);

esp_lcd_panel_handle_t lcd_panel(void);

/* 0~100 */
void lcd_backlight_set(uint8_t percent);
void lcd_backlight_fade_to(uint8_t percent, uint32_t ms);
uint8_t lcd_backlight_get(void);

/* 整屏推一帧 RGB565（480x480），数据在 PSRAM 里 */
void lcd_draw_frame(const uint16_t *frame565);

/**
 * 零拷贝渲染：驱动自己维护多块 PSRAM 帧缓冲，
 * 拿到一块「当前没在显示」的直接解码进去，再 lcd_end_frame 提交。
 * 返回 NULL 表示没有可用帧缓冲（调用方用自己的缓冲 + lcd_draw_frame）。
 */
uint16_t *lcd_begin_frame(void);
void lcd_end_frame(uint16_t *fb);
int lcd_fb_count(void);

/* 全屏填充一个颜色 */
void lcd_fill(uint16_t color565);

/* 等待下一帧开始（VSYNC），用于避免撕裂、也用来做帧率统计 */
bool lcd_wait_vsync(uint32_t timeout_ms);

uint32_t lcd_vsync_count(void);
