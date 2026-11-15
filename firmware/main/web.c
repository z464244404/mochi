#include "web.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "builtin_plugins.h"
#include "cJSON.h"
#include "dsec.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "net.h"
#include "plugin_conf.h"
#include "plugins.h"
#include "sd.h"
#include "fontpack.h"
#include "www.h"

static const char *TAG = "web";
static httpd_handle_t s_srv;

float mochi_fps(void);

/* ------------------------------------------------------------------ */

static esp_err_t send_json(httpd_req_t *r, cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_sendstr(r, s ? s : "{}");
    free(s);
    cJSON_Delete(j);
    return ESP_OK;
}

static bool query(httpd_req_t *r, const char *key, char *out, size_t n)
{
    /* 中文 SSID 百分号编码后每个汉字占 9 个字符，再加 64 字节密码，256 不够用 */
    char buf[640];
    if (httpd_req_get_url_query_str(r, buf, sizeof(buf)) != ESP_OK) return false;
    if (httpd_query_key_value(buf, key, out, n) != ESP_OK) return false;
    return true;
}

/* 文件/插件名净化：不允许路径分隔符 */
static bool name_ok(const char *s)
{
    if (!s || !s[0] || strlen(s) > 63) return false;
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return strstr(s, "..") == NULL;
}

/* ------------------------------------------------------------------ */

static esp_err_t root_get(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    return httpd_resp_send(r, (const char *)www_gz, www_gz_len);
}

static esp_err_t status_get(httpd_req_t *r)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "fw", esp_app_get_description()->version);
    cJSON_AddStringToObject(j, "app", apps_current()->id);
    cJSON_AddStringToObject(j, "ssid", net_ap_ssid());
    cJSON_AddStringToObject(j, "ip", net_ap_ip());
    cJSON_AddStringToObject(j, "ap_ip", net_ap_ip());
    cJSON_AddStringToObject(j, "sta_ip", net_sta_ip());
    cJSON_AddStringToObject(j, "sta_ssid", net_sta_ssid());
    cJSON_AddBoolToObject(j, "sta", net_sta_up());
    cJSON_AddBoolToObject(j, "sd", sd_ready());
    cJSON_AddNumberToObject(j, "fps", mochi_fps());
    cJSON_AddNumberToObject(j, "heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(j, "psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(j, "font", fp_count());
    cJSON_AddNumberToObject(j, "font29", fp_count_px(29));
    cJSON_AddNumberToObject(j, "font22", fp_count_px(22));

    const char *st = "idle";
    switch (net_sta_state()) {
    case NET_STA_CONNECTING: st = "connecting"; break;
    case NET_STA_CONNECTED: st = "connected"; break;
    case NET_STA_FAILED: st = "failed"; break;
    default: st = "idle"; break;
    }
    cJSON_AddStringToObject(j, "sta_state", st);
    cJSON_AddNumberToObject(j, "sta_reason", net_sta_reason());
    cJSON_AddNumberToObject(j, "sta_rssi", net_sta_rssi());
    return send_json(r, j);
}

/* GET /api/scan —�?阻塞 2~4 秒扫一�?*/
static esp_err_t scan_get(httpd_req_t *r)
{
    static net_ap_t list[20];
    int n = net_scan(list, 20);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", list[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", list[i].rssi);
        cJSON_AddNumberToObject(o, "auth", list[i].auth);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "networks", arr);
    return send_json(r, j);
}

static esp_err_t apps_get(httpd_req_t *r)
{
    cJSON *arr = cJSON_CreateArray();

    /* built-in plugins (compiled in) */
    for (int i = 0; i < mochi_builtin_count; i++) {
        const mochi_builtin_t *b = &mochi_builtins[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", b->id);
        cJSON_AddStringToObject(o, "name", b->name);
        cJSON_AddStringToObject(o, "version", "1.0.0");
        cJSON_AddStringToObject(o, "kind", "builtin");
        cJSON_AddBoolToObject(o, "enabled", plugins_enabled(b->id));
        cJSON_AddItemToArray(arr, o);
    }

    /* lua plugins on the card */
    plugin_info_t list[PLUGIN_MAX];
    int n = plugins_scan(list, PLUGIN_MAX);
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", list[i].id);
        cJSON_AddStringToObject(o, "name", list[i].name);
        cJSON_AddStringToObject(o, "version", list[i].version);
        cJSON_AddStringToObject(o, "kind", "lua");
        cJSON_AddBoolToObject(o, "enabled", plugins_enabled(list[i].id));
        cJSON_AddItemToArray(arr, o);
    }

    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "apps", arr);
    return send_json(r, j);
}

