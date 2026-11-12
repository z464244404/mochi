#include "lua_rt.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "icons.h"
#include "bg.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "plugin_conf.h"
#include "plugins.h"
#include "rtc.h"
#include "net.h"
#include "sd.h"

/**
 * 插件运行时。设计取舍：
 *  - 堆放在 PSRAM，硬上限 192KB，超了 Lua 自己抛 "not enough memory"，不会拖垮系统
 *  - 只开 base/string/table/math/utf8/coroutine；io/os/package/debug 一律不注册
 *  - on_draw 里挂指令计数钩子，跑飞了会被 pcall 捕获，不会卡死主循环
 *  - 画布只暴露 5 个图元函数，插件碰不到像素缓冲
 *  - http.get 在独立任务里跑，结果回到绘制线程再回调 Lua（Lua 不是线程安全的）
 */

#define LUA_HEAP_CAP (192 * 1024)
#define DRAW_BUDGET 120000 /* 单帧指令上限 */
#define HTTP_BODY_MAX 8192
#define URL_MAX 255

static const char *TAG = "lua";

typedef struct {
    char url[URL_MAX + 1];
    int ref;
    uint32_t gen;
} http_req_t;

typedef struct {
    int ref;
    int status;
    char *body;
    uint32_t gen;
} http_res_t;

static QueueHandle_t s_req_q;
static QueueHandle_t s_res_q;
static cJSON *s_conf;
static volatile uint32_t s_gen;

/* 最近一次成功响应（按 URL）。切屏会重建 Lua 状态，缓存放在 C 里才不会丢，
 * 这样来回翻页时不用每次都重新请求。 */
#define CACHE_TTL_US (120LL * 1000000)
static char *s_cache_body;    /* 已发布，只读 */
static char *s_cache_stage;   /* worker 往这里写，写完交换 */
static char s_cache_url[URL_MAX + 1];
static volatile int s_cache_status;
static volatile int64_t s_cache_us;

typedef struct {
    size_t used;
    size_t cap;
} lua_mem_t;

static lua_mem_t s_mem;
static lua_State *L;
static gfx_t *s_cur;
static bool s_active;
static bool s_failed;
static char s_err[160];

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    lua_mem_t *m = (lua_mem_t *)ud;

    if (nsize == 0) {
        if (ptr) {
            m->used -= osize;
            heap_caps_free(ptr);
        }
        return NULL;
    }

    size_t delta = nsize - (ptr ? osize : 0);
    if (m->used + delta > m->cap) return NULL;

    void *np = heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (np) m->used += delta;
    return np;
}

static void set_error(const char *msg)
{
    snprintf(s_err, sizeof(s_err), "%s", msg ? msg : "?");
    s_failed = true;
}

static void hook(lua_State *L_, lua_Debug *ar)
{
    (void)ar;
    luaL_error(L_, "frame budget exceeded");
}

/* ---------------- ui.* ---------------- */

static uint32_t color_arg(lua_State *L_, int idx)
{
    lua_Integer v = luaL_checkinteger(L_, idx);
    return (uint32_t)v | 0xFF000000u;
}

static int l_clear(lua_State *L_)
{
    if (s_cur) gfx_clear(s_cur, color_arg(L_, 1));
    return 0;
}

static int l_rect(lua_State *L_)
{
    float x = (float)luaL_checknumber(L_, 1);
    float y = (float)luaL_checknumber(L_, 2);
    float w = (float)luaL_checknumber(L_, 3);
    float h = (float)luaL_checknumber(L_, 4);
    uint32_t c = color_arg(L_, 5);
    if (s_cur) gfx_round_rect(s_cur, x + w / 2, y + h / 2, w, h, 0.5f, gfx_solid(c));
    return 0;
}

