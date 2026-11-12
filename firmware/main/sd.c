#include "sd.h"

#include "board.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

static sdmmc_card_t *s_card;
static bool s_ok;

/**
 * 这块板子的 TF 卡：CLK=GPIO2 / CMD=GPIO1 / D0=GPIO42，D3(CS) 接在 EXIO4。
 * 1-bit SD 模式不需要 CS，但 D3 必须为高，卡才不会在上电时进 SPI 模式。
 * GPIO1/2 和屏幕初始化用的 SPI 共用，所以必须先 lcd_init() 再挂卡。
 */
esp_err_t sd_mount(void)
{
    io_exio_set(EXIO_SD_D3, 1);

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED; /* 40MHz */

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GPIO_NUM_2;
    slot.cmd = GPIO_NUM_1;
    slot.d0 = GPIO_NUM_42;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mcfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        s_ok = false;
        return err;
    }

    s_ok = true;
    ESP_LOGI(TAG, "mounted: %s %llu MB, %d-bit", s_card->cid.name,
             ((unsigned long long)s_card->csd.capacity * s_card->csd.sector_size) / (1024 * 1024),
             s_card->log_bus_width);
    return ESP_OK;
}

bool sd_ready(void)
{
    return s_ok;
}

FILE *sd_open_write(const char *path, int tries)
{
    for (int i = 0; i < tries; i++) {
        FILE *f = fopen(path, "wb");
        if (f) {
            if (i) ESP_LOGI(TAG, "open for write ok on try %d", i + 1);
            return f;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGE(TAG, "open for write failed: %s", path);
    return NULL;
}

/* 临时：测顺序读速度，决定「整段缓存」能不能改成「边读边解」 */
void sd_bench(void)
{
    FILE *f = fopen("/sdcard/pet.mjp", "rb");
    if (!f) return;
    const size_t CH = 128 * 1024;
    uint8_t *buf = heap_caps_malloc(CH, MALLOC_CAP_SPIRAM);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t total = 0, n;
    int64_t t0 = esp_timer_get_time();
    while ((n = fread(buf, 1, CH, f)) > 0) total += n;
    int64_t t1 = esp_timer_get_time();
    fclose(f);
    free(buf);
    ESP_LOGI(TAG, "read bench: %u KB in %.1f ms = %.2f MB/s", (unsigned)(total / 1024),
             (t1 - t0) / 1000.0, (float)total / (float)(t1 - t0));
}