/* POST /api/app/enable?id=<plugin>&on=0|1 */
static esp_err_t app_enable_post(httpd_req_t *r)
{
    char id[64] = {0}, on[8] = {0};
    if (!query(r, "id", id, sizeof(id)) || !query(r, "on", on, sizeof(on))) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "id/on required");
        return ESP_FAIL;
    }
    if (!plugins_id_ok(id) && !mochi_builtin_find(id)) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_FAIL;
    }

    plugins_set_enabled(id, strcmp(on, "0") != 0);
    apps_reload();

    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    return send_json(r, j);
}

/* POST /api/upload?id=<plugin>&file=<name>  原始 body 写入文件 */
static esp_err_t upload_post(httpd_req_t *r)
{
    char id[64] = {0}, file[64] = {0};
    if (!query(r, "id", id, sizeof(id)) || !query(r, "file", file, sizeof(file))) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "id/file required");
        return ESP_FAIL;
    }
    if (!plugins_id_ok(id) || !name_ok(file)) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad name");
        return ESP_FAIL;
    }

    char dir[160], path[200];
    plugins_path(id, NULL, dir, sizeof(dir));
    plugins_path(id, file, path, sizeof(path));

    /* 新建目录后立�?fopen 偶尔会失败（FATFS 首次写），用带重试的打开 */
    FILE *f = NULL;
    for (int attempt = 0; attempt < 3 && !f; attempt++) {
        mkdir(PLUGIN_ROOT, 0777);
        mkdir(dir, 0777);
        f = sd_open_write(path, 5);
    }
    if (!f) {
        ESP_LOGE(TAG, "open %s failed", path);
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
        return ESP_FAIL;
    }

    char buf[2048];
    int remaining = r->content_len;
    while (remaining > 0) {
        int want = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int got = httpd_req_recv(r, buf, want);
        if (got <= 0) {
            fclose(f);
            unlink(path);
            httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_FAIL;
        }
        fwrite(buf, 1, got, f);
        remaining -= got;
    }
    fclose(f);

    ESP_LOGI(TAG, "uploaded %s/%s (%d bytes)", id, file, (int)r->content_len);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    return send_json(r, j);
}

/* POST /api/install?id=<plugin> */
static esp_err_t install_post(httpd_req_t *r)
{
    char id[64] = {0};
    if (!query(r, "id", id, sizeof(id)) || !plugins_id_ok(id)) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_FAIL;
    }
    if (!plugins_has(id, "main.lua")) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "main.lua missing");
        return ESP_FAIL;
    }

    char path[200];
    plugins_path(id, "manifest.json", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "manifest.json missing");
        return ESP_FAIL;
    }
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;

    cJSON *m = cJSON_Parse(buf);
    if (!m) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "manifest is not valid json");
        return ESP_FAIL;
    }
    cJSON_Delete(m);

    plugins_set_active(id);
    ESP_LOGI(TAG, "installed %s", id);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    return send_json(r, j);
}

/* DELETE /api/app?id=<plugin> */
static esp_err_t app_delete(httpd_req_t *r)
{
    char id[64] = {0};
    if (!query(r, "id", id, sizeof(id)) || !plugins_id_ok(id)) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_FAIL;
    }
    bool ok = plugins_delete(id);
    apps_reload();

    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", ok);
    return send_json(r, j);
}

