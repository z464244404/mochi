#include <stdio.h>

#include "app.h"
#include "esp_timer.h"
#include "gfx.h"
#include "net.h"

/**
 * 网络状态屏
 *
 * - 已连家庭 WiFi：显示路由器分配的 IP（局域网里用这个地址打开网页）
 * - 没连：提示连热点、给热点名和密码
 *
 * 开机时如果暂时没连上，会停在这一屏；连上并展示几秒后自动进桌宠。
 */

static int64_t s_t0;
static bool s_auto_advance;

static void net_enter(void)
{
    s_t0 = esp_timer_get_time();
    s_auto_advance = !net_sta_up();
}

static void net_draw(gfx_t *g, float dt)
{
    (void)dt;
    gfx_background(g, GFX_RGB(0x0b, 0x12, 0x18), GFX_RGB(0x12, 0x1d, 0x28),
                   GFX_RGB(0x07, 0x0a, 0x0d));

    if (net_sta_up()) {
        char url[40];
        gfx_text(g, &font_ui_29, "CONNECTED", 240, 138, GFX_ALIGN_CENTER,
                 GFX_RGB(0x5b, 0xe0, 0xbd));
        gfx_text(g, &font_num_56, net_sta_ip(), 240, 218, GFX_ALIGN_CENTER,
                 GFX_RGB(0xf4, 0xf7, 0xf8));
        snprintf(url, sizeof(url), "http://%.15s", net_sta_ip());
        gfx_text(g, &font_ui_29, url, 240, 296, GFX_ALIGN_CENTER, GFX_RGB(0x7e, 0x8a, 0x99));
        if (net_sta_ssid()[0]) {
            gfx_text(g, &font_ui_29, net_sta_ssid(), 240, 348, GFX_ALIGN_CENTER,
                     GFX_RGB(0x46, 0x50, 0x60));
        }
        if (s_auto_advance && (esp_timer_get_time() - s_t0) > 6000000) {
            s_auto_advance = false;
            apps_next();
        }
        return;
    }

    gfx_text(g, &font_ui_29, "WIFI NOT CONNECTED", 240, 146, GFX_ALIGN_CENTER,
             GFX_RGB(0xff, 0xc8, 0x6b));
    gfx_text(g, &font_ui_29, "JOIN HOTSPOT", 240, 212, GFX_ALIGN_CENTER, GFX_RGB(0x7e, 0x8a, 0x99));
    gfx_text(g, &font_ui_29, net_ap_ssid(), 240, 250, GFX_ALIGN_CENTER, GFX_RGB(0xf4, 0xf7, 0xf8));
    gfx_text(g, &font_ui_29, "PASS mochi1234", 240, 288, GFX_ALIGN_CENTER,
             GFX_RGB(0x7e, 0x8a, 0x99));
    gfx_text(g, &font_ui_29, "THEN OPEN", 240, 340, GFX_ALIGN_CENTER, GFX_RGB(0x5c, 0x66, 0x75));
    gfx_text(g, &font_ui_29, "http://192.168.4.1", 240, 374, GFX_ALIGN_CENTER,
             GFX_RGB(0x5b, 0xe0, 0xbd));

    /* 连过但失败了：把原因码显示出来，方便对号入座 */
    if (net_sta_state() == NET_STA_FAILED && net_sta_reason() != 0) {
        char err[20];
        snprintf(err, sizeof(err), "ERR %d", net_sta_reason());
        gfx_text(g, &font_ui_29, err, 240, 414, GFX_ALIGN_CENTER, GFX_RGB(0xff, 0x8a, 0x9a));
    }
}

const mochi_app_t app_net = {
    .id = "net",
    .name = "网络",
    .enter = net_enter,
    .leave = NULL,
    .draw = net_draw,
    .event = NULL,
};
