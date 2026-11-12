#include "dsec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "nvs.h"
#include "plugin_conf.h"
#include "rtc.h"

static const char *TAG = "dsec";

#define BODY_MAX 2048
#define KEY_MAX 160
#define WORK_WINDOW_US (300LL * 1000000)   /* 5 分钟 */
#define SLEEP_WINDOW_US (1800LL * 1000000) /* 30 分钟 */

static char s_key[KEY_MAX];
static volatile float s_balance = -1.0f;
static volatile float s_today = -1.0f;
static volatile bool s_peak;
static char s_status[24] = "NO KEY";
static int64_t s_last_us;
static volatile bool s_force;
static int s_period_s = 300;

/* 余额最后一次变化（下降或充值）的时间，用来决定播哪段动画 */
static volatile int64_t s_last_change_us;
static volatile float s_prev_balance = -1.0f;

/* ---- 今日消耗累加值存在 NVS ---- */

static void load_day(int *date, float *acc)
{
    *date = 0;
    *acc = 0.0f;
    nvs_handle_t h;
    if (nvs_open("mochi", NVS_READONLY, &h) != ESP_OK) return;
    int32_t d = 0;
    if (nvs_get_i32(h, "dsp_date", &d) == ESP_OK) *date = d;
    size_t len = 24;
    char buf[24];
    if (nvs_get_str(h, "dsp_acc", buf, &len) == ESP_OK) *acc = (float)atof(buf);
    nvs_close(h);
}

static void save_day(int date, float acc)
{
    nvs_handle_t h;
    if (nvs_open("mochi", NVS_READWRITE, &h) != ESP_OK) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "%.4f", acc);
    nvs_set_i32(h, "dsp_date", date);
    nvs_set_str(h, "dsp_acc", buf);
    nvs_commit(h);
    nvs_close(h);
}

static void load_settings(void)
{
    s_key[0] = 0;
    cJSON *c = plugin_conf_read("pet");
    if (!c) return;
    cJSON *k = cJSON_GetObjectItem(c, "api_key");
    if (cJSON_IsString(k) && k->valuestring) {
        /* 只保留可见 ASCII，避免把空格/换行带进 header */
        size_t o = 0;
        for (const char *p = k->valuestring; *p && o < KEY_MAX - 1; p++) {
            if (*p > 32 && *p < 127) s_key[o++] = *p;
        }
        s_key[o] = 0;
    }
    cJSON *r = cJSON_GetObjectItem(c, "refresh");
    if (cJSON_IsString(r) && r->valuestring) {
        int m = atoi(r->valuestring);
        if (m >= 1 && m <= 120) s_period_s = m * 60;
    }
    cJSON_Delete(c);
}

static bool fetch_once(void)
{
    if (!s_key[0]) {
        snprintf(s_status, sizeof(s_status), "NO KEY");
        return false;
    }
    if (!net_sta_up()) {
        snprintf(s_status, sizeof(s_status), "NO WIFI");
        return false;
    }

    char *body = heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM);
    if (!body) return false;

    char auth[KEY_MAX + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", s_key);

    esp_http_client_config_t cfg = {
        .url = "https://api.deepseek.com/user/balance",
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        heap_caps_free(body);
        return false;
    }
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Accept", "application/json");

    int status = 0;
    body[0] = 0;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int n = esp_http_client_read(c, body, BODY_MAX - 1);
        body[n > 0 ? n : 0] = 0;
        status = esp_http_client_get_status_code(c);
    }
    esp_http_client_cleanup(c);

    bool ok = false;
    if (status == 200) {
        cJSON *j = cJSON_Parse(body);
        if (j) {
            cJSON *infos = cJSON_GetObjectItem(j, "balance_infos");
            cJSON *first = cJSON_IsArray(infos) ? cJSON_GetArrayItem(infos, 0) : NULL;
            cJSON *tot = first ? cJSON_GetObjectItem(first, "total_balance") : NULL;
            if (cJSON_IsString(tot) && tot->valuestring) {
                float bal = (float)atof(tot->valuestring);
                float prev = s_prev_balance; /* 先留住上一次的值，下面还要用 */

                /* 余额变了就记时间：降了就进入「工作」，充值是变化但不工作 */
                if (prev >= 0.0f && bal < prev - 0.001f) {
                    s_last_change_us = esp_timer_get_time();
                    time_t now_s = time(NULL);
                    if (now_s > 1000000000) {
                        nvs_handle_t h;
                        if (nvs_open("mochi", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_i64(h, "dchg", (int64_t)now_s);
                            nvs_commit(h);
                            nvs_close(h);
                        }
                    }
                } else if (prev >= 0.0f && bal > prev + 0.001f) {
                    s_last_change_us = esp_timer_get_time();
                }
                s_balance = bal;

                /* 记住余额，下次开机第一眼看得出涨跌 */
                nvs_handle_t hb;
                if (nvs_open("mochi", NVS_READWRITE, &hb) == ESP_OK) {
                    char b[16];
                    snprintf(b, sizeof(b), "%.2f", bal);
                    nvs_set_str(hb, "dbal", b);
                    nvs_commit(hb);
                    nvs_close(hb);
                }

                /* 今日消耗 = 当天每次余额下降的累加；充值不影响已经花掉的 */
                rtc_time_t t;
                rtc_now(&t);
                int today = t.year * 10000 + t.mon * 100 + t.day;
                int d0;
                float acc;
                load_day(&d0, &acc);
                if (d0 != today) {
                    d0 = today;
                    acc = 0.0f;
                }
                if (prev >= 0.0f && bal < prev - 0.001f) {
                    acc += prev - bal;
                    save_day(d0, acc);
                }
                s_prev_balance = bal;
                s_today = acc;
                snprintf(s_status, sizeof(s_status), "OK");
                ok = true;
                ESP_LOGI(TAG, "balance %.2f, today %.2f", bal, s_today);
            }
            cJSON_Delete(j);
        }
        if (!ok) snprintf(s_status, sizeof(s_status), "BAD JSON");
    } else if (status == 401) {
        snprintf(s_status, sizeof(s_status), "KEY INVALID");
        ESP_LOGW(TAG, "401 invalid api key");
    } else if (status == 0) {
        snprintf(s_status, sizeof(s_status), "NET FAIL");
    } else {
        snprintf(s_status, sizeof(s_status), "HTTP %d", status);
    }

    heap_caps_free(body);
    return ok;
}