/* round_rect(x, y, w, h, r, color) —— x/y 是左上角 */
static int l_round_rect(lua_State *L_)
{
    float x = (float)luaL_checknumber(L_, 1);
    float y = (float)luaL_checknumber(L_, 2);
    float w = (float)luaL_checknumber(L_, 3);
    float h = (float)luaL_checknumber(L_, 4);
    float r = (float)luaL_checknumber(L_, 5);
    uint32_t c = color_arg(L_, 6);
    if (s_cur) gfx_round_rect(s_cur, x + w / 2, y + h / 2, w, h, r, gfx_solid(c));
    return 0;
}

/* arc(cx, cy, r, thick, a0, a1, color)，角度制，0°=右，顺时针 */
static int l_arc(lua_State *L_)
{
    float cx = (float)luaL_checknumber(L_, 1);
    float cy = (float)luaL_checknumber(L_, 2);
    float r = (float)luaL_checknumber(L_, 3);
    float th = (float)luaL_checknumber(L_, 4);
    float a0 = (float)luaL_checknumber(L_, 5);
    float a1 = (float)luaL_checknumber(L_, 6);
    uint32_t c = color_arg(L_, 7);
    if (s_cur) gfx_arc(s_cur, cx, cy, r, th, a0, a1, c);
    return 0;
}

static int l_circle(lua_State *L_)
{
    float cx = (float)luaL_checknumber(L_, 1);
    float cy = (float)luaL_checknumber(L_, 2);
    float r = (float)luaL_checknumber(L_, 3);
    uint32_t c = color_arg(L_, 4);
    if (s_cur) gfx_circle(s_cur, cx, cy, r, gfx_solid(c));
    return 0;
}

static int l_ring(lua_State *L_)
{
    float cx = (float)luaL_checknumber(L_, 1);
    float cy = (float)luaL_checknumber(L_, 2);
    float r = (float)luaL_checknumber(L_, 3);
    float th = (float)luaL_checknumber(L_, 4);
    uint32_t c = color_arg(L_, 5);
    if (s_cur) gfx_arc(s_cur, cx, cy, r, th, 0, 360, c);
    return 0;
}

static int l_line(lua_State *L_)
{
    float x0 = (float)luaL_checknumber(L_, 1);
    float y0 = (float)luaL_checknumber(L_, 2);
    float x1 = (float)luaL_checknumber(L_, 3);
    float y1 = (float)luaL_checknumber(L_, 4);
    float th = (float)luaL_checknumber(L_, 5);
    uint32_t c = color_arg(L_, 6);
    if (s_cur) gfx_line(s_cur, x0, y0, x1, y1, th, c);
    return 0;
}

static int l_text(lua_State *L_)
{
    const char *s = luaL_checkstring(L_, 1);
    float x = (float)luaL_checknumber(L_, 2);
    float y = (float)luaL_checknumber(L_, 3);
    int size = (int)luaL_optinteger(L_, 4, 29);
    uint32_t c = color_arg(L_, 5);
    if (!s_cur) return 0;

    const mochi_font_t *f = size >= 60 ? &font_digits_106
                            : size <= 24 ? &font_num_22
                                         : &font_ui_29;
    /* y 传的是文字中心，按参考字形的包围盒反推基线 */
    uint32_t ref = size >= 60 ? '0' : (size <= 24 ? '0' : 'M');
    for (int i = 0; i < f->count; i++) {
        if (f->glyphs[i].cp == ref) {
            y -= f->glyphs[i].by + f->glyphs[i].h * 0.5f;
            break;
        }
    }
    gfx_text(s_cur, f, s, x, y, GFX_ALIGN_CENTER, c);
    return 0;
}

/* ---------------- time.* ---------------- */

static int l_now(lua_State *L_)
{
    rtc_time_t t;
    rtc_now(&t);
    lua_createtable(L_, 0, 7);
    lua_pushinteger(L_, t.year);
    lua_setfield(L_, -2, "year");
    lua_pushinteger(L_, t.mon);
    lua_setfield(L_, -2, "month");
    lua_pushinteger(L_, t.day);
    lua_setfield(L_, -2, "day");
    lua_pushinteger(L_, t.hour);
    lua_setfield(L_, -2, "hour");
    lua_pushinteger(L_, t.min);
    lua_setfield(L_, -2, "min");
    lua_pushinteger(L_, t.sec);
    lua_setfield(L_, -2, "sec");
    lua_pushinteger(L_, t.wday);
    lua_setfield(L_, -2, "wday");
    return 1;
}

