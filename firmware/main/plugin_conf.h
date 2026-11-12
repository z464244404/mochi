#pragma once

#include <stdbool.h>

#include "cJSON.h"

/**
 * 每个插件的设置，存在 /sdcard/plugins/<id>/settings.json
 * 格式就是一个扁平 JSON 对象：{"lat":"22.54","city":"Shenzhen"}
 *
 * 没写过的 key 不在文件里，插件用自己代码里的默认值。
 */

cJSON *plugin_conf_read(const char *id); /* 调用方 cJSON_Delete；失败返回 NULL */
bool plugin_conf_write(const char *id, const char *json, size_t len);
