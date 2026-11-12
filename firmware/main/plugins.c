#include "plugins.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "plugins";

static char s_active[32];

void plugins_path(const char *id, const char *file, char *out, size_t n)
{
    if (file && file[0]) {
        snprintf(out, n, "%s/%s/%s", PLUGIN_ROOT, id, file);
    } else {
        snprintf(out, n, "%s/%s", PLUGIN_ROOT, id);
    }
}

bool plugins_id_ok(const char *id)
{
    if (!id || !id[0] || strlen(id) > 31) return false;
    for (const char *p = id; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return strcmp(id, ".") != 0 && strcmp(id, "..") != 0;
}

bool plugins_has(const char *id, const char *file)
{
    char path[160];
    plugins_path(id, file, path, sizeof(path));
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void read_manifest(const char *dir_id, char *id, size_t ilen, char *name, size_t nlen,
                          char *ver, size_t vlen)
{
    snprintf(id, ilen, "%.31s", dir_id);
    snprintf(name, nlen, "%s", dir_id);
    ver[0] = 0;

    char path[160];
    plugins_path(dir_id, "manifest.json", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;

    cJSON *j = cJSON_Parse(buf);
    if (!j) return;
    cJSON *ji = cJSON_GetObjectItem(j, "id");
    cJSON *jn = cJSON_GetObjectItem(j, "name");
    cJSON *jv = cJSON_GetObjectItem(j, "version");
    /* 以 manifest 里的 id 为准：FAT 回读目录名可能变成 8.3 大写 */
    if (cJSON_IsString(ji) && ji->valuestring && plugins_id_ok(ji->valuestring)) {
        snprintf(id, ilen, "%.31s", ji->valuestring);
    }
    if (cJSON_IsString(jn) && jn->valuestring) snprintf(name, nlen, "%.47s", jn->valuestring);
    if (cJSON_IsString(jv) && jv->valuestring) snprintf(ver, vlen, "%.15s", jv->valuestring);
    cJSON_Delete(j);
}

int plugins_scan(plugin_info_t *out, int max)
{
    int n = 0;
    DIR *d = opendir(PLUGIN_ROOT);
    if (!d) return 0;

    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.') continue;
        if (!plugins_id_ok(e->d_name)) continue;
        if (!plugins_has(e->d_name, "manifest.json") || !plugins_has(e->d_name, "main.lua")) {
            continue;
        }
        snprintf(out[n].id, sizeof(out[n].id), "%.31s", e->d_name);
        read_manifest(out[n].id, out[n].id, sizeof(out[n].id), out[n].name, sizeof(out[n].name),
                      out[n].version, sizeof(out[n].version));
        n++;
    }
    closedir(d);
    return n;
}

const char *plugins_active(void)
{
    if (s_active[0]) return s_active;

    nvs_handle_t h;
    if (nvs_open("mochi", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(s_active);
        nvs_get_str(h, "plugin", s_active, &n);
        nvs_close(h);
    }
    if (s_active[0] && plugins_has(s_active, "main.lua")) return s_active;

    plugin_info_t list[PLUGIN_MAX];
    int n = plugins_scan(list, PLUGIN_MAX);
    if (n > 0) {
        snprintf(s_active, sizeof(s_active), "%s", list[0].id);
        return s_active;
    }
    s_active[0] = 0;
    return s_active;
}

void plugins_set_active(const char *id)
{
    snprintf(s_active, sizeof(s_active), "%.31s", id);
    nvs_handle_t h;
    if (nvs_open("mochi", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "plugin", s_active);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---------------- 启用 / 停用 ---------------- */

#define OFF_MAX 255

static void read_off(char *buf, size_t n)
{
    buf[0] = 0;
    nvs_handle_t h;
    if (nvs_open("mochi", NVS_READONLY, &h) != ESP_OK) return;
    size_t len = n;
    nvs_get_str(h, "off", buf, &len);
    nvs_close(h);
}

static bool list_has(const char *list, const char *id)
{
    size_t idlen = strlen(id);
    const char *p = list;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == idlen && !strncmp(p, id, len)) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

bool plugins_enabled(const char *id)
{
    char buf[OFF_MAX + 1];
    read_off(buf, sizeof(buf));
    if (!buf[0]) return true; /* 默认全开 */
    return !list_has(buf, id);
}

void plugins_set_enabled(const char *id, bool on)
{
    char buf[OFF_MAX + 1];
    read_off(buf, sizeof(buf));

    /* 先重建一份不含 id 的列表，再按需要追加 */
    char out[OFF_MAX + 1] = {0};
    size_t o = 0;
    const char *p = buf;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len && !(len == strlen(id) && !strncmp(p, id, len))) {
            if (o && o < OFF_MAX) out[o++] = ',';
            if (o + len <= OFF_MAX) {
                memcpy(out + o, p, len);
                o += len;
            }
        }
        if (!e) break;
        p = e + 1;
    }
    if (!on && o + strlen(id) + 2 <= OFF_MAX) {
        if (o) out[o++] = ',';
        memcpy(out + o, id, strlen(id));
        o += strlen(id);
    }
    out[o] = 0;

    nvs_handle_t h;
    if (nvs_open("mochi", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "off", out);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "%s %s (off list: '%s')", id, on ? "enabled" : "disabled", out);
}

bool plugins_delete(const char *id)
{
    if (!plugins_id_ok(id)) return false;

    char dir[160];
    plugins_path(id, NULL, dir, sizeof(dir));

    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[256];
        snprintf(p, sizeof(p), "%.159s/%.63s", dir, e->d_name);
        unlink(p);
    }
    closedir(d);

    bool ok = rmdir(dir) == 0;
    if (strcmp(s_active, id) == 0) {
        s_active[0] = 0;
        plugins_set_active("");
    }
    ESP_LOGI(TAG, "delete %s: %s", id, ok ? "ok" : "failed");
    return ok;
}
