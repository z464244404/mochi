#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * 板载 I2C 总线 + TCA9554PWR IO 扩展
 * 屏幕复位(EXIO1) / 屏幕片选(EXIO3) / 蜂鸣器(EXIO8) 都挂在这颗芯片上
 */

esp_err_t io_init(void);

/* 共享 I2C 总线句柄（imu / rtc 用） */
i2c_master_bus_handle_t io_bus(void);
void io_lock(void);
void io_unlock(void);

/* 1 = 高电平，0 = 低电平；pin 用 EXIO_x 宏（1..8） */
esp_err_t io_exio_set(int pin, int level);
/* 直接读回 TCA9554 的输出寄存器影子值 */
uint8_t io_exio_state(void);
