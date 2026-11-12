#include "net.h"

#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rtc.h"

static const char *TAG = "net";

#define AP_PASS "mochi1234"
#define NVS_NS "mochi"

static char s_ap_ssid[32];
static char s_ip[16] = "192.168.4.1";
static char s_sta_ip[16];
static char s_sta_ssid[33];
static bool s_have_creds;
static volatile bool s_sta_up;
static volatile net_sta_state_t s_state = NET_STA_IDLE;
static volatile int s_reason;
static volatile int s_rssi;
static TaskHandle_t s_retry_task;
static int s_tz_min = 8 * 60; /* 时区偏移（分钟），默认东八区 */

static void sntp_task(void *arg)
{
    (void)arg;
    /* 等 STA 真的通了再开 SNTP */
    while (!s_sta_up) vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "starting SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "ntp.aliyun.com");
    esp_sntp_init();

    for (int i = 0; i < 60; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            time_t now = 0;
            struct tm tm;
            time(&now);
            /* SNTP 给的是 UTC；RTC 里存本地时间，默认东八区 */
            now += (time_t)s_tz_min * 60;
            gmtime_r(&now, &tm);
            rtc_time_t t = {
                .year = tm.tm_year + 1900,
                .mon = tm.tm_mon + 1,
                .day = tm.tm_mday,
                .hour = tm.tm_hour,
                .min = tm.tm_min,
                .sec = tm.tm_sec,
            };
            rtc_set(&t);
            ESP_LOGI(TAG, "SNTP synced: %04d-%02d-%02d %02d:%02d:%02d", t.year, t.mon, t.day,
                     t.hour, t.min, t.sec);
            vTaskDelete(NULL);
            return;
        }
    }
    ESP_LOGW(TAG, "SNTP timeout");
    vTaskDelete(NULL);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        /* 没配过 WiFi 就只让 STA 接口挂着，扫描要用它 */
        if (s_have_creds) {
            s_state = NET_STA_CONNECTING;
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        s_state = NET_STA_CONNECTING;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        s_sta_up = false;
        s_sta_ip[0] = 0;
        s_rssi = 0;
        s_reason = e ? e->reason : -1;
        s_state = NET_STA_FAILED;
        ESP_LOGW(TAG, "STA disconnected, reason=%d", s_reason);
        /* 不要在事件回调里 delay：会堵住整个 wifi 事件任务 */
        if (s_retry_task) xTaskNotifyGive(s_retry_task);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_rssi = ap.rssi;
        s_reason = 0;
        s_sta_up = true;
        s_state = NET_STA_CONNECTED;
        ESP_LOGI(TAG, "STA got ip %s -> http://%s (rssi %d)", s_sta_ip, s_sta_ip, s_rssi);
    }
}

/* 断线后隔 5 秒重连，独立任务，不阻塞事件回调 */
static void retry_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!s_sta_up && s_have_creds) {
            ESP_LOGI(TAG, "reconnect...");
            s_state = NET_STA_CONNECTING;
            esp_wifi_connect();
        }
    }
}

static void load_sta(char *ssid, size_t slen, char *pass, size_t plen)
{
    ssid[0] = pass[0] = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = slen;
    nvs_get_str(h, "ssid", ssid, &n);
    n = plen;
    nvs_get_str(h, "pass", pass, &n);
    nvs_close(h);
}

static void save_sta(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
}

void net_set_sta(const char *ssid, const char *pass)
{
    save_sta(ssid, pass);
    wifi_config_t cfg = {0};
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", pass);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_disconnect();
    esp_wifi_connect();
}

const char *net_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *net_ap_ip(void)
{
    return s_ip;
}

bool net_sta_up(void)
{
    return s_sta_up;
}

const char *net_sta_ip(void)
{
    return s_sta_ip;
}

const char *net_sta_ssid(void)
{
    return s_sta_ssid;
}

bool net_have_creds(void)
{
    return s_have_creds;
}

net_sta_state_t net_sta_state(void)
{
    return s_state;
}

int net_sta_reason(void)
{
    return s_reason;
}

int net_sta_rssi(void)
{
    return s_rssi;
}

int net_scan(net_ap_t *out, int max)
{
    wifi_scan_config_t cfg = {.show_hidden = false};
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) {
        ESP_LOGW(TAG, "scan start failed");
        return 0;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) return 0;

    wifi_ap_record_t *recs = heap_caps_malloc(num * sizeof(*recs), MALLOC_CAP_SPIRAM);
    if (!recs) return 0;
    uint16_t got = num;
    if (esp_wifi_scan_get_ap_records(&got, recs) != ESP_OK) {
        free(recs);
        return 0;
    }

    int k = 0;
    for (int i = 0; i < got && k < max; i++) {
        if (recs[i].ssid[0] == 0) continue; /* 隐藏网络跳过 */
        char name[33];
        snprintf(name, sizeof(name), "%.32s", (char *)recs[i].ssid);

        /* 同名 AP（mesh）只留信号最好的 */
        int dup = -1;
        for (int j = 0; j < k; j++) {
            if (!strcmp(out[j].ssid, name)) {
                dup = j;
                break;
            }
        }
        if (dup >= 0) {
            if (recs[i].rssi > out[dup].rssi) out[dup].rssi = recs[i].rssi;
            continue;
        }
        snprintf(out[k].ssid, sizeof(out[k].ssid), "%s", name);
        out[k].rssi = recs[i].rssi;
        out[k].auth = recs[i].authmode;
        k++;
    }
    free(recs);

    /* 信号强的排前面 */
    for (int i = 1; i < k; i++) {
        net_ap_t t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = t;
    }
    ESP_LOGI(TAG, "scan found %d networks", k);
    return k;
}

esp_err_t net_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_wifi_event, NULL, NULL));

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Mochi-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", s_ap_ssid);
    ap.ap.ssid_len = strlen(s_ap_ssid);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", AP_PASS);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    char ssid[33], pass[65];
    load_sta(ssid, sizeof(ssid), pass, sizeof(pass));
    bool have_sta = ssid[0] != 0;
    if (have_sta) {
        snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%.32s", ssid);
        s_have_creds = true;
    }

    wifi_config_t sta = {0};
    if (have_sta) {
        snprintf((char *)sta.sta.ssid, sizeof(sta.sta.ssid), "%s", ssid);
        snprintf((char *)sta.sta.password, sizeof(sta.sta.password), "%s", pass);
    }

    /* 一直用 APSTA：没配 WiFi 时 STA 接口闲着，但扫描要用它 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (have_sta) ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (have_sta) s_state = NET_STA_CONNECTING;

    /* APSTA 同开时省电模式容易导致偶发掉线 */
    esp_wifi_set_ps(WIFI_PS_NONE);

    xTaskCreate(retry_task, "wifi_retry", 3072, NULL, 3, &s_retry_task);

    ESP_LOGI(TAG, "AP '%s' pass '%s' -> http://%s", s_ap_ssid, AP_PASS, s_ip);
    if (have_sta) ESP_LOGI(TAG, "STA trying '%s'", ssid);

    xTaskCreate(sntp_task, "sntp", 4096, NULL, 3, NULL);
    return ESP_OK;
}
