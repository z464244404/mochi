# Mochi 架构设计

> 一块 480×480 圆屏 + ESP32-S3，做成桌面上「活着」的小东西：
> 默认是一个会眨眼、会跟着你倾斜看、被摸会笑的表情；摇一摇切换下一个应用（时钟、天气、番茄钟…），
> 应用从网页上传安装，不用重新刷固件。

本文是**平台层**的设计。M1（当前）只实现了内核：渲染器 + 表情 + 时钟 + 手势 + RTC。
插件运行时（Lua）和网页后台在 M2。

---

## 1. 为什么这样分层

AWTRIX 的本质不是那块 8×32 点阵，而是「小屏 + 可扩展应用 + 网页配置」。
换到 480×480 圆屏，最大的变化是**每帧 450KB**（480×480×2B）：

- 全屏重绘必须直接写 PSRAM 帧缓冲，不能再拷来拷去；
- 插件绝不能自己碰像素，否则一个 `for` 循环就能把帧率拖死；
- 所以插件只描述「画什么」，C 层负责栅格化。

```
┌──────────────────────────────────────────────────────────┐
│ apps / plugins                                           │
│   内置（C）: face · clock       第三方（Lua，M2）: 天气…  │
│   同一套回调: enter / draw(gfx, dt) / event / leave       │
├──────────────────────────────────────────────────────────┤
│ app manager   切换过渡（淡出/淡入）、事件分发             │
├──────────────────────────────────────────────────────────┤
│ gfx           480×480 RGB565 软件渲染器，SDF 抗锯齿       │
│               round_rect / circle / ellipse / arc / text  │
├──────────────────────────────────────────────────────────┤
│ lcd           ST7701S + RGB565 并口 + 3 帧缓冲零拷贝      │
│ imu           QMI8658 50Hz → 摇一摇 / 轻敲 / 翻转 / 倾斜  │
│ rtc           PCF85063                                    │
└──────────────────────────────────────────────────────────┘
```

### 为什么不用 LVGL

LVGL 适合「控件 + 事件」的界面，不适合「每帧整屏重画、图形以几何图元为主」的摆件：

- 全屏 480×480 的 canvas 在 LVGL 里仍然要经过对象/刷新机制，多一层开销；
- 我们需要的只有 6 种图元 + 字体，自研渲染器 ~600 行，可控、可测、启动快；
- 以后要给插件做「沙箱画布」，自己的命令式 API 比暴露 LVGL 对象模型更安全。

### 为什么插件用 Lua 而不是原生代码

| 方案 | 能否网页安装 | 风险/成本 |
|---|---|---|
| 原生 `.so` | ✗ | Xtensa 没有 MMU 隔离，IDF 不支持 `dlopen`；等于每次加插件重刷固件 |
| MicroPython | ✓ | 启动慢、RAM 占用大、和 C 渲染层胶水复杂 |
| WASM | ✓ | 解释执行慢 5~10×，工具链重 |
| **Lua 5.4** | ✓ | ~250KB Flash，纯 C，沙箱容易，逻辑性能足够（它只跑逻辑，不跑像素） |

---

## 2. 插件包格式

一个插件就是一个 zip（后缀 `.mochi`，本质是 zip）：

```
weather.mochi
├─ manifest.json
├─ main.lua
└─ assets/
   ├─ icon.png        # 列表里显示的 96×96 图标
   └─ sun.png
```

`manifest.json`：

```json
{
  "id": "com.mochi.weather",
  "name": "天气",
  "version": "1.0.0",
  "api": 1,
  "author": "mochi",
  "entry": "main.lua",
  "icon": "assets/icon.png",
  "permissions": ["net", "storage"],
  "settings": [
    { "key": "city", "type": "string", "label": "城市", "default": "深圳" },
    { "key": "unit", "type": "enum", "label": "单位", "options": ["C", "F"], "default": "C" }
  ]
}
```