/* POST /api/active?id=<plugin> */
static esp_err_t active_post(httpd_req_t *r)
{
    char id[64] = {0};
    if (!query(r, "id", id, sizeof(id)) || !plugins_id_ok(id)) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_FAIL;
    }
    plugins_set_active(id);
    apps_reload();

    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    return send_json(r, j);
}

/* POST /api/fontpack —— 上传 TF 卡字体包（body 就是 fontpack.bin 原始字节） */
static esp_err_t fontpack_put(httpd_req_t *r)
{
    if (r->content_len <= 0) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }

    char *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!buf) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }

    mkdir("/sdcard/fonts", 0777);
    char which[8] = "";
    query(r, "size", which, sizeof(which));
    bool is22 = (which[0] == '2');
    const char *tmp = is22 ? "/sdcard/fonts/fontpack22.tmp" : "/sdcard/fonts/fontpack.tmp";
    const char *dst = is22 ? "/sdcard/fonts/fontpack22.bin" : "/sdcard/fonts/fontpack.bin";

    FILE *f = NULL;
    for (int attempt = 0; attempt < 3 && !f; attempt++) f = sd_open_write(tmp, 5);
    if (!f) {
        heap_caps_free(buf);
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
        return ESP_FAIL;
    }

    int remaining = r->content_len;
    int total = 0;
    bool io_ok = true;
    while (remaining > 0) {
        int want = remaining < 4096 ? remaining : 4096;
        int n = httpd_req_recv(r, buf, want);
        if (n <= 0) {
            io_ok = false;
            break;
        }
        if (fwrite(buf, 1, n, f) != (size_t)n) {
            io_ok = false;
            break;
        }
        total += n;
        remaining -= n;
    }
    fclose(f);
    heap_caps_free(buf);

    if (!io_ok) {
        unlink(tmp);
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
        return ESP_FAIL;
    }
    unlink(dst);
    rename(tmp, dst);

    bool ok = fp_init();
    ESP_LOGI(TAG, "fontpack uploaded: %d bytes, %d glyphs", total, fp_count());

    cJSON *j = cJSON_CreateObject();
    if (!j) return ESP_OK;
    cJSON_AddBoolToObject(j, "ok", ok);
    cJSON_AddNumberToObject(j, "bytes", total);
    cJSON_AddNumberToObject(j, "glyphs", fp_count());
    cJSON_AddNumberToObject(j, "glyphs29", fp_count_px(29));
    cJSON_AddNumberToObject(j, "glyphs22", fp_count_px(22));
    return send_json(r, j);
}

/* url_decode 定义在下面（dfm 那一节），这里先声明，music 要用 */
static void url_decode(char *s);

/* GET /api/shot.bmp —— 把当前帧导成 BMP（浏览器打开就能存图） */
extern const uint16_t *mochi_last_frame(void);
extern void mochi_pause_render(bool on);

static esp_err_t shot_bmp_get(httpd_req_t *r)
{
    mochi_pause_render(true);
    vTaskDelay(pdMS_TO_TICKS(80)); /* 等当前这帧画完 */
    const uint16_t *fb = mochi_last_frame();
    if (!fb) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "no frame");
        return ESP_FAIL;
    }

    const int w = 480, h = 480;
    const int row = (w * 3 + 3) & ~3; /* BMP 每行 4 字节对齐 */
    const uint32_t data = (uint32_t)row * h;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    uint32_t total = 54 + data;
    memcpy(&hdr[2], &total, 4);
    uint32_t off = 54;
    memcpy(&hdr[10], &off, 4);
    uint32_t hsz = 40;
    memcpy(&hdr[14], &hsz, 4);
    memcpy(&hdr[18], &w, 4);
    memcpy(&hdr[22], &h, 4);
    uint16_t planes = 1, bpp = 24;
    memcpy(&hdr[26], &planes, 2);
    memcpy(&hdr[28], &bpp, 2);
    memcpy(&hdr[34], &data, 4);

    httpd_resp_set_type(r, "image/bmp");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_send_chunk(r, (const char *)hdr, 54);

    uint8_t *line = heap_caps_malloc(row, MALLOC_CAP_SPIRAM);
    if (!line) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }
    for (int y = h - 1; y >= 0; y--) {
        const uint16_t *src = fb + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint16_t c = src[x];
            line[x * 3 + 0] = (uint8_t)((c & 0x1F) << 3);          /* B */
            line[x * 3 + 1] = (uint8_t)(((c >> 5) & 0x3F) << 2);   /* G */
            line[x * 3 + 2] = (uint8_t)(((c >> 11) & 0x1F) << 3);  /* R */
        }
        httpd_resp_send_chunk(r, (const char *)line, row);
    }
    heap_caps_free(line);
    httpd_resp_send_chunk(r, NULL, 0);
    mochi_pause_render(false);
    return ESP_OK;
}

