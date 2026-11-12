#include "rtc.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "io.h"

static const char *TAG = "rtc";

#define REG_CTRL1 0x00
#define REG_SEC 0x04

static i2c_master_dev_handle_t s_dev;
static bool s_ok;

/* 没有 RTC 芯片时，用「编译时刻 + 运行时长」兜底，至少时钟是走的 */
static rtc_time_t s_base;
static int64_t s_base_secs; /* 自 2000-01-01 起的秒数 */
static int64_t s_base_us;

/* Howard Hinnant 的日期算法，避免自己写闰年 */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mm = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (mm <= 2));
    *m = (int)mm;
    *d = (int)dd;
}

int rtc_weekday(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static inline int bcd2int(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

static inline uint8_t int2bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static bool read_time(rtc_time_t *t)
{
    uint8_t reg = REG_SEC;
    uint8_t b[7];
    io_lock();
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, b, sizeof(b), 100);
    io_unlock();
    if (err != ESP_OK) return false;

    t->sec = bcd2int(b[0] & 0x7F);
    t->min = bcd2int(b[1] & 0x7F);
    t->hour = bcd2int(b[2] & 0x3F);
    t->day = bcd2int(b[3] & 0x3F);
    t->wday = b[4] & 0x07;
    t->mon = bcd2int(b[5] & 0x1F);
    t->year = 2000 + bcd2int(b[6]);

    if (t->mon < 1 || t->mon > 12 || t->day < 1 || t->day > 31) return false;
    if (t->hour > 23 || t->min > 59 || t->sec > 59) return false;
    t->wday = rtc_weekday(t->year, t->mon, t->day);
    return true;
}

void rtc_set(const rtc_time_t *t)
{
    if (!s_ok) return;
    uint8_t b[8];
    b[0] = REG_SEC;
    b[1] = int2bcd(t->sec);
    b[2] = int2bcd(t->min);
    b[3] = int2bcd(t->hour);
    b[4] = int2bcd(t->day);
    b[5] = (uint8_t)rtc_weekday(t->year, t->mon, t->day);
    b[6] = int2bcd(t->mon);
    b[7] = int2bcd(t->year % 100);
    io_lock();
    i2c_master_transmit(s_dev, b, sizeof(b), 200);
    io_unlock();
}

esp_err_t rtc_clock_init(void)
{
    if (i2c_master_probe(io_bus(), ADDR_PCF85063, 50) != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063 not found, using compile-time clock");
        s_ok = false;
    } else {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ADDR_PCF85063,
            .scl_speed_hz = I2C_SPEED_HZ,
        };
        if (i2c_master_bus_add_device(io_bus(), &cfg, &s_dev) == ESP_OK) {
            uint8_t ctrl[2] = {REG_CTRL1, 0x00}; /* 清 STOP，24 小时制 */
            io_lock();
            i2c_master_transmit(s_dev, ctrl, 2, 100);
            io_unlock();
            s_ok = true;
        }
    }

    /* 编译时刻兜底 */
    static const char *mon_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char mon[4] = {0};
    int day = 1, year = 2026;
    if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) == 3) {
        s_base.mon = 1;
        for (int i = 0; i < 12; i++) {
            if (!strncmp(mon, mon_names[i], 3)) s_base.mon = i + 1;
        }
    }
    s_base.day = day;
    s_base.year = year;
    s_base.hour = s_base.min = s_base.sec = 0;
    sscanf(__TIME__, "%d:%d:%d", &s_base.hour, &s_base.min, &s_base.sec);
    s_base.wday = rtc_weekday(s_base.year, s_base.mon, s_base.day);
    s_base_secs = days_from_civil(s_base.year, s_base.mon, s_base.day) * 86400 +
                  s_base.hour * 3600 + s_base.min * 60 + s_base.sec;
    s_base_us = esp_timer_get_time();

    if (s_ok) {
        rtc_time_t t;
        if (!read_time(&t) || t.year < 2025) {
            ESP_LOGW(TAG, "RTC time invalid, seeding from build time");
            rtc_set(&s_base);
        } else {
            ESP_LOGI(TAG, "RTC %04d-%02d-%02d %02d:%02d:%02d", t.year, t.mon, t.day, t.hour,
                     t.min, t.sec);
        }
    }
    return s_ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool rtc_present(void)
{
    return s_ok;
}

void rtc_now(rtc_time_t *out)
{
    if (s_ok && read_time(out)) return;

    int64_t secs = s_base_secs + (esp_timer_get_time() - s_base_us) / 1000000;
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    civil_from_days(days, &out->year, &out->mon, &out->day);
    out->hour = (int)(rem / 3600);
    out->min = (int)((rem % 3600) / 60);
    out->sec = (int)(rem % 60);
    out->wday = rtc_weekday(out->year, out->mon, out->day);
}
