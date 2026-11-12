#pragma once

#include <stdbool.h>

/**
 * DeepSeek 账户状态
 *
 * 余额：官方 API `GET https://api.deepseek.com/user/balance`
 *       Authorization: Bearer <API_KEY>   （不需要账号登录）
 *
 * 今日消耗：官方没有用量接口，这里用「当天首次读到的余额 - 当前余额」估算，
 *           精度取决于设备当天第一次联网的时间。
 *
 * 峰谷：DeepSeek 优惠时段按北京时间 00:30–08:30（对应 UTC 16:30–00:30），
 *       本地算，不花请求。
 */

void dsec_init(void);   /* 读设置、起刷新任务 */
void dsec_reload(void); /* 设置改了就调一下 */

bool dsec_has_key(void);
float dsec_balance(void); /* 人民币，-1 表示还没取到 */
float dsec_today(void);   /* 今日消耗，-1 表示还不知道 */
bool dsec_peak(void);     /* true=峰时，false=谷时 */
const char *dsec_status(void);

/* 桌宠状态：余额在掉=工作，5 分钟没变=吃饭，30 分钟没变=睡觉 */
typedef enum {
    DSEC_MOOD_EAT = 0,
    DSEC_MOOD_WORK,
    DSEC_MOOD_SLEEP,
} dsec_mood_t;

dsec_mood_t dsec_mood(void);

void dsec_touch(void);    /* 进入应用时调用：数据太旧就立刻刷一次 */