static bool calc_peak(void);

static void dsec_task(void *arg)
{
    (void)arg;
    int tick = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        bool due = s_force || (s_last_us == 0) || (now - s_last_us > (int64_t)s_period_s * 1000000);
        if (due && s_key[0] && net_sta_up()) {
            s_force = false;
            s_last_us = now;
            fetch_once();
        }
        if (++tick >= 30) {
            tick = 0;
            s_peak = calc_peak(); /* 跨过 12:00 / 18:00 要自己切 */
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* 峰谷：官方定价页
 *   峰时 = UTC 01:00-04:00 与 06:00-10:00，周一至周五
 *   其余时段全部谷时（含周末、法定节假日）
 * 换算成北京时间：09:00-12:00 与 14:00-18:00，周末全天谷时。
 * 注：法定节假日没有内置日历，节假日当天会按工作日算——这点在 README 里写明。
 */
static bool calc_peak(void)
{
    rtc_time_t t;
    rtc_now(&t);
    if (t.wday == 0 || t.wday == 6) return false;
    int m = t.hour * 60 + t.min;
    return (m >= 9 * 60 && m < 12 * 60) || (m >= 14 * 60 && m < 18 * 60);
}

void dsec_init(void)
{
    load_settings();
    if (s_period_s > 300) s_period_s = 300; /* 5 分钟窗口需要 5 分钟粒度 */
#ifdef DSEC_FAST_TEST
    s_period_s = 20; /* 测试用，正式版没有这个宏 */
#endif
    s_peak = calc_peak();

    /* 上次变化时间：开机前 30 分钟内的才有意义，否则当作已经闲了很久 */
    int64_t saved = 0;
    nvs_handle_t h;
    if (nvs_open("mochi", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i64(h, "dchg", &saved);
        char b[16];
        size_t bl = sizeof(b);
        if (nvs_get_str(h, "dbal", b, &bl) == ESP_OK) s_prev_balance = (float)atof(b);
        nvs_close(h);
    }
    int64_t now_us = esp_timer_get_time();
    time_t now_s = time(NULL);
    if (saved > 1000000000 && now_s > 1000000000 && now_s >= saved &&
        now_s - saved < 1800) {
        s_last_change_us = now_us - (int64_t)(now_s - saved) * 1000000;
    } else {
        s_last_change_us = now_us - 600LL * 1000000; /* 默认「一顿饭之前」→ 吃饭 */
    }

    ESP_LOGI(TAG, "key %s, refresh %ds, %s, mood %d", s_key[0] ? "set" : "missing", s_period_s,
             s_peak ? "peak" : "off-peak", (int)dsec_mood());
    if (!s_key[0]) snprintf(s_status, sizeof(s_status), "NO KEY");
    xTaskCreate(dsec_task, "dsec", 6144, NULL, 3, NULL);
}

dsec_mood_t dsec_mood(void)
{
#ifdef DSEC_FAST_TEST
    /* 测试用：90 秒一轮，依次 吃饭(0-30) → 工作(30-60) → 睡觉(60-90) */
    int64_t cyc = (esp_timer_get_time() / 1000000) % 90;
    if (cyc >= 60) return DSEC_MOOD_SLEEP;
    if (cyc >= 30) return DSEC_MOOD_WORK;
    return DSEC_MOOD_EAT;
#else
    int64_t age = esp_timer_get_time() - s_last_change_us;
    if (age < WORK_WINDOW_US) return DSEC_MOOD_WORK;
    if (age < SLEEP_WINDOW_US) return DSEC_MOOD_EAT;
    return DSEC_MOOD_SLEEP;
#endif
}

void dsec_reload(void)
{
    load_settings();
    s_force = true;
}

bool dsec_has_key(void)
{
    return s_key[0] != 0;
}

float dsec_balance(void)
{
    return s_balance;
}

float dsec_today(void)
{
    return s_today;
}

bool dsec_peak(void)
{
    return s_peak;
}

const char *dsec_status(void)
{
    return s_status;
}

void dsec_touch(void)
{
    s_peak = calc_peak();
    if (s_last_us == 0) return;
    if (esp_timer_get_time() - s_last_us > 120000000) s_force = true;
}
