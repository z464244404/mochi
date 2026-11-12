#include "bg.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"

static const char *TAG = "bg";

#define NAME_LEN 28
#define ENTRY_LEN (NAME_LEN + 16)
#define BG_PATH "/sdcard/bg.bin"

static FILE *s_f;
static int s_count;
static char s_name[NAME_LEN + 4];
static uint16_t *s_px; /* 480*480*2 */
static jpeg_dec_handle_t s_dec;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool bg_init(void)
{
    if (s_f) {
        fclose(s_f);
        s_f = NULL;
    }
    s_f = fopen(BG_PATH, "rb");
    if (!s_f) {
        ESP_LOGW(TAG, "no %s", BG_PATH);
        return false;
    }
    uint8_t hdr[8];
    if (fread(hdr, 1, 8, s_f) != 8 || memcmp(hdr, "BG01", 4) != 0) {
        fclose(s_f);
        s_f = NULL;
        return false;
    }
    s_count = (int)rd32(hdr + 4);
    if (s_count <= 0) {
        fclose(s_f);
        s_f = NULL;
        return false;
    }
    s_name[0] = 0;
    ESP_LOGI(TAG, "%d backgrounds from SD", s_count);
    return true;
}

static bool load_one(const char *name)
{
    if (!s_f) return false;
    if (!strcmp(s_name, name) && s_px) return true;

    for (int i = 0; i < s_count; i++) {
        uint8_t e[ENTRY_LEN];
        if (fseek(s_f, 8 + (long)i * ENTRY_LEN, SEEK_SET) != 0) return false;
        if (fread(e, 1, ENTRY_LEN, s_f) != ENTRY_LEN) return false;

        char nm[NAME_LEN + 1];
        memcpy(nm, e, NAME_LEN);
        nm[NAME_LEN] = 0;
        if (strcmp(nm, name) != 0) continue;

        uint32_t off = rd32(e + 28);
        uint32_t size = rd32(e + 32);
        uint32_t w = rd32(e + 36);
        uint32_t h = rd32(e + 40);
        if (w != MOCHI_W || h != MOCHI_H || size == 0) return false;

        if (!s_px) {
            /* JPEG 解码器要求输出缓冲 16 字节对齐 */
            s_px = heap_caps_aligned_alloc(16, (size_t)MOCHI_W * MOCHI_H * 2,
                                           MALLOC_CAP_SPIRAM);
            if (!s_px) return false;
        }
        if (!s_dec) {
            jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
            cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
            if (jpeg_dec_open(&cfg, &s_dec) != JPEG_ERR_OK) {
                s_dec = NULL;
                return false;
            }
        }

        /* 整段读进临时缓冲再解码（解码器要求输入在内存里） */
        uint8_t *jpg = heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM);
        if (!jpg) return false;
        if (fseek(s_f, (long)off, SEEK_SET) != 0 || fread(jpg, 1, size, s_f) != size) {
            heap_caps_free(jpg);
            return false;
        }

        jpeg_dec_io_t io;
        jpeg_dec_header_info_t info;
        memset(&io, 0, sizeof(io));
        memset(&info, 0, sizeof(info));
        io.inbuf = jpg;
        io.inbuf_len = (int)size;
        jpeg_dec_parse_header(s_dec, &io, &info);
        io.outbuf = (uint8_t *)s_px;
        esp_err_t ok = jpeg_dec_process(s_dec, &io);
        heap_caps_free(jpg);
        if (ok != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "decode %s failed", name);
            return false;
        }

        s_name[0] = 0;
        snprintf(s_name, sizeof(s_name), "%s", name);
        ESP_LOGI(TAG, "bg %s ready", name);
        return true;
    }
    return false;
}

bool bg_draw(gfx_t *g, const char *name)
{
    if (!name || !name[0]) return false;
    if (!load_one(name)) return false;
    if (!s_px) return false;
    memcpy(g->px, s_px, (size_t)MOCHI_W * MOCHI_H * 2);
    return true;
}
