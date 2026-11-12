#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    float ax, ay, az;
    float gx, gy, gz;
} imu_sample_t;

enum {
    IMU_EV_SHAKE = 1,
    IMU_EV_TAP = 2,
    IMU_EV_FLIP_DOWN = 3,
    IMU_EV_FLIP_UP = 4,
    IMU_EV_DIZZY = 5, /* 留给「双击」之类的手势 */
};

esp_err_t imu_init(void);
bool imu_present(void);
bool imu_read(imu_sample_t *out);

/* 启动 50Hz 手势任务；回调在 IMU 任务里执行，只做投递，不要在里面画图 */
void imu_start(void (*cb)(int ev));

float imu_pitch_deg(void);
float imu_roll_deg(void);