/* POST /api/music?title=&artist=&pos=&dur=&playing=&ts=
 * 电脑上的 tools/music_sync.py 把系统媒体信息推过来（音乐副屏用） */
static esp_err_t music_post(httpd_req_t *r)
{
    char title[128] = "", artist[96] = "", pos[16] = "0", dur[16] = "0";
    char playing[8] = "0", ts[24] = "0";

    query(r, "title", title, sizeof(title));
    query(r, "artist", artist, sizeof(artist));
    query(r, "pos", pos, sizeof(pos));
    query(r, "dur", dur, sizeof(dur));
    query(r, "playing", playing, sizeof(playing));
    query(r, "ts", ts, sizeof(ts));
    url_decode(title);
    url_decode(artist);

    cJSON *o = cJSON_CreateObject();
    if (!o) return ESP_OK;
    cJSON_AddStringToObject(o, "title", title);
    cJSON_AddStringToObject(o, "artist", artist);
    cJSON_AddStringToObject(o, "pos", pos);
    cJSON_AddStringToObject(o, "dur", dur);
    cJSON_AddStringToObject(o, "playing", playing);
    cJSON_AddStringToObject(o, "ts", ts);

    char *js = cJSON_PrintUnformatted(o);
    bool ok = false;
    if (js) {
        FILE *f = sd_open_write("/sdcard/plugins/music/data.json", 3);
        if (f) {
            ok = fwrite(js, 1, strlen(js), f) == strlen(js);
            fclose(f);
        }
        cJSON_free(js);
    }
    cJSON_Delete(o);

    cJSON *res = cJSON_CreateObject();
    if (res) {
        cJSON_AddBoolToObject(res, "ok", ok);
        return send_json(r, res);
    }
    return ESP_OK;
}

/* POST /api/music/cover —— 裸 RGB565（240x240x2 = 115200 字节） */
static esp_err_t music_cover_post(httpd_req_t *r)
{
    if (r->content_len != 240 * 240 * 2) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "expect 240x240 rgb565");
        return ESP_FAIL;
    }
    char *buf = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!buf) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }
    mkdir("/sdcard/plugins", 0777);
    mkdir("/sdcard/plugins/music", 0777);
    FILE *f = NULL;
    for (int attempt = 0; attempt < 3 && !f; attempt++)
        f = sd_open_write("/sdcard/plugins/music/cover.565", 5);
    if (!f) {
        heap_caps_free(buf);
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
        return ESP_FAIL;
    }

    int remaining = r->content_len;
    int total = 0;
    bool io_ok = true;
    while (remaining > 0) {
        int want = remaining < 8192 ? remaining : 8192;
        int n = httpd_req_recv(r, buf, want);
        if (n <= 0) { io_ok = false; break; }
        if (fwrite(buf, 1, n, f) != (size_t)n) { io_ok = false; break; }
        total += n;
        remaining -= n;
    }
    fclose(f);
    heap_caps_free(buf);

    ESP_LOGI(TAG, "music cover: %d bytes ok=%d", total, (int)io_ok);
    cJSON *res = cJSON_CreateObject();
    if (res) {
        cJSON_AddBoolToObject(res, "ok", io_ok);
        cJSON_AddNumberToObject(res, "bytes", total);
        return send_json(r, res);
    }
    return ESP_OK;
}

