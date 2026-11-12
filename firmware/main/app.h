#pragma once

#include <stdbool.h>

#include "gfx.h"

/**
 * 应用 / 插件接口
 *
 * 内置应用（表情、时钟）和以后的 Lua 插件最终都落到这套回调上：
 *   enter → 每帧 draw → leave
 * 应用只负责「画什么」，像素由 gfx 负责。
 */

typedef enum {
    APP_EV_SHAKE = 1,
    APP_EV_TAP = 2,
    APP_EV_FLIP_DOWN = 3,
    APP_EV_FLIP_UP = 4,
    APP_EV_DIZZY = 5, /* 双击：转晕（摇一摇留给「切换应用」） */
} app_event_t;

typedef struct mochi_app {
    const char *id;
    const char *name;
    void (*enter)(void);
    void (*leave)(void);
    void (*draw)(gfx_t *g, float dt);
    void (*event)(app_event_t ev);
} mochi_app_t;

void apps_init(const char *start_id);
void apps_draw(gfx_t *g, float dt);
void apps_event(app_event_t ev);
void apps_next(void);
void apps_prev(void);
/* 让当前应用重新 enter（插件安装/切换后调用），实际重载在主循环里做 */
void apps_reload(void);

const mochi_app_t *apps_current(void);
int apps_index(void);
bool apps_busy(void); /* 正在做切换过渡 */

/* 按 id 找当前列表里的位置，找不到返回 -1 */
int apps_index_of(const char *id);

/* 固定应用（插件槽位由 apps.c 动态生成） */
extern const mochi_app_t app_net;
extern const mochi_app_t app_pet;

/* 内置插件：代码编进固件，但对外和卡上的插件一样有 manifest / 设置 / 启用开关 */
typedef struct {
    const char *id;
    const char *name;
    const char *manifest;
    const mochi_app_t *app;
} mochi_builtin_t;

extern const mochi_builtin_t mochi_builtins[];
extern const int mochi_builtin_count;

const mochi_builtin_t *mochi_builtin_find(const char *id);
