#pragma once

#include <stdbool.h>

#include "gfx.h"

/**
 * 天气背景（480x480 JPEG，存在 TF 卡 /sdcard/bg.bin）
 *
 * 一张图 450KB，解码一次缓存到 PSRAM，之后每帧直接拷进帧缓冲。
 * 只有天气变化时才重新解码。
 */

bool bg_init(void);

/* 名字变了才重新解码；失败返回 false（调用方自己兜底色） */
bool bg_draw(gfx_t *g, const char *name);
