#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * 插件目录管理：/sdcard/plugins/<id>/
 * 一个插件 = 一个目录，至少要有 manifest.json 和 main.lua。
 */

#define PLUGIN_ROOT "/sdcard/plugins"
#define PLUGIN_MAX 16

typedef struct {
    char id[32];
    char name[48];
    char version[16];
} plugin_info_t;

int plugins_scan(plugin_info_t *out, int max);
bool plugins_id_ok(const char *id);
bool plugins_has(const char *id, const char *file);

/* 当前激活的插件 id（NVS 里存）；没设置或找不到时返回第一个可用插件 */
const char *plugins_active(void);
void plugins_set_active(const char *id);

bool plugins_delete(const char *id);

/* 启用状态（NVS 里存一份「停用名单」，默认全启用） */
bool plugins_enabled(const char *id);
void plugins_set_enabled(const char *id, bool on);

/* 拼 /sdcard/plugins/<id>/<file> */
void plugins_path(const char *id, const char *file, char *out, size_t n);
