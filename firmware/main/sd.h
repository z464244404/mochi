#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "esp_err.h"

/* 挂载 TF 卡到 /sdcard（SDMMC 1-bit） */
esp_err_t sd_mount(void);
bool sd_ready(void);
void sd_bench(void);

/**
 * 打开文件写。FATFS 挂载后的第一次写经常直接失败（EINVAL），
 * 之后就好了，所以这里带重试。
 */
FILE *sd_open_write(const char *path, int tries);