static int l_log(lua_State *L_)
{
    const char *s = luaL_tolstring(L_, 1, NULL);
    ESP_LOGI(TAG, "%s", s);
    return 0;
}

/* 量文字宽度，插件排版用 */
static int l_measure(lua_State *L_)
{
    const char *s = luaL_checkstring(L_, 1);
    int size = (int)luaL_checknumber(L_, 2);
    const mochi_font_t *f = (size >= 60) ? &font_digits_106
                            : (size <= 24) ? &font_num_22
                                           : &font_ui_29;
    lua_pushnumber(L_, gfx_text_width(f, s));
    return 1;
}

/* net.up() —— 没网就别发请求 */
static int l_net_up(lua_State *L_)
{
    lua_pushboolean(L_, net_sta_up());
    return 1;
}

static const luaL_Reg net_funcs[] = {
    {"up", l_net_up},
    {NULL, NULL},
};

/* ui.icon(name, size, cx, cy) —— size 取 128 或 64 */
static int l_icon(lua_State *L_)
{
    const char *name = luaL_checkstring(L_, 1);
    int size = (int)luaL_checknumber(L_, 2);
    float cx = (float)luaL_checknumber(L_, 3);
    float cy = (float)luaL_checknumber(L_, 4);
    if (s_cur) icons_draw(s_cur, name, size, cx, cy);
    return 0;
}

/* store.get(key) / store.set(key, value) —— 每个插件自己的小 JSON，存在卡上 */
static cJSON *s_data;
static char s_data_path[160];

static int l_store_get(lua_State *L_)
{
    const char *k = luaL_checkstring(L_, 1);
    cJSON *v = s_data ? cJSON_GetObjectItem(s_data, k) : NULL;
    if (cJSON_IsString(v)) {
        lua_pushstring(L_, v->valuestring);
    } else {
        lua_pushnil(L_);
    }
    return 1;
}

static int l_store_set(lua_State *L_)
{
    const char *k = luaL_checkstring(L_, 1);
    const char *v = luaL_checkstring(L_, 2);
    if (!s_data) s_data = cJSON_CreateObject();
    if (!s_data) return 0;

    cJSON_DeleteItemFromObject(s_data, k);
    cJSON_AddStringToObject(s_data, k, v);

    char *js = cJSON_PrintUnformatted(s_data);
    if (js && s_data_path[0]) {
        FILE *f = sd_open_write(s_data_path, 10);
        if (f) {
            fwrite(js, 1, strlen(js), f);
            fclose(f);
        }
    }
    if (js) cJSON_free(js);
    return 0;
}

static const luaL_Reg store_funcs[] = {
    {"get", l_store_get},
    {"set", l_store_set},
    {NULL, NULL},
};

static int l_epoch(lua_State *L_)
{
    lua_pushinteger(L_, (lua_Integer)time(NULL));
    return 1;
}

/* ui.bg(name) —— 天气背景，成功返回 true */
static int l_bg(lua_State *L_)
{
    const char *name = luaL_checkstring(L_, 1);
    bool ok = false;
    if (s_cur) ok = bg_draw(s_cur, name);
    lua_pushboolean(L_, ok);
    return 1;
}

static const luaL_Reg ui_funcs[] = {
    {"clear", l_clear},   {"rect", l_rect},     {"round_rect", l_round_rect},
    {"circle", l_circle}, {"ring", l_ring},     {"arc", l_arc},
    {"line", l_line},     {"text", l_text},     {"measure", l_measure},
    {"icon", l_icon},     {"bg", l_bg},
    {NULL, NULL},
};

static int l_epoch(lua_State *L_);

static const luaL_Reg time_funcs[] = {
    {"now", l_now},
    {"epoch", l_epoch},
    {NULL, NULL},
};

