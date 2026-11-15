#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

/**
 * TF 卡字体包（/sdcard/fonts/fontpack.bin）
 *
 * 内置字体只有 221 个汉字（省 flash），其余汉字从卡上补：
 * gfx_text 查不到内置字形时来这里找，找不到就继续跳过。
 */

bool fp_init(void);
int fp_count(void);

/* 查字形：成功返回 true，*bits 指向缓存里的灰度点阵 */
bool fp_glyph(uint32_t cp, mochi_glyph_t *out, const uint8_t **bits);

/* 只取步进宽度（排版测量用，不读点阵） */
bool fp_adv(uint32_t cp, uint16_t *adv);
