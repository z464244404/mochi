#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

/**
 * TF 卡字体包（可多套，按字号选）
 *
 *   /sdcard/fonts/fontpack.bin    29px（GB2312 全集）
 *   /sdcard/fonts/fontpack22.bin  22px
 *
 * 内置字体只有 233 个字形（省 flash），其余汉字从卡上补：
 * gfx_text 查不到内置字形时，按字号挑最接近的一套来查。
 */

bool fp_init(void);
int fp_count(void);      /* 所有包合计字形数 */
int fp_count_px(int px); /* 某一套的字形数，没装返回 0 */

/* 查字形：size 是请求的字号，返回最接近那套里的字形 */
bool fp_glyph(uint32_t cp, int size, mochi_glyph_t *out, const uint8_t **bits);

/* 只取步进宽度（排版测量用，不读点阵） */
bool fp_adv(uint32_t cp, int size, uint16_t *adv);
