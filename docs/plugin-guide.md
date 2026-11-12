# 插件开发指南

插件就是 TF 卡上的一个目录，里面放两个文件。设备摇到这一屏时，固件会起一个 Lua 状态机执行它，
每帧调用你的 `on_draw()`，你只能通过 `ui.*` 画东西。

- [1. 最小插件](#1-最小插件)
- [2. manifest.json](#2-manifestjson)
- [3. 设置项](#3-设置项)
- [4. 生命周期](#4-生命周期)
- [5. Lua API 完整参考](#5-lua-api-完整参考)
- [6. 坐标、颜色、字体](#6-坐标颜色字体)
- [7. 运行环境与限制](#7-运行环境与限制)
- [8. 调试](#8-调试)
- [9. 完整示例：时钟](#9-完整示例时钟)
- [10. 安装与分发](#10-安装与分发)

---

## 1. 最小插件

```
/sdcard/plugins/hello/
├── manifest.json
└── main.lua
```

`manifest.json`：

```json
{
  "id": "hello",
  "name": "你好",
  "version": "1.0.0",
  "api": 1,
  "author": "you"
}
```

`main.lua`：

```lua
local t = 0

function on_start()
  log("hello 启动")
end

function on_draw(dt)
  t = t + dt
  ui.clear(0x101820)
  ui.text("HELLO", 240, 220, 106, 0xf4f7f8)
  ui.text(string.format("%.1fs", t), 240, 320, 29, 0x5be0bd)
end
```

装到卡上（网页上传或直接拷目录）→ 摇到这一屏即可。

## 2. manifest.json

| 字段 | 必填 | 说明 |
|---|---|---|
| `id` | ✓ | 目录名，只用 `a-z 0-9 . _ -`，最长 31 |
| `name` | ✓ | 网页上显示的名字（可中文） |
| `version` | | 字符串，内置插件靠它判断要不要更新 |
| `api` | | 目前固定 `1` |
| `author` | | 显示在详情页 |
| `settings` | | 设置项数组，见下 |

## 3. 设置项

manifest 里声明，网页会**自动生成表单**，保存后写到 `/sdcard/plugins/<id>/settings.json`，
插件里用 `app.get("key")` 读。

```json
"settings": [
  {"key": "city",  "label": "地名",     "type": "text",   "default": "深圳"},
  {"key": "lat",   "label": "纬度",     "type": "number", "default": "22.54"},
  {"key": "unit",  "label": "单位",     "type": "select", "options": ["c", "f"], "default": "c"},
  {"key": "bg",    "label": "背景色",   "type": "color",  "default": "#0F1115"}
]
```

| type | 控件 | `app.get()` 拿到 |
|---|---|---|
| `text` | 单行输入框 | string |
| `number` | 数字输入框 | string（**自己 tonumber**） |
| `select` | 下拉框 | string（选项之一） |
| `color` | 颜色选择器 | `"#rrggbb"` |

注意：

- 用户没保存过、或者保存成空串时，`app.get()` 可能返回 `nil` 或 `""`。
  Lua 里 `""` 是真值，`v or "默认"` **兜不住空串**，要写成：
  ```lua
  local function text_or(v, def) if v == nil or v == "" then return def end return v end
  ```
- 颜色要自己解析：`tonumber((s:gsub("#", "")), 16)`（括号不能省，否则 `gsub` 的第二个返回值会被当成进制）。

## 4. 生命周期

| 函数 | 何时调用 |
|---|---|
| `on_start()` | 进入这一屏（每次进入都会重建 Lua 状态，**全局变量会丢**） |
| `on_draw(dt)` | 每帧，`dt` 是距上一帧的秒数（float） |

- 没有 `on_stop`；要持久化就写 `store`。
- 每帧都会重画整屏，没有「脏矩形」概念：先 `ui.clear` 或 `ui.bg`，再画内容。
- 帧率上限是面板的 43Hz，但一帧预算只有 **约 23ms**（整屏填充/拷贝就要 15–18ms），别画太重的东西。

## 5. Lua API 完整参考

### ui.*

| 函数 | 说明 |
|---|---|
| `ui.clear(color)` | 整屏填充 |
| `ui.bg(name)` | 画内置天气背景（480×480），成功返回 `true` |
| `ui.rect(x, y, w, h, color)` | 矩形，**x/y 是左上角** |
| `ui.round_rect(x, y, w, h, r, color)` | 圆角矩形，x/y 左上角，r 圆角半径 |
| `ui.circle(cx, cy, r, color)` | 实心圆 |
| `ui.ring(cx, cy, r, thick, color)` | 圆环 |
| `ui.arc(cx, cy, r, thick, a0, a1, color)` | 圆弧，**角度制，0° = 正右，顺时针** |
| `ui.line(x0, y0, x1, y1, thick, color)` | 线段（带圆形端头） |
| `ui.text(s, x, y, size, color)` | 文字，**x/y 是文字中心** |
| `ui.measure(s, size)` | 返回这段文字的像素宽度，用来排版 |
| `ui.icon(name, size, cx, cy)` | 画内置天气图标，`size` 建议 120 或 64 |

颜色是 `0xRRGGBB`；要半透明就写 `0xAARRGGBB`（比如 `0x38FFFFFF` 是 22% 白）。

### time.*

| 函数 | 返回 |
|---|---|
| `time.now()` | `{year, mon, day, hour, min, sec, wday}`，`wday` 0=周日 |
| `time.epoch()` | unix 秒（NTP 同步后才有意义） |

### app.*

| 函数 | 说明 |
|---|---|
| `app.get(key)` | 读本插件的设置；没有则 `nil` |

### http.*

```lua
http.get(url, function(status, body) ... end)
```

- **异步**：立刻返回，结果到了才回调（回调一定在绘制线程里执行，可以安全调用 `ui.*`）
- `status` 是 HTTP 状态码，0 表示连不上（DNS/超时）
- `body` 是字符串，超过 8KB 会被截断
- 同一个 URL 2 分钟内的重复请求会**直接命中缓存**，不会真的发出去
- 只支持 GET，需要 POST 的接口自己再封一层（用 `app` 转发或换服务）

### json.*

| 函数 | 说明 |
|---|---|
| `json.decode(s)` | JSON → 表；失败返回 `nil`；数组从 1 开始 |

### net.*

| 函数 | 说明 |
|---|---|
| `net.up()` | WiFi 是否已连上外网（有 IP） |

**发请求前一定先判断**，否则开机瞬间 DNS 会失败：

```lua
if not net.up() then next_try = t + 3; return end
```

### store.*

| 函数 | 说明 |
|---|---|
| `store.get(key)` | 读本插件自己的小数据（存在 `/sdcard/plugins/<id>/data.json`） |
| `store.set(key, value)` | 写（只支持字符串，立刻落盘） |

用来缓存接口结果，避免每次进屏都重新请求。切屏会重建 Lua 状态，但 `store` 在卡上，不会丢。

### log

```lua
log("任意字符串")
```

打到串口（`idf.py monitor` 能看到 `lua: xxx`）。

## 6. 坐标、颜色、字体

- 屏幕 **480×480**，圆心 (240, 240)，可视圆形直径 480
- 靠近四角的地方会被圆边裁掉，放内容前算一下：`y` 处可用半宽 ≈ `sqrt(240² - (y-240)²)`
- 字号只有四档：

| size | 字体 | 内容 |
|---|---|---|
| `>= 60` | 106px | **只有数字、`.`、`:`** |
| `45–59` | 56px | 数字、`.`、`:` |
| `25–44` | 29px | ASCII + 内置中文字表 |
| `<= 24` | 22px | 数字、`℃ ℉ / - . :` |

中文字表是编译进固件的白名单（约 200 字）：常用时间词、天气词、20 个城市名。
**不在表里的字会被跳过**，显示成空白——要加字只能改固件 `tools/gen_font.py` 重新生成字体。

## 7. 运行环境与限制

| 项 | 值 |
|---|---|
| Lua 版本 | 5.4.7，**32 位整数 / 单精度浮点**（`LUA_32BITS`） |
| 堆 | PSRAM 里，**硬上限 192KB**，超了抛 `not enough memory` |
| 指令预算 | 单帧 120,000 条指令，跑飞会被 pcall 捕获并显示错误屏 |
| 开放库 | `base / string / table / math / utf8 / coroutine` |
| **没有** | `io`、`os`、`package`、`debug`，不能读写文件、不能开线程 |
| 网络 | 只有 `http.get`（HTTPS 走内置证书链） |

## 8. 调试

1. 串口看日志：`idf.py -p COM10 monitor`
   ```
   lua: loading /sdcard/plugins/hello/main.lua (312 bytes)
   lua: plugin ready, heap 18/192 KB
   lua: hello 启动
   ```
2. 语法/运行时错误会直接显示在屏幕上（红字 `PLUGIN ERROR` + 错误信息），同时串口有 `load failed: ...`
3. 想看变量就 `log(tostring(x))`——Lua 里 `log(1)` 会报错，**必须传字符串**
4. 改完插件不用重启：切到别的屏幕再切回来，会重新加载

## 9. 完整示例：时钟

这是固件内置时钟的简化版，覆盖了设置读取、时间格式化、排版测量：

```lua
local style = "cute"

function on_start()
  style = app.get("style") or "cute"
end

function on_draw(dt)
  local now = time.now()
  local hh = string.format("%02d:%02d", now.hour, now.min)

  if style == "tech" then
    ui.clear(0x05090E)
    for i = 0, 59 do
      local a = math.rad(i * 6 - 90)
      local major = (i % 5 == 0)
      ui.line(240 + math.cos(a) * 192, 240 + math.sin(a) * 192,
              240 + math.cos(a) * 206, 240 + math.sin(a) * 206,
              major and 3 or 2, major and 0x2E6B7E or 0x16323C)
    end
    ui.arc(240, 240, 214, 3, -90, -90 + now.sec * 6, 0x35E0C0)
    ui.text(hh, 240, 236, 106, 0xE6FBFF)
    ui.text(string.format("%04d.%02d.%02d", now.year, now.mon, now.day), 240, 342, 29, 0x3E7A8C)
  else
    ui.clear(0xFFF6EC)
    ui.text(hh, 240, 236, 106, 0x3D2B1F)
    ui.round_rect(118, 306, 244, 48, 24, 0xF1D7BE)
    ui.text(string.format("%d月%d日", now.mon, now.day), 240, 330, 29, 0x6B4A36)
  end
end
```

天气插件的写法可以看固件里内置的那份：网页「示例插件 → 天气 → 安装」之后，
用读卡器看 `/sdcard/plugins/weather/main.lua`，里面有 `http.get` + `json.decode` + `store` 缓存的完整用法。

## 10. 安装与分发

**方式一：网页上传**

1. 打开后台 →「安装插件」→ 选文件
2. **必须同时选中 `manifest.json` 和 `main.lua`**（文件名不能改）
3. 点上传 → 成功后出现在列表里

**方式二：直接拷卡**

把整个目录拷到 `/sdcard/plugins/<id>/`，重启设备。

**分发**：把这两个文件打包成 zip 发给别人即可；不要带 `settings.json` / `data.json`（那是用户自己的数据）。