/* ---------------- http.get + json.decode + app.get ---------------- */

static void http_worker(void *arg)
{
    (void)arg;
    http_req_t req;
    for (;;) {
        if (xQueueReceive(s_req_q, &req, portMAX_DELAY) != pdTRUE) continue;

        char *body = heap_caps_malloc(HTTP_BODY_MAX, MALLOC_CAP_SPIRAM);
        int status = 0;
        if (body) {
            esp_http_client_config_t cfg = {
                .url = req.url,
                .timeout_ms = 12000,
                .crt_bundle_attach = esp_crt_bundle_attach,
            };
            esp_http_client_handle_t c = esp_http_client_init(&cfg);
            if (c) {
                if (esp_http_client_open(c, 0) == ESP_OK) {
                    esp_http_client_fetch_headers(c);
                    int n = esp_http_client_read(c, body, HTTP_BODY_MAX - 1);
                    body[n > 0 ? n : 0] = 0;
                    status = esp_http_client_get_status_code(c);
                }
                esp_http_client_close(c);
                esp_http_client_cleanup(c);
            }
            if (status != 200) ESP_LOGW(TAG, "http %d: %s", status, req.url);
        }

        http_res_t res = {.ref = req.ref, .status = status, .body = body, .gen = req.gen};

        /* 成功的响应留一份给缓存 */
        if (status == 200 && body && body[0]) {
            if (!s_cache_stage) s_cache_stage = heap_caps_malloc(HTTP_BODY_MAX, MALLOC_CAP_SPIRAM);
            if (s_cache_stage) {
                strncpy(s_cache_stage, body, HTTP_BODY_MAX - 1);
                s_cache_stage[HTTP_BODY_MAX - 1] = 0;
                char *old = s_cache_body;
                s_cache_body = s_cache_stage;
                s_cache_stage = old;
                snprintf(s_cache_url, sizeof(s_cache_url), "%s", req.url);
                s_cache_status = status;
                s_cache_us = esp_timer_get_time();
            }
        }

        xQueueSend(s_res_q, &res, portMAX_DELAY);
    }
}

static int l_http_get(lua_State *L_)
{
    const char *url = luaL_checkstring(L_, 1);
    luaL_checktype(L_, 2, LUA_TFUNCTION);
    if (strlen(url) > URL_MAX) {
        lua_pushboolean(L_, 0);
        return 1;
    }

    lua_pushvalue(L_, 2);
    int ref = luaL_ref(L_, LUA_REGISTRYINDEX);

    /* 同一个 URL 两分钟内 повторно — 直接回调缓存，不占网络 */
    if (s_cache_body && s_cache_url[0] && !strcmp(url, s_cache_url) &&
        esp_timer_get_time() - s_cache_us < CACHE_TTL_US) {
        luaL_unref(L_, LUA_REGISTRYINDEX, ref);
        lua_pushvalue(L_, 2);
        lua_pushinteger(L_, s_cache_status);
        lua_pushstring(L_, s_cache_body);
        if (lua_pcall(L_, 2, 0, 0) != LUA_OK) lua_pop(L_, 1);
        lua_pushboolean(L_, 1);
        return 1;
    }

    http_req_t req;
    snprintf(req.url, sizeof(req.url), "%s", url);
    req.ref = ref;
    req.gen = s_gen;
    if (!s_req_q || xQueueSend(s_req_q, &req, 0) != pdTRUE) {
        luaL_unref(L_, LUA_REGISTRYINDEX, ref);
        lua_pushboolean(L_, 0);
        return 1;
    }
    lua_pushboolean(L_, 1);
    return 1;
}

