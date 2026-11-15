#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

/**
 * 专辑封面（电脑推送的 RGB565 裸数据，圆形裁剪后贴到屏上）
 *
 * 文件：/sdcard/plugins/music/cover.565，以 side × side 的 uint16 小端存储。
 * 电脑端把封面缩放到 side×side 转成 RGB565 发过来，设备这边不做解码。
 */

/* 按 mtime/size 判断是否变化，变了才重新读盘；返回 false 表示没有封面 */
bool cover_load(const char *path, int side);

/* 在 (cx, cy) 画一个直径 size 的圆形封面（最近邻缩放） */
bool cover_draw(gfx_t *g, int cx, int cy, int size);
