#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * 电脑状态：只放内存，不写 SD。
 * 由电脑上的 tools/pc_monitor.py 每秒推一次。
 */

typedef struct {
    char host[48];
    float cpu;      /* % */
    float mem;      /* % */
    float up;       /* 上行 MB/s */
    float down;     /* 下行 MB/s */
    float vol;      /* 系统音量 0~100 */
    int mute;       /* 1 = 静音 */
    int64_t up_sec; /* 开机时长（秒） */
    bool valid;
} pc_state_t;

void pc_set(const pc_state_t *s);
const pc_state_t *pc_get(void);