规则：

- `id` 反向域名，全局唯一，同时是目录名 `/<plugins>/<id>/`；
- `api` 不匹配直接拒绝安装（固件升级时插件不会被静默跑坏）；
- 安装是原子的：先解压到 `<id>.tmp`，校验 manifest + 入口文件 + 总大小，再 rename；
- 单个插件上限 2MB（素材另算），总空间用完时网页提示删插件；
- 卸载 = 删目录 + 删 `/data/<id>/`。

### 权限

| 权限 | 能力 |
|---|---|
| `net` | `http.get/post`，只允许 https，域名白名单可配 |
| `storage` | 自己的 `/data/<id>/` 读写，配额 64KB |
| `notify` | 往其它应用弹 toast（M3） |

默认无权限。没有 `fs`、`os`、`io`、`package.loadlib`。

---

## 3. Lua API（M2）

```lua
local ui   = require("ui")     -- 画布
local time = require("time")
local http = require("http")
local app  = require("app")    -- 设置、日志、跳转

function on_start()
  app.log("weather started")
  fetch()
  app.every(600000, fetch)     -- 10 分钟
end

function on_draw(c, dt)        -- 每帧，c 是 480×480 画布
  c:clear("#0b0d12")
  c:text(state.temp .. "°", 240, 230, { size = 96, color = "#f4f7f8", align = "center" })
  c:text(state.city, 240, 320, { size = 30, color = "#7e8a99", align = "center" })
end

function on_event(ev)          -- "shake" / "tap" / "flip"
  if ev == "tap" then fetch() end
end

function fetch()
  http.get("https://api.example.com/weather?city=" .. app.setting("city"), function(res)
    if res.status == 200 then
      state = json.decode(res.body)   -- json 库随固件内置
    end
  end)
end
```

### 画布方法（v1）

| 方法 | 说明 |
|---|---|
| `clear(color)` | 整屏填充 |
| `rect(x, y, w, h, color)` / `round_rect(..., r)` | 左上角坐标 |
| `circle(cx, cy, r, color)` / `ellipse` | |
| `ring(cx, cy, r, thick, color)` / `arc(cx, cy, r, thick, a0, a1, color)` | 角度制，0°=右，顺时针 |
| `line(x0, y0, x1, y1, thick, color)` | |
| `text(str, x, y, opts)` | `opts.size` 只支持内置的 29 / 106 两档（M3 加更多字号）；`align` = left/center/right |
| `image(name, x, y, w, h)` | `name` 是包内 `assets/` 的文件名，解码后缓存在 PSRAM |

颜色支持 `"#rrggbb"`、`"#aarrggbb"`。

### 限额（沙箱）

| 项 | 限额 | 超了怎么办 |
|---|---|---|
| 单帧 Lua 时间 | 6ms | 跳过本帧绘制，记一次 warning |
| 单次回调指令数 | 200k（`lua_sethook`） | 中断回调，`pcall` 捕获 |
| 单插件内存 | 256KB（自定义 allocator 记账） | 分配失败 → Lua 抛错 |
| 网络回调 | 10s 超时 | 错误码返回给插件 |
| 连续出错 | 5 次 | 自动停用该插件，网页显示原因 |

插件崩溃不能影响主循环：所有入口都经过 `lua_pcall`，Lua 栈/状态损坏时整插件卸载重载。

---

## 4. 手势

QMI8658 以 50Hz 采样，先用低通估重力，再做差得到线性加速度。

| 手势 | 判据 | 动作 |
|---|---|---|
| 摇一摇 | 450ms 内 ≥4 次线性加速度 >0.45g | 下一个应用（冷却 1.2s） |
| 轻敲 | 安静 300ms 后单次 >1.1g，且不在连击里 | 当前应用收到 `tap` |
| 双击 | 两次 tap 间隔 <420ms | 表情转晕（`dizzy`） |
| 翻转朝下 | 重力 z < -0.55g 持续 700ms | 息屏 |
| 翻回来 | z > 0.55g 持续 400ms | 亮屏 |
| 倾斜 | roll / pitch | 表情视线跟随（±13px） |

