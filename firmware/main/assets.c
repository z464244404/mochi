#include "assets.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "sd.h"

static const char *TAG = "assets";

#define MAX_ENTRIES 8
#define NAME_LEN 40
#define HDR_LEN 12
#define ENT_LEN (4 + 4 + NAME_LEN)
#define CHUNK (64 * 1024)

typedef struct {
    uint32_t off;
    uint32_t size;
    char name[NAME_LEN];
} ent_t;

static bool s_have_pack;

static const esp_partition_t *part(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "assets");
}

static bool read_entries(ent_t *out, int *count)
{
    const esp_partition_t *p = part();
    if (!p) return false;

    uint8_t hdr[HDR_LEN];
    if (esp_partition_read(p, 0, hdr, sizeof(hdr)) != ESP_OK) return false;
    if (memcmp(hdr, "AS1", 3) != 0) return false;

    int n = (int)(hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | ((uint32_t)hdr[7] << 24));
    if (n <= 0 || n > MAX_ENTRIES) return false;

    for (int i = 0; i < n; i++) {
        uint8_t e[ENT_LEN];
        if (esp_partition_read(p, HDR_LEN + (size_t)i * ENT_LEN, e, ENT_LEN) != ESP_OK) return false;
        out[i].off = (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) |
                     ((uint32_t)e[3] << 24);
        out[i].size = (uint32_t)e[4] | ((uint32_t)e[5] << 8) | ((uint32_t)e[6] << 16) |
                      ((uint32_t)e[7] << 24);
        memcpy(out[i].name, e + 8, NAME_LEN);
        out[i].name[NAME_LEN - 1] = 0;
        if (out[i].off + out[i].size > p->size || out[i].size == 0) return false;
    }
    *count = n;
    return true;
}

bool assets_has(const char *name)
{
    ent_t ents[MAX_ENTRIES];
    int n = 0;
    if (!s_have_pack && !read_entries(ents, &n)) return false;
    for (int i = 0; i < n; i++) {
        if (!strcmp(ents[i].name, name)) return true;
    }
    return false;
}

esp_err_t assets_sync(void)
{
    ent_t ents[MAX_ENTRIES];
    int n = 0;

    const esp_partition_t *p = part();
    if (!p) return ESP_ERR_NOT_FOUND;
    if (!read_entries(ents, &n)) {
        ESP_LOGI(TAG, "no asset pack in flash");
        return ESP_ERR_INVALID_STATE;
    }
    s_have_pack = true;

    uint8_t *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    if (!buf) return ESP_ERR_NO_MEM;

    int copied = 0;
    int failed = 0;
    uint32_t pack_end = 0;
    for (int i = 0; i < n; i++) {
        char path[96];
        snprintf(path, sizeof(path), "/sdcard/%s", ents[i].name);

        struct stat st;
        memset(&st, 0, sizeof(st));
        if (stat(path, &st) == 0 && (uint32_t)st.st_size == ents[i].size) continue;

        ESP_LOGW(TAG, "%s: writing %u KB", ents[i].name, (unsigned)(ents[i].size / 1024));
        FILE *f = sd_open_write(path, 10);
        if (!f) {
            ESP_LOGE(TAG, "open %s failed", path);
            continue;
        }
        uint32_t done = 0;
        bool ok = true;
        while (done < ents[i].size) {
            size_t chunk = ents[i].size - done;
            if (chunk > CHUNK) chunk = CHUNK;
            if (esp_partition_read(p, ents[i].off + done, buf, chunk) != ESP_OK ||
                fwrite(buf, 1, chunk, f) != chunk) {
                ok = false;
                break;
            }
            done += chunk;
        }
        fclose(f);
        if (ok) {
            copied++;
            ESP_LOGI(TAG, "%s done", ents[i].name);
        } else {
            failed++;
            ESP_LOGE(TAG, "%s failed at %u", ents[i].name, (unsigned)done);
            remove(path);
        }
        if (ents[i].off + ents[i].size > pack_end) pack_end = ents[i].off + ents[i].size;
    }
    heap_caps_free(buf);

    ESP_LOGI(TAG, "asset sync: %d/%d file(s) updated", copied, n);

    /* 全部同步好了就把分区擦掉，不给闪存留着这份拷贝 */
    if (failed == 0 && pack_end > 0) {
        uint32_t len = (pack_end + 0xFFFFu) & ~0xFFFFu;
        if (len > p->size) len = p->size;
        ESP_LOGI(TAG, "erasing pack (%u KB) to free flash", (unsigned)(len / 1024));
        if (esp_partition_erase_range(p, 0, len) != ESP_OK) {
            ESP_LOGW(TAG, "erase failed");
        }
    }
    return ESP_OK;
}
