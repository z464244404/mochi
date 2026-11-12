#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx.h"

/**
 * 极简 Lua 插件运行时
 *
 * - 一个 lua_State，堆在 PSRAM 里，有硬上限
 * - 只开 base/string/table/math/utf8/coroutine
 * - 插件入口：/sdcard/plugins/<id>/main.lua，可选 on_start() / on_draw(dt)
 * - 画布就是当前 LCD 帧缓冲，插件只能通过 ui.* 这几个函数画
 */

bool lua_rt_start(const char *path, const char *plugin_id, const char *fallback_src);
void lua_rt_stop(void);
bool lua_rt_active(void);
bool lua_rt_failed(void);
const char *lua_rt_error(void);
size_t lua_rt_heap_used(void); /* 当前 Lua 堆占用（字节） */

void lua_rt_draw(gfx_t *g, float dt);
