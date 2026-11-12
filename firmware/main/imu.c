#include "imu.h"

#include <math.h>

#include "board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"

static const char *TAG = "imu";

#define REG_CTRL1 0x02
#define REG_CTRL2 0x03
#define REG_CTRL3 0x04
#define REG_CTRL5 0x06
#define REG_CTRL7 0x08
#define REG_AX_L 0x35
#define REG_WHO_AM_I 0x00

static i2c_master_dev_handle_t s_dev;
static bool s_ok;

static float s_acc_scale = 4.0f / 32768.0f;
static float s_gyro_scale = 512.0f / 32768.0f;

static volatile float s_pitch, s_roll;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, 2, 100);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

static esp_err_t try_addr(uint8_t addr)
{
    if (i2c_master_probe(io_bus(), addr, 50) != ESP_OK) return ESP_ERR_NOT_FOUND;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    if (i2c_master_bus_add_device(io_bus(), &cfg, &s_dev) != ESP_OK) return ESP_FAIL;

    uint8_t who = 0;
    if (rd(REG_WHO_AM_I, &who, 1) != ESP_OK) return ESP_FAIL;
    ESP_LOGI(TAG, "found QMI8658 at 0x%02X, WHO_AM_I=0x%02X", addr, who);
    return ESP_OK;
}

esp_err_t imu_init(void)
{
    if (try_addr(ADDR_QMI8658L) != ESP_OK && try_addr(ADDR_QMI8658H) != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 not found, gestures disabled");
        s_ok = false;
        return ESP_ERR_NOT_FOUND;
    }

    wr(REG_CTRL1, 0x40); /* 地址自增 + 小端 */

    uint8_t c2 = 0;
    rd(REG_CTRL2, &c2, 1);
    wr(REG_CTRL2, (uint8_t)((c2 & 0x0F) | (1 << 4))); /* ±4g */

    uint8_t c3 = 0;
    rd(REG_CTRL3, &c3, 1);
    wr(REG_CTRL3, (uint8_t)((c3 & 0x0F) | (5 << 4))); /* ±512dps */

    wr(REG_CTRL5, 0x11); /* 低通使能 */
    wr(REG_CTRL7, 0x43); /* acc + gyro 使能 */

    vTaskDelay(pdMS_TO_TICKS(30));
    s_ok = true;
    return ESP_OK;
}

bool imu_present(void)
{
    return s_ok;
}

bool imu_read(imu_sample_t *out)
{
    if (!s_ok || out == NULL) return false;

    uint8_t buf[12];
    io_lock();
    esp_err_t err = rd(REG_AX_L, buf, sizeof(buf));
    io_unlock();
    if (err != ESP_OK) return false;

    int16_t ax = (int16_t)(buf[0] | (buf[1] << 8));
    int16_t ay = (int16_t)(buf[2] | (buf[3] << 8));
    int16_t az = (int16_t)(buf[4] | (buf[5] << 8));
    int16_t gx = (int16_t)(buf[6] | (buf[7] << 8));
    int16_t gy = (int16_t)(buf[8] | (buf[9] << 8));
    int16_t gz = (int16_t)(buf[10] | (buf[11] << 8));

    out->ax = ax * s_acc_scale;
    out->ay = ay * s_acc_scale;
    out->az = az * s_acc_scale;
    out->gx = gx * s_gyro_scale;
    out->gy = gy * s_gyro_scale;
    out->gz = gz * s_gyro_scale;
    return true;
}

float imu_pitch_deg(void) { return s_pitch; }
float imu_roll_deg(void) { return s_roll; }

/* ------------------------------------------------------------------ *
 * 手势：50Hz 采样，去重力后看「线性加速度」
 *   shake  —— 400ms 内多次冲击
 *   tap    —— 安静之后的一次孤立冲击
 *   flip   —— 重力方向持续反向（屏幕朝下）
 * ------------------------------------------------------------------ */

static void imu_task(void *arg)
{
    void (*cb)(int) = (void (*)(int))arg;
    const int period_ms = 20;

    float gx = 0, gy = 0, gz = 1.0f;
    int shake_hits = 0;
    int64_t last_hit = 0, last_shake = -10000000, last_big = -10000000;
    int64_t down_since = 0, up_since = 0;
    bool face_down = false;
    int64_t warm_until = 0;

    vTaskDelay(pdMS_TO_TICKS(400));
    warm_until = esp_timer_get_time() + 2500000; /* 上电头 2.5s 不判手势 */

    for (;;) {
        imu_sample_t s;
        if (imu_read(&s)) {
            int64_t now = esp_timer_get_time();

            /* 重力低通（0.1 系数约 200ms 时间常数） */
            gx = gx * 0.90f + s.ax * 0.10f;
            gy = gy * 0.90f + s.ay * 0.10f;
            gz = gz * 0.90f + s.az * 0.10f;

            float dx = s.ax - gx, dy = s.ay - gy, dz = s.az - gz;
            float dm = sqrtf(dx * dx + dy * dy + dz * dz);

            /* 这块板子的 Z 轴朝下：平放时 az≈-1g，所以 roll 用 -gz 做参考 */
            s_roll = atan2f(gy, -gz) * 57.2958f;
            s_pitch = atan2f(-gx, sqrtf(gy * gy + gz * gz)) * 57.2958f;

            /* --- 冲击 --- */
            if (dm > 0.45f) {
                shake_hits++;
                last_hit = now;
            }
            if (now - last_hit > 450000) shake_hits = 0;

            bool quiet = (now - last_big) > 300000;
            bool warm = now > warm_until;

            if (warm && dm > 1.10f && quiet && shake_hits <= 2 && cb) {
                cb(IMU_EV_TAP);
                last_big = now;
                shake_hits = 0;
            } else if (warm && shake_hits >= 4 && now - last_shake > 1200000 && cb) {
                cb(IMU_EV_SHAKE);
                last_shake = now;
                last_big = now;
                shake_hits = 0;
            }

            /* --- 翻转：Z 轴朝下，az>+0.55g 才是屏幕扣在桌上 --- */
            if (!warm) {
                down_since = 0;
                up_since = 0;
            } else if (gz > 0.55f) {
                up_since = 0;
                if (down_since == 0) down_since = now;
                if (!face_down && now - down_since > 700000) {
                    face_down = true;
                    if (cb) cb(IMU_EV_FLIP_DOWN);
                }
            } else if (gz < -0.55f) {
                down_since = 0;
                if (up_since == 0) up_since = now;
                if (face_down && now - up_since > 400000) {
                    face_down = false;
                    if (cb) cb(IMU_EV_FLIP_UP);
                }
            } else {
                down_since = 0;
                up_since = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

void imu_start(void (*cb)(int ev))
{
    if (!s_ok) return;
    xTaskCreatePinnedToCore(imu_task, "imu", 4096, (void *)cb, 4, NULL, 0);
}
