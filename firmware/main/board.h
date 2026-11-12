#pragma once

/**
 * ESP32-S3-LCD-2.8C 板级引脚定义
 * 依据微雪官方原理图 / 示例工程（ST7701S + RGB565 并口 + TCA9554 IO 扩展）
 */

/* ---------------- 屏幕 ---------------- */
#define LCD_H_RES 480
#define LCD_V_RES 480
#define LCD_PIXELS (LCD_H_RES * LCD_V_RES)
#define LCD_FRAME_BYTES (LCD_PIXELS * 2)

/* ST7701S 初始化用的 3 线 SPI（只在上电时用，之后复位/CS 走 IO 扩展芯片） */
#define PIN_LCD_SPI_MOSI 1
#define PIN_LCD_SPI_SCLK 2

/* RGB565 并口 */
#define PIN_LCD_PCLK 41
#define PIN_LCD_DE 40
#define PIN_LCD_VSYNC 39
#define PIN_LCD_HSYNC 38
#define PIN_LCD_B0 5
#define PIN_LCD_B1 45
#define PIN_LCD_B2 48
#define PIN_LCD_B3 47
#define PIN_LCD_B4 21
#define PIN_LCD_G0 14
#define PIN_LCD_G1 13
#define PIN_LCD_G2 12
#define PIN_LCD_G3 11
#define PIN_LCD_G4 10
#define PIN_LCD_G5 9
#define PIN_LCD_R0 46
#define PIN_LCD_R1 3
#define PIN_LCD_R2 8
#define PIN_LCD_R3 18
#define PIN_LCD_R4 17

/* 背光（PWM） */
#define PIN_LCD_BL 6
/* 18MHz 时面板刷新 64.7Hz，PSRAM 带宽被 DMA 吃掉一半；
 * 降到 12MHz ≈ 43Hz，肉眼够用，把带宽让给渲染 */
#define LCD_PCLK_HZ (12 * 1000 * 1000)

/* ---------------- I2C 总线（板载，不可复用） ---------------- */
#define PIN_I2C_SDA 15
#define PIN_I2C_SCL 7
#define I2C_SPEED_HZ 400000

#define ADDR_TCA9554 0x20 /* IO 扩展 */
#define ADDR_QMI8658H 0x6A /* 六轴，SA0 = 高 */
#define ADDR_QMI8658L 0x6B /* 六轴，SA0 = 低 */
#define ADDR_PCF85063 0x51 /* RTC */

/* TCA9554 引脚号，与丝印 EXIO1~EXIO8 对应（1 起） */
#define EXIO_LCD_RST 1
#define EXIO_LCD_CS 3
#define EXIO_SD_D3 4 /* TF 卡 D3/CS：拉高让卡进 SD 模式 */
#define EXIO_BUZZER 8

/* BOOT 键 */
#define PIN_BOOT_KEY 0
