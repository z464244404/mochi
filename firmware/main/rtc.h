#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int year, mon, day;
    int hour, min, sec;
    int wday; /* 0=周日 */
} rtc_time_t;

esp_err_t rtc_clock_init(void);
bool rtc_present(void);

/* 读当前时间：优先 PCF85063，芯片不在就用开机时刻 + 运行时长 */
void rtc_now(rtc_time_t *out);

/* 写 RTC（以后 NTP / 网页对时用） */
void rtc_set(const rtc_time_t *t);

/* 星期几（0=周日），Sakamoto 算法 */
int rtc_weekday(int y, int m, int d);