/* POST /api/dfm?nick=&rank=&liquid=&noncur=&gain=&ts=
 * 电脑上的 tools/df_sync.py 拉完腾讯接口后推过来，插件只负责显示 */
static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '%' && p[1] && p[2]) {
            char hex[3] = {p[1], p[2], 0};
            *o++ = (char)strtol(hex, NULL, 16);
            p += 2;
        } else if (*p == '+') {
            *o++ = ' ';
        } else {
            *o++ = *p;
        }
    }
    *o = 0;
}

static esp_err_t dfm_post(httpd_req_t *r)
{
    char nick[128] = "", rank[16] = "0", liquid[24] = "0", noncur[24] = "0";
    char gain[24] = "0", ts[24] = "0";

    query(r, "nick", nick, sizeof(nick));
    query(r, "rank", rank, sizeof(rank));
    query(r, "liquid", liquid, sizeof(liquid));
    query(r, "noncur", noncur, sizeof(noncur));
    query(r, "gain", gain, sizeof(gain));
    query(r, "ts", ts, sizeof(ts));
    url_decode(nick);

    cJSON *o = cJSON_CreateObject();
    if (!o) return send_json(r, NULL);
    cJSON_AddStringToObject(o, "nick", nick);
    cJSON_AddStringToObject(o, "rank", rank);
    cJSON_AddStringToObject(o, "liquid", liquid);
    cJSON_AddStringToObject(o, "noncur", noncur);
    cJSON_AddStringToObject(o, "gain", gain);
    cJSON_AddStringToObject(o, "ts", ts);

    char *js = cJSON_PrintUnformatted(o);
    bool ok = false;
    if (js) {
        FILE *f = sd_open_write("/sdcard/plugins/dfm/data.json", 3);
        if (f) {
            ok = fwrite(js, 1, strlen(js), f) == strlen(js);
            fclose(f);
        }
        cJSON_free(js);
    }
    cJSON_Delete(o);

    ESP_LOGI(TAG, "dfm push: %s rank=%s (+%s) ok=%d", nick, rank, gain, (int)ok);
    cJSON *res = cJSON_CreateObject();
    if (res) {
        cJSON_AddBoolToObject(res, "ok", ok);
        return send_json(r, res);
    }
    return ESP_OK;
}

/* POST /api/gesture?event=next|prev|shake|tap */
static esp_err_t gesture_post(httpd_req_t *r)
{
    char ev[16] = {0};
    if (!query(r, "event", ev, sizeof(ev))) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "event required");
        return ESP_FAIL;
    }
    if (!strcmp(ev, "next")) {
        apps_next();
    } else if (!strcmp(ev, "prev")) {
        apps_prev();
    } else if (!strcmp(ev, "shake")) {
        apps_next();
    } else if (!strcmp(ev, "tap")) {
        apps_event(APP_EV_TAP);
    }
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    return send_json(r, j);
}

/* 只解 %XX，不把 '+' 当空格：网页用 encodeURIComponent，
 * 空格是 %20、'+' 原样保留，按表单语义解会把 SSID 里的 '+' 变成空格 */
static void url_decode_pct(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '%' && p[1] && p[2]) {
            char hex[3] = {p[1], p[2], 0};
            char *end = NULL;
            long v = strtol(hex, &end, 16);
            if (end && *end == 0) {
                *o++ = (char)v;
                p += 2;
                continue;
            }
        }
        *o++ = *p;
    }
    *o = 0;
}

/* POST /api/wifi?ssid=..&pass=.. */
static esp_err_t wifi_post(httpd_req_t *r)
{
    /* 百分号编码后的中文 SSID 可能到 96 字符，缓冲区要够大 */
    char ssid[192] = {0}, pass[224] = {0};
    if (!query(r, "ssid", ssid, sizeof(ssid))) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "ssid required");
        return ESP_FAIL;
    }
    query(r, "pass", pass, sizeof(pass));
    url_decode_pct(ssid);
    url_decode_pct(pass);

    /* SSID 最多 32 字节（UTF-8 下约 10 个汉字），WPA2 密码 8~63 字节 */
    size_t sl = strlen(ssid), pl = strlen(pass);
    if (sl == 0 || sl > 32) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "ssid must be 1..32 bytes");
        return ESP_FAIL;
    }
    if (pl > 63) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "pass too long");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "wifi: ssid %u bytes, pass %u bytes", (unsigned)sl, (unsigned)pl);
    net_set_sta(ssid, pass);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    return send_json(r, j);
}

