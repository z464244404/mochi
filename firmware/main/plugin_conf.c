#include "plugin_conf.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "plugins.h"
#include "sd.h"

static const char *TAG = "pconf";

#define CONF_MAX 4096

cJSON *plugin_conf_read(const char *id)
{
    char path[160];
    plugins_path(id, "settings.json", path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    /* 不要放栈上：httpd 任务栈只有 8KB */
    char *buf = heap_caps_malloc(CONF_MAX + 1, MALLOC_CAP_SPIRAM);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, CONF_MAX, f);
    fclose(f);
    buf[n] = 0;

    cJSON *j = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return NULL;
    }
    return j;
}

bool plugin_conf_write(const char *id, const char *json, size_t len)
{
    if (len == 0 || len > CONF_MAX) return false;

    cJSON *j = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(j)) {
        ESP_LOGW(TAG, "settings is not a json object");
        cJSON_Delete(j);
        return false;
    }
    cJSON_Delete(j);

    char path[160];
    plugins_path(id, "settings.json", path, sizeof(path));

    FILE *f = sd_open_write(path, 10);
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return false;
    }
    size_t w = fwrite(json, 1, len, f);
    fclose(f);
    ESP_LOGI(TAG, "saved %u bytes to %s", (unsigned)w, path);
    return w == len;
}
