#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * 网络：永远开一个 AP（Mochi-XXXX），有保存的家庭 WiFi 就同时连 STA。
 * STA 拿到 IP 后启动 SNTP，把时间写回 PCF85063。
 */

esp_err_t net_init(void);

const char *net_ap_ssid(void);
const char *net_ap_ip(void);
bool net_sta_up(void);

/* STA 连上后路由器分配的地址；未连接时返回空串 */
const char *net_sta_ip(void);
const char *net_sta_ssid(void);
bool net_have_creds(void);

typedef enum {
    NET_STA_IDLE = 0,   /* 没配过 WiFi */
    NET_STA_CONNECTING, /* 正在连 */
    NET_STA_CONNECTED,  /* 已连上，有 IP */
    NET_STA_FAILED,     /* 连失败了 */
} net_sta_state_t;

net_sta_state_t net_sta_state(void);
int net_sta_reason(void); /* 断线原因码，0 表示正常 */
int net_sta_rssi(void);

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t auth;
} net_ap_t;

/* 阻塞扫描，2~4 秒；返回扫到的数量 */
int net_scan(net_ap_t *out, int max);

/* 网页里保存 WiFi：写入 NVS 并立刻尝试连接 */
void net_set_sta(const char *ssid, const char *pass);
