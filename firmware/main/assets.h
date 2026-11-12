#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * 素材包同步：把 assets 分区里的打包文件写到 TF 卡。
 *
 * 电脑没法直接写卡，所以素材走「烧到 assets 分区 → 设备开机自己拷到卡上」这条路。
 * 只拷大小不一致的文件，重复烧录不会反复写卡。
 */
esp_err_t assets_sync(void);

/* 包里有没有这个名字（给上层判断要不要提示刷素材） */
bool assets_has(const char *name);
