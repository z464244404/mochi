#include <string.h>

#include "app.h"

/**
 * 内置插件注册表
 *
 * 桌宠和网络是 C 实现（要跑视频解码 / 要稳定的启动顺序），
 * 但它们对外和卡上的 Lua 插件一样：有 manifest、有设置页、能启用停用。
 */

static const char NET_MANIFEST[] =
    "{"
    "\"id\":\"net\",\"name\":\"网络\",\"version\":\"1.0.0\",\"api\":1,\"builtin\":true,"
    "\"settings\":[]"
    "}";

static const char PET_MANIFEST[] =
    "{"
    "\"id\":\"pet\",\"name\":\"桌宠\",\"version\":\"1.2.0\",\"api\":1,\"builtin\":true,"
    "\"settings\":["
    "{\"key\":\"api_key\",\"label\":\"DeepSeek API Key\",\"type\":\"text\",\"default\":\"\"}"
    "]"
    "}";

const mochi_builtin_t mochi_builtins[] = {
    {.id = "net", .name = "网络", .manifest = NET_MANIFEST, .app = &app_net},
    {.id = "pet", .name = "桌宠", .manifest = PET_MANIFEST, .app = &app_pet},
};

const int mochi_builtin_count = (int)(sizeof(mochi_builtins) / sizeof(mochi_builtins[0]));

const mochi_builtin_t *mochi_builtin_find(const char *id)
{
    for (int i = 0; i < mochi_builtin_count; i++) {
        if (!strcmp(mochi_builtins[i].id, id)) return &mochi_builtins[i];
    }
    return NULL;
}