static void json_to_lua(lua_State *L_, const cJSON *j)
{
    if (!j) {
        lua_pushnil(L_);
        return;
    }
    if (cJSON_IsObject(j)) {
        lua_newtable(L_);
        for (const cJSON *c = j->child; c; c = c->next) {
            json_to_lua(L_, c);
            lua_setfield(L_, -2, c->string ? c->string : "");
        }
    } else if (cJSON_IsArray(j)) {
        lua_newtable(L_);
        int i = 1;
        for (const cJSON *c = j->child; c; c = c->next) {
            json_to_lua(L_, c);
            lua_rawseti(L_, -2, i++);
        }
    } else if (cJSON_IsString(j)) {
        lua_pushstring(L_, j->valuestring);
    } else if (cJSON_IsNumber(j)) {
        lua_pushnumber(L_, (lua_Number)j->valuedouble);
    } else if (cJSON_IsBool(j)) {
        lua_pushboolean(L_, cJSON_IsTrue(j));
    } else {
        lua_pushnil(L_);
    }
}

static int l_json_decode(lua_State *L_)
{
    const char *s = luaL_checkstring(L_, 1);
    cJSON *j = cJSON_Parse(s);
    if (!j) {
        lua_pushnil(L_);
        return 1;
    }
    json_to_lua(L_, j);
    cJSON_Delete(j);
    return 1;
}

static int l_app_get(lua_State *L_)
{
    const char *k = luaL_checkstring(L_, 1);
    cJSON *v = s_conf ? cJSON_GetObjectItemCaseSensitive(s_conf, k) : NULL;
    if (cJSON_IsString(v)) {
        lua_pushstring(L_, v->valuestring);
    } else if (cJSON_IsNumber(v)) {
        lua_pushnumber(L_, (lua_Number)v->valuedouble);
    } else if (cJSON_IsBool(v)) {
        lua_pushboolean(L_, cJSON_IsTrue(v));
    } else {
        lua_pushnil(L_);
    }
    return 1;
}

static const luaL_Reg app_funcs[] = {
    {"get", l_app_get},
    {NULL, NULL},
};

static const luaL_Reg json_funcs[] = {
    {"decode", l_json_decode},
    {NULL, NULL},
};

static const luaL_Reg http_funcs[] = {
    {"get", l_http_get},
    {NULL, NULL},
};

static void register_api(lua_State *L_)
{
    luaL_newlib(L_, ui_funcs);
    lua_setglobal(L_, "ui");

    luaL_newlib(L_, time_funcs);
    lua_setglobal(L_, "time");

    luaL_newlib(L_, app_funcs);
    lua_setglobal(L_, "app");

    luaL_newlib(L_, json_funcs);
    lua_setglobal(L_, "json");

    luaL_newlib(L_, http_funcs);
    lua_setglobal(L_, "http");

    luaL_newlib(L_, net_funcs);
    lua_setglobal(L_, "net");

    luaL_newlib(L_, store_funcs);
    lua_setglobal(L_, "store");

    lua_register(L_, "log", l_log);
}

/* ---------------- 加载 ---------------- */

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 256 * 1024) {
        fclose(f);
        return NULL;
    }
    char *buf = heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    buf[got] = 0;
    *out_len = got;
    return buf;
}

