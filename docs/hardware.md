# 硬件

板子：**微雪 ESP32-S3-LCD-2.8C**（ESP32-S3-WROOM-1-N16R8）。
下面这些值都来自 `firmware/main/board.h`，改板子只需要改那一份。

## 主要规格

| 项 | 值 |
|---|---|
| SoC | ESP32-S3，双核 Xtensa LX7 @240MHz |
| 内存 | 512KB SRAM + **8MB PSRAM（八线）** |
| Flash | **16MB** |
| 屏幕 | ST7701S，**480×480** 圆形，RGB565 并口 |
| 六轴 | QMI8658（加速度+陀螺仪） |
| RTC | PCF85063（I2C，带纽扣电池座） |
| IO 扩展 | TCA9554（8 路，控制屏幕复位/CS、TF 卡 D3、蜂鸣器） |
| 存储 | TF 卡，**SDMMC 1-bit** |
| 其它 | BOOT 键、USB-C（原生 USB-Serial-JTAG） |

## 屏幕接线

初始化用 3 线 SPI，正常显示用 RGB565 并口：

| 信号 | 引脚 |
|---|---|
| SPI MOSI / SCLK（仅初始化） | GPIO1 / GPIO2 |
| PCLK | GPIO41 |
| DE | GPIO40 |
| VSYNC | GPIO39 |
| HSYNC | GPIO38 |
| B0..B4 | GPIO5, 45, 48, 47, 21 |
| G0..G5 | GPIO14, 13, 12, 11, 10, 9 |
| R0..R4 | GPIO46, 3, 8, 18, 17 |
| 背光 | GPIO6（PWM） |
| 复位 / CS | 走 IO 扩展 EXIO1 / EXIO3 |

**PCLK 故意降到 12MHz**：18MHz 时面板刷新 64.7Hz，DMA 会吃掉一半 PSRAM 带宽，
渲染就没时间了；12MHz ≈ 43Hz，肉眼完全够用，把带宽让给绘制。

## I2C

GPIO15 (SDA) / GPIO7 (SCL)，400kHz。

| 器件 | 地址 |
|---|---|
| TCA9554 | 0x20 |
| QMI8658 | 0x6A（SA0 高）/ 0x6B（SA0 低，这块板是 0x6B） |
| PCF85063 | 0x51 |

## IO 扩展（TCA9554）

| 丝印 | 用途 |
|---|---|
| EXIO1 | 屏幕复位 |
| EXIO3 | 屏幕 CS |
| **EXIO4** | **TF 卡 D3/CS —— 必须拉高，卡才会进 SD 模式** |
| EXIO8 | 蜂鸣器 |

## TF 卡

**SDMMC 1-bit**（不是 SPI）：CLK=GPIO2、CMD=GPIO1、D0=GPIO42、D3=EXIO4。

两个坑：

1. GPIO1/2 同时是屏幕初始化 SPI 的引脚，所以 `lcd_init()` 结束后必须 `spi_bus_free(SPI2_HOST)` 才能挂卡；
2. EXIO4 不拉高的话 `sdmmc` 初始化会失败或者只能跑 1-bit 低速。

## 内存怎么花的

| 用途 | 大小 |
|---|---|
| 3 块帧缓冲（三缓冲防撕裂） | 3 × 450KB = 1.35MB |
| 桌宠视频环形缓冲 | 576KB（12 槽 × 48KB） |
| 天气图标缓存（4 槽 LRU） | 4 × 345KB ≈ 1.38MB |
| 天气背景（480×480 RGB565） | 450KB |
| Lua 状态机堆（上限） | 192KB |
| 其余（WiFi、HTTP、JPEG 工作区、堆开销） | 剩下的 |

实测空闲约 **3.3MB PSRAM + 88KB 内部 RAM**（内部 RAM 还要养 WiFi/lwIP/TLS）。

## 电源

USB-C 供电即可。屏幕背光全开 + WiFi 发射时峰值约 0.5A，普通 5V/1A 头够用。
另外板子侧面有电池接口（需要自己焊），但本项目目前没有做电池管理。
