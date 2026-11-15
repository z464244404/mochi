#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * 音乐播放状态：只放内存，不写 SD 卡。
 *
 * 之前每推一次就写一次 data.json，等于每 10 秒磨损一次卡；
 * 而这块卡的写入本来就不可靠。改成纯内存后，电脑端可以每 2 秒推一次，
 * 进度条也就准了。
 */

typedef struct {
    char title[128];
    char artist[96];
    float pos;   /* 已播放秒数 */
    float dur;   /* 总时长秒数，0 = 未知 */
    int playing; /* 1 播放 / 0 暂停 */
    int64_t ts;  /* 推送时的 unix 秒（电脑的时钟） */
    bool valid;
} music_state_t;

void music_set(const music_state_t *s);
const music_state_t *music_get(void);