static bool run_string(const char *src, size_t len, const char *name)
{
    if (luaL_loadbuffer(L, src, len, name) != LUA_OK) {
        set_error(lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        set_error(lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool lua_rt_start(const char *path, const char *plugin_id, const char *fallback_src)
{
    lua_rt_stop();

    s_gen++;
    if (!s_req_q) {
        s_req_q = xQueueCreate(4, sizeof(http_req_t));
        s_res_q = xQueueCreate(4, sizeof(http_res_t));
        if (s_req_q && s_res_q) {
            xTaskCreate(http_worker, "lua_http", 8192, NULL, 4, NULL);
        }
    }

    s_mem.used = 0;
    s_mem.cap = LUA_HEAP_CAP;
    s_failed = false;
    s_err[0] = 0;

    L = lua_newstate(lua_alloc, &s_mem);
    if (!L) {
        set_error("lua_newstate failed");
        return false;
    }

    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 1);

    register_api(L);

    /* 插件设置：/sdcard/plugins/<id>/settings.json */
    if (s_conf) {
        cJSON_Delete(s_conf);
        s_conf = NULL;
    }
    /* 插件自己的小数据（天气缓存等） */
    if (s_data) {
        cJSON_Delete(s_data);
        s_data = NULL;
    }
    s_data_path[0] = 0;
    if (plugin_id && plugin_id[0]) {
        plugins_path(plugin_id, "data.json", s_data_path, sizeof(s_data_path));
        FILE *f = fopen(s_data_path, "rb");
        if (f) {
            char *buf = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
            if (buf) {
                size_t n = fread(buf, 1, 8191, f);
                buf[n] = 0;
                s_data = cJSON_Parse(buf);
                heap_caps_free(buf);
            }
            fclose(f);
        }
    }
    if (plugin_id && plugin_id[0]) s_conf = plugin_conf_read(plugin_id);

    size_t len = 0;
    char *src = path ? read_file(path, &len) : NULL;
    bool ok;
    if (src) {
        ESP_LOGI(TAG, "loading %s (%u bytes)", path, (unsigned)len);
        ok = run_string(src, len, path);
        heap_caps_free(src);
    } else {
        ESP_LOGW(TAG, "no %s, running built-in demo", path ? path : "?");
        ok = run_string(fallback_src, strlen(fallback_src), "=demo");
    }

    if (!ok) {
        ESP_LOGE(TAG, "load failed: %s", s_err);
        return false;
    }

    lua_getglobal(L, "on_start");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            set_error(lua_tostring(L, -1));
            ESP_LOGE(TAG, "on_start failed: %s", s_err);
            return false;
        }
    } else {
        lua_pop(L, 1);
    }

    s_active = true;
    ESP_LOGI(TAG, "plugin ready, heap %u/%u KB", (unsigned)(s_mem.used / 1024),
             (unsigned)(s_mem.cap / 1024));
    return true;
}

size_t lua_rt_heap_used(void)
{
    return s_mem.used;
}

void lua_rt_stop(void)
{
    if (s_data) {
        cJSON_Delete(s_data);
        s_data = NULL;
    }
    if (s_conf) {
        cJSON_Delete(s_conf);
        s_conf = NULL;
    }
    if (s_res_q) {
        http_res_t r;
        while (xQueueReceive(s_res_q, &r, 0) == pdTRUE) {
            if (r.body) heap_caps_free(r.body);
        }
    }
    if (L) {
        lua_close(L);
        L = NULL;
    }
    s_active = false;
    s_cur = NULL;
}

bool lua_rt_active(void)
{
    return s_active && L != NULL;
}

bool lua_rt_failed(void)
{
    return s_failed;
}

const char *lua_rt_error(void)
{
    return s_err;
}

void lua_rt_draw(gfx_t *g, float dt)
{
    if (!L || !s_active) return;

    /* 先处理 http.get 的结果：回调一定在绘制线程里执行 */
    http_res_t res;
    while (s_res_q && xQueueReceive(s_res_q, &res, 0) == pdTRUE) {
        if (res.gen != s_gen) {
            if (res.body) heap_caps_free(res.body);
            continue;
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, res.ref);
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, res.status);
            lua_pushstring(L, res.body ? res.body : "");
            if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                set_error(lua_tostring(L, -1));
                lua_pop(L, 1);
                s_active = false;
            }
        } else {
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, res.ref);
        if (res.body) heap_caps_free(res.body);
        if (!s_active) return;
    }

    lua_getglobal(L, "on_draw");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    s_cur = g;
    lua_pushnumber(L, (lua_Number)dt);
    lua_sethook(L, hook, LUA_MASKCOUNT, DRAW_BUDGET);
    int rc = lua_pcall(L, 1, 0, 0);
    lua_sethook(L, NULL, 0, 0);
    s_cur = NULL;

    if (rc != LUA_OK) {
        set_error(lua_tostring(L, -1));
        lua_pop(L, 1);
        ESP_LOGE(TAG, "on_draw failed: %s", s_err);
        s_active = false;
    }
}
