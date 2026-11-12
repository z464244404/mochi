#pragma once

#include <stdbool.h>

#include "gfx.h"

/**
 * 天气图标（编译进固件的 icons.bin，meteocons 渲染后压成 JPEG）
 *
 * 图标已经预合成到天气屏底色上，所以直接整块贴，不需要 alpha。
 * 每种图标有 128 和 64 两档尺寸，贴图时按名字取对应尺寸。
 */

bool icons_init(void);
bool icons_draw(gfx_t *g, const char *name, int size, float cx, float cy);
