#include <stdio.h>
#include <string.h>

#include "app.h"
#include "builtin_plugins.h"
#include "esp_log.h"
#include "gfx.h"
#include "lua_rt.h"
#include "plugins.h"

/**
 * 应用管理
 *
 * 列表 = 启用的内置插件（网络、桌宠）+ 启用的卡上插件（每个一屏）。
 * 内置插件和 Lua 插件共用同一套 enter/draw 入口，也共用启用开关和设置页。
 */

#define MAX_APPS (8 + PLUGIN_MAX)

typedef struct {
    mochi_app_t app;
    char id[32];
    char name[48];
    char path[160];
} slot_t;

static slot_t s_slots[PLUGIN_MAX];
static const mochi_app_t *s_apps[MAX_APPS];
static int s_count;
static int s_nslots;
static int s_index;
static int s_dir = 1;
static float s_fade = 1.0f;
static int s_phase; /* 0=显示中 1=淡出 2=淡入 */
static volatile bool s_reload;

static const char *PLUGIN_FALLBACK =
    "function on_draw()\n"
    "  ui.clear(0x101418)\n"
    "  ui.text('PLUGIN MISSING', 240, 226, 29, 0xffc86b)\n"
    "  ui.text('reinstall it in web ui', 240, 268, 29, 0x6b7785)\n"
    "end\n";

static slot_t *current_slot(void)
{
    for (int i = 0; i < s_nslots; i++) {
        if (s_apps[s_index] == &s_slots[i].app) return &s_slots[i];
    }
    return NULL;
}

static void plugin_enter(void)
{
    slot_t *s = current_slot();
    if (!s) return;

    /* main.lua 不见了就当场从内置副本装回来（用户装的插件不在此列，install 会直接返回 0） */
    if (!plugins_has(s->id, "main.lua")) {
        ESP_LOGW("apps", "%s: main.lua missing, reinstalling", s->id);
        builtin_install(s->id);
    }
    lua_rt_start(s->path, s->id, PLUGIN_FALLBACK);
}

static void plugin_draw(gfx_t *g, float dt)
{
    if (!lua_rt_active()) {
        gfx_background(g, GFX_RGB(0x1a, 0x0b, 0x0d), GFX_RGB(0x12, 0x08, 0x0a),
                       GFX_RGB(0x08, 0x05, 0x06));
        gfx_text(g, &font_ui_29, "PLUGIN ERROR", 240, 220, GFX_ALIGN_CENTER,
                 GFX_RGB(0xff, 0x8a, 0x9a));
        gfx_text(g, &font_ui_29, lua_rt_error(), 240, 268, GFX_ALIGN_CENTER,
                 GFX_RGB(0x8a, 0x7a, 0x7e));
        return;
    }
    lua_rt_draw(g, dt);
}

int apps_index_of(const char *id)
{
    for (int i = 0; i < s_count; i++) {
        if (!strcmp(s_apps[i]->id, id)) return i;
    }
    return -1;
}

static void rebuild(void)
{
    /* 记住当前应用，重建后尽量停在同一个上 */
    char keep[32] = {0};
    if (s_index >= 0 && s_index < s_count) {
        snprintf(keep, sizeof(keep), "%.31s", s_apps[s_index]->id);
    }

    int n = 0;
    for (int i = 0; i < mochi_builtin_count; i++) {
        const mochi_builtin_t *b = &mochi_builtins[i];
        if (!plugins_enabled(b->id)) continue;
        s_apps[n++] = b->app;
    }

    plugin_info_t list[PLUGIN_MAX];
    int np = plugins_scan(list, PLUGIN_MAX);
    s_nslots = 0;
    for (int i = 0; i < np; i++) {
        if (!plugins_enabled(list[i].id)) continue;
        slot_t *s = &s_slots[s_nslots];
        snprintf(s->id, sizeof(s->id), "%.31s", list[i].id);
        snprintf(s->name, sizeof(s->name), "%.47s", list[i].name);
        snprintf(s->path, sizeof(s->path), PLUGIN_ROOT "/%.31s/main.lua", list[i].id);
        s->app = (mochi_app_t){
            .id = s->id,
            .name = s->name,
            .enter = plugin_enter,
            .leave = NULL,
            .draw = plugin_draw,
            .event = NULL,
        };
        s_apps[n++] = &s->app;
        s_nslots++;
    }

    if (n == 0) {
        /* 全停用了就保底显示网络屏，至少能连上来重新打开 */
        s_apps[n++] = &app_net;
    }
    s_count = n;

    char ids[200] = {0};
    for (int i = 0; i < s_count; i++) {
        strncat(ids, s_apps[i]->id, sizeof(ids) - strlen(ids) - 2);
        if (i + 1 < s_count) strcat(ids, ",");
    }
    ESP_LOGI("apps", "rotation(%d): %s", s_count, ids);

    if (keep[0]) {
        for (int i = 0; i < s_count; i++) {
            if (!strcmp(s_apps[i]->id, keep)) {
                s_index = i;
                return;
            }
        }
    }
    if (s_index >= s_count) s_index = 0;
}

void apps_init(const char *start_id)
{
    rebuild();

    int idx = 0;
    if (start_id && start_id[0]) {
        for (int i = 0; i < s_count; i++) {
            if (!strcmp(s_apps[i]->id, start_id)) {
                idx = i;
                break;
            }
        }
    }

    s_index = idx;
    s_fade = 1.0f;
    s_phase = 0;
    if (s_apps[s_index]->enter) s_apps[s_index]->enter();
}

const mochi_app_t *apps_current(void)
{
    return s_apps[s_index];
}

int apps_index(void)
{
    return s_index;
}

bool apps_busy(void)
{
    return s_phase != 0;
}

void apps_next(void)
{
    if (s_phase != 0) return;
    s_dir = 1;
    s_phase = 1;
}

void apps_prev(void)
{
    if (s_phase != 0) return;
    s_dir = -1;
    s_phase = 1;
}

void apps_event(app_event_t ev)
{
    if (s_apps[s_index]->event) s_apps[s_index]->event(ev);
}

void apps_reload(void)
{
    s_reload = true;
}

void apps_draw(gfx_t *g, float dt)
{
    if (s_reload) {
        s_reload = false;
        if (s_apps[s_index]->leave) s_apps[s_index]->leave();
        rebuild();
        if (s_apps[s_index]->enter) s_apps[s_index]->enter();
        s_fade = 1.0f;
        s_phase = 0;
    }

    if (s_phase == 1) {
        s_fade -= dt / 0.10f;
        if (s_fade <= 0.0f) {
            s_fade = 0.0f;
            if (s_apps[s_index]->leave) s_apps[s_index]->leave();
            s_index = (s_index + s_dir + s_count) % s_count;
            if (s_apps[s_index]->enter) s_apps[s_index]->enter();
            s_phase = 2;
        }
    } else if (s_phase == 2) {
        s_fade += dt / 0.13f;
        if (s_fade >= 1.0f) {
            s_fade = 1.0f;
            s_phase = 0;
        }
    }

    uint8_t saved = g->alpha;
    g->alpha = (uint8_t)(s_fade * 255.0f);
    if (s_fade < 0.999f) gfx_clear(g, 0xFF000000u);
    if (s_apps[s_index]->draw) s_apps[s_index]->draw(g, dt);
    g->alpha = saved;
}