/* POST /api/ota  原始固件 */
static esp_err_t ota_post(httpd_req_t *r)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t h;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
        return ESP_FAIL;
    }

    char buf[4096];
    int remaining = r->content_len;
    while (remaining > 0) {
        int want = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int got = httpd_req_recv(r, buf, want);
        if (got <= 0) {
            esp_ota_abort(h);
            httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_FAIL;
        }
        if (esp_ota_write(h, buf, got) != ESP_OK) {
            esp_ota_abort(h);
            httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
            return ESP_FAIL;
        }
        remaining -= got;
    }

    if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "ota finish failed");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "OTA done, restarting");
    httpd_resp_sendstr(r, "ok, rebooting");
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
    return ESP_OK;
}

/* GET /api/app?id=  -> manifest + current settings */
static esp_err_t app_get(httpd_req_t *r)
{
    char id[64] = {0};
    if (!query(r, "id", id, sizeof(id))) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_FAIL;
    }

    const mochi_builtin_t *b = mochi_builtin_find(id);
    char *buf = NULL;

    if (b) {
        /* compiled-in manifest */
        size_t len = strlen(b->manifest);
        buf = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
        if (buf) memcpy(buf, b->manifest, len + 1);
    } else {
        if (!plugins_id_ok(id)) {
            httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
            return ESP_FAIL;
        }
        char path[200];
        plugins_path(id, "manifest.json", path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (f) {
            buf = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
            if (buf) {
                size_t n = fread(buf, 1, 2047, f);
                buf[n] = 0;
            }
            fclose(f);
        }
    }
    if (!buf) {
        httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, "manifest missing");
        return ESP_FAIL;
    }

    cJSON *j = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "manifest is not json");
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(j, "enabled", plugins_enabled(id));
    cJSON_AddBoolToObject(j, "builtin", b != NULL);

    /* manifest.settings is the form schema; current values go to "values" */
    cJSON *conf = plugin_conf_read(id);
    cJSON_AddItemToObject(j, "values", conf ? conf : cJSON_CreateObject());
    return send_json(r, j);
}

/* POST /api/app/settings?id=   body is a flat JSON object */
static esp_err_t app_settings_post(httpd_req_t *r)
{
    char id[64] = {0};
    if (!query(r, "id", id, sizeof(id))) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_FAIL;
    }
    if (!plugins_id_ok(id) && !mochi_builtin_find(id)) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_FAIL;
    }

    /* builtin apps may not have a folder on the card yet */
    char dir[160];
    plugins_path(id, NULL, dir, sizeof(dir));
    mkdir(PLUGIN_ROOT, 0777);
    mkdir(dir, 0777);

    char buf[2048];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int got = httpd_req_recv(r, buf + total, sizeof(buf) - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    buf[total] = 0;

    if (!plugin_conf_write(id, buf, total)) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "save failed");
        return ESP_FAIL;
    }

    apps_reload();
    dsec_reload();

    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    cJSON_AddNumberToObject(j, "bytes", total);
    return send_json(r, j);
}

/* GET /api/builtin */
static esp_err_t builtin_list_get(httpd_req_t *r)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < builtin_count(); i++) {
        const builtin_plugin_t *p = builtin_get(i);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", p->id);
        cJSON_AddStringToObject(o, "name", p->name);
        cJSON_AddBoolToObject(o, "installed", plugins_has(p->id, "main.lua"));
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "plugins", arr);
    return send_json(r, j);
}

/* POST /api/builtin/install?id= */
static esp_err_t builtin_install_post(httpd_req_t *r)
{
    char id[64] = {0};
    if (!query(r, "id", id, sizeof(id)) || !plugins_id_ok(id)) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_FAIL;
    }

    int n = builtin_install(id);
    if (n <= 0) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
        return ESP_FAIL;
    }
    plugins_set_active(id);
    apps_reload();

    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    cJSON_AddNumberToObject(j, "files", n);
    return send_json(r, j);
}

