#pragma once

#include <stdbool.h>

/**
 * 固件内置的示例插件（直接编译进固件）。
 * 没有读卡器也能一键装到 TF 卡上，同时给用户当模板参考。
 */

typedef struct {
    const char *file;
    const char *content;
} builtin_file_t;

typedef struct {
    const char *id;
    const char *name;
    const char *version;
    const builtin_file_t *files;
} builtin_plugin_t;

int builtin_count(void);
const builtin_plugin_t *builtin_get(int i);
const builtin_plugin_t *builtin_find(const char *id);

/* 写到 /sdcard/plugins/<id>/，返回写成功的文件数 */
int builtin_install(const char *id);

/* 每次开机同步：缺的装上、版本旧的重装（用户自己装的插件不动） */
void builtin_sync(void);
