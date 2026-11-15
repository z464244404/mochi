[中文](hardware.md) · **English**

# Hardware

Board: **Waveshare ESP32-S3-LCD-2.8C** (ESP32-S3-WROOM-1-N16R8).
All the values below come from `firmware/main/board.h`; to switch boards you only edit that one file.

## Main Specifications

| Item | Value |
|---|---|
| SoC | ESP32-S3, dual-core Xtensa LX7 @240MHz |
| Memory | 512KB SRAM + **8MB PSRAM (octal)** |
| Flash | **16MB** |
| Screen | ST7701S, **480×480** round, RGB565 parallel |
| Six-axis | QMI8658 (accelerometer + gyroscope) |
| RTC | PCF85063 (I2C, with coin-cell holder) |
| IO expander | TCA9554 (8 channels: screen reset/CS, TF card D3, buzzer) |
| Storage | TF card, **SDMMC 1-bit** |
| Other | BOOT button, USB-C (native USB-Serial-JTAG) |

## Screen Wiring

Initialization uses 3-wire SPI; normal display uses the RGB565 parallel bus:

| Signal | Pin |
|---|---|
| SPI MOSI / SCLK (init only) | GPIO1 / GPIO2 |
| PCLK | GPIO41 |
| DE | GPIO40 |
| VSYNC | GPIO39 |
| HSYNC | GPIO38 |
| B0..B4 | GPIO5, 45, 48, 47, 21 |
| G0..G5 | GPIO14, 13, 12, 11, 10, 9 |
| R0..R4 | GPIO46, 3, 8, 18, 17 |
| Backlight | GPIO6 (PWM) |
| Reset / CS | via IO expander EXIO1 / EXIO3 |

**PCLK is deliberately dropped to 12MHz**: at 18MHz the panel refreshes at 64.7Hz and DMA eats
half the PSRAM bandwidth, leaving no time to render; 12MHz ≈ 43Hz is more than enough to the eye
and gives that bandwidth back to drawing.

## I2C

GPIO15 (SDA) / GPIO7 (SCL), 400kHz.

| Device | Address |
|---|---|
| TCA9554 | 0x20 |
| QMI8658 | 0x6A (SA0 high) / 0x6B (SA0 low; this board is 0x6B) |
| PCF85063 | 0x51 |

## IO Expander (TCA9554)

| Silkscreen | Purpose |
|---|---|
| EXIO1 | Screen reset |
| EXIO3 | Screen CS |
| **EXIO4** | **TF card D3/CS — must be pulled high, otherwise the card never enters SD mode** |
| EXIO8 | Buzzer |

## TF Card

**SDMMC 1-bit** (not SPI): CLK=GPIO2, CMD=GPIO1, D0=GPIO42, D3=EXIO4.

Two gotchas:

1. GPIO1/2 are also the pins used for the screen's initialization SPI, so after `lcd_init()` you
   must call `spi_bus_free(SPI2_HOST)` before the card can be mounted;
2. If EXIO4 is not pulled high, `sdmmc` initialization fails or only runs at 1-bit low speed.

## Where the Memory Goes

| Use | Size |
|---|---|
| 3 frame buffers (triple buffering, anti-tearing) | 3 × 450KB = 1.35MB |
| Pet video ring buffer | 576KB (12 slots × 48KB) |
| Weather icon cache (4-slot LRU) | 4 × 345KB ≈ 1.38MB |
| Weather background (480×480 RGB565) | 450KB |
| Lua state machine heap (cap) | 192KB |
| Everything else (WiFi, HTTP, JPEG workspace, heap overhead) | the remainder |

Measured free memory: about **3.3MB PSRAM + 88KB internal RAM** (internal RAM also has to feed
WiFi/lwIP/TLS).

## Power

USB-C power is sufficient. With the screen backlight at full and WiFi transmitting, peak draw is
about 0.5A, so an ordinary 5V/1A adapter is enough.
The board also has a battery connector on the side (you have to solder it yourself), but this
project does not currently implement any battery management.