/* ------------------------------------------------------------------ *
 * 自检：设备访问自己的 HTTP 服务，把「上�?�?安装 �?列表」整条链路跑一遍�? * 没有手机/电脑连热点时也能确认后台是通的�? * ------------------------------------------------------------------ */
#ifndef MOCHI_WEB_SELFTEST
#define MOCHI_WEB_SELFTEST 0
#endif

#if MOCHI_WEB_SELFTEST
static int http_get(const char *url, char *out, int cap)
{
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 6000};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    esp_http_client_set_method(c, HTTP_METHOD_GET);
    if (esp_http_client_open(c, 0) != ESP_OK) {
        esp_http_client_cleanup(c);
        return -1;
    }
    esp_http_client_fetch_headers(c);
    int n = esp_http_client_read(c, out, cap - 1);
    if (n < 0) n = 0;
    out[n] = 0;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return n;
}

static int http_post(const char *url, const char *body, int len)
{
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 8000};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    esp_http_client_set_method(c, HTTP_METHOD_POST);
    esp_http_client_set_header(c, "Content-Type", "application/octet-stream");
    if (esp_http_client_open(c, len) != ESP_OK) {
        esp_http_client_cleanup(c);
        return -1;
    }
    esp_http_client_write(c, body, len);
    esp_http_client_fetch_headers(c);
    char tmp[128];
    int n = esp_http_client_read(c, tmp, sizeof(tmp) - 1);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return n;
}

static void selftest_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2500));
    char buf[1024];
    int n = http_get("http://192.168.4.1/api/apps", buf, sizeof(buf));
    ESP_LOGI(TAG, "selftest apps: %d bytes %.400s", n, n > 0 ? buf : "(failed)");
    vTaskDelete(NULL);
}
#endif

/* ------------------------------------------------------------------ */

esp_err_t web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 32; /* 接口已经 21 个了，20 会静默丢最后一个 */
    cfg.stack_size = 12288;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;

    if (httpd_start(&s_srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return ESP_FAIL;
    }

    static const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_get},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = scan_get},
        {.uri = "/api/apps", .method = HTTP_GET, .handler = apps_get},
        {.uri = "/api/app", .method = HTTP_GET, .handler = app_get},
        {.uri = "/api/app/settings", .method = HTTP_POST, .handler = app_settings_post},
        {.uri = "/api/app/enable", .method = HTTP_POST, .handler = app_enable_post},
        {.uri = "/api/builtin", .method = HTTP_GET, .handler = builtin_list_get},
        {.uri = "/api/builtin/install", .method = HTTP_POST, .handler = builtin_install_post},
        {.uri = "/api/upload", .method = HTTP_POST, .handler = upload_post},
        {.uri = "/api/install", .method = HTTP_POST, .handler = install_post},
        {.uri = "/api/active", .method = HTTP_POST, .handler = active_post},
        {.uri = "/api/app", .method = HTTP_DELETE, .handler = app_delete},
        {.uri = "/api/gesture", .method = HTTP_POST, .handler = gesture_post},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post},
    {.uri = "/api/dfm", .method = HTTP_POST, .handler = dfm_post},
    {.uri = "/api/fontpack", .method = HTTP_POST, .handler = fontpack_put},
    {.uri = "/api/shot.bmp", .method = HTTP_GET, .handler = shot_bmp_get},
    {.uri = "/api/music", .method = HTTP_POST, .handler = music_post},
    {.uri = "/api/music/cover", .method = HTTP_POST, .handler = music_cover_post},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_srv, &uris[i]);
    }

    ESP_LOGI(TAG, "web on http://%s/", net_ap_ip());
#if MOCHI_WEB_SELFTEST
    xTaskCreate(selftest_task, "wstest", 6144, NULL, 3, NULL);
#endif
    return ESP_OK;
}