阈值都在 `imu.c` 顶部，实机调。左右/前后方向如果反了，改 `s_roll`/`s_pitch` 的符号即可。

**摇一摇为什么直接切应用**：这是用户最直觉的「换一个」动作。表情的「转晕」放在双击上，
两者不抢同一个手势。

---

## 5. 网页后台（M2）

设备起 AP + STA 双模：没配网时是 `Mochi-XXXX` 热点，配网后同时连家里 WiFi。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/status` | 固件版本、内存、帧率、当前应用、IP |
| GET | `/api/apps` | 已安装插件列表（含内置） |
| POST | `/api/apps` | 上传 `.mochi`（流式写 Flash，边收边校验） |
| DELETE | `/api/apps/<id>` | 卸载 |
| POST | `/api/apps/<id>/enable` | 启用/停用 |
| GET/POST | `/api/apps/<id>/settings` | 读写插件设置（表单由 manifest 生成） |
| POST | `/api/order` | 调整顺序（决定摇一摇的轮换顺序） |
| POST | `/api/gesture` | 注入手势，调试用（`{"event":"shake"}`） |
| GET | `/api/preview` | MJPEG 实时画面（5fps，网页里看设备在显示什么） |
| POST | `/api/ota` | 固件升级 |
| POST | `/api/wifi` | 保存家里 WiFi |

前端仍然走「单文件内联 + gzip 进固件」的老路（参考 smart-emblem），不依赖外网 CDN。

---

## 6. 存储

16MB Flash 的分配（`partitions.csv`）：

```
0x000000  bootloader
0x009000  nvs        24KB    配置
0x00f000  otadata     8KB
0x011000  phy_init    4KB
0x020000  ota_0       3MB    固件（Lua + 字体 + 内置应用）
0x320000  ota_1       3MB
0x620000  assets    9.875MB  littlefs：插件、图片、字体、日志
```

固件目前 ~1.1MB（字体 113KB），加上 Lua 约 1.4MB，3MB 分区足够。

---

## 7. 性能预算（480×480 / 30Hz）

面板 PCLK 18MHz，整屏刷新约 30Hz，所以渲染目标就是 30fps（33ms/帧）。

| 项 | 估算 |
|---|---|
| 背景（逐行渐变填充 230k px） | ~2ms |
| 体块（SDF + 渐变，87k px，边缘才开方） | ~6ms |
| 外发光两层 | ~3ms |
| 眼睛/嘴/腮红 | <1ms |
| 时钟文字（5 字形 × 8.5k px） | ~2ms |
| **合计** | **~14ms** |

优化手段：SDF 内部像素跳过开方（`d < -1` 直接满覆盖）、逐行算渐变、刻度用线段而不是逐像素角度判定。

---

## 8. 里程碑

| | 内容 | 状态 |
|---|---|---|
| M1 | 渲染器 + 表情 + 时钟 + 摇一摇/敲击/翻转 + RTC | 代码完成，待上机 |
| M2 | Lua 运行时 + 插件安装/卸载 + 网页后台 + NTP | 待做 |
| M3 | 插件市场（静态 index.json）、更多字号/图标、toast、番茄钟/天气示例 | 待做 |
| M4 | 桌面模拟器（SDL/Lua 同 API）、主题、低功耗 | 待做 |

---

## 9. 还没定的

- 插件签名：要不要 ed25519 验签（防篡改，但个人项目可能嫌麻烦）——先不做，manifest 校验足够。
- 中文字体子集：现在内置 138 字，M2 需要按插件包自带字体或全量 3500 字（约 2MB）。
- 是否支持 MQTT / Home Assistant（AWTRIX 用户很在意）——M3 再评估。
