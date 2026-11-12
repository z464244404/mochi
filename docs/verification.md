# 实机验证记录（M1）

板子：ESP32-S3-LCD-2.8C（S3R8 / 16MB Flash / 8MB Octal PSRAM），USB = `COM10`（USB-Serial-JTAG）
固件：Mochi 0.1，ESP-IDF v5.5.2

## 1. 能跑起来

```
I (211) octal_psram: density 0x03 (64 Mbit)
I (997) lcd: frame buffers in psram: 3 x 450 KB
I (1007) imu: found QMI8658 at 0x6B, WHO_AM_I=0x05
I (705) rtc: RTC 2026-11-12 13:51:52
I (31079) mochi: vsync=43.0 render=35.0 draw=28.6ms app=face
```

- PSRAM 8MB、三帧缓冲正常
- RTC 走时正确（芯片里有电，读出来就是当前时间）
- 面板刷新 43Hz（PCLK 12MHz），渲染 35fps，绘制 28.6ms/帧

## 2. 上机才发现的问题（都修了）

| 问题 | 现象 | 修法 |
|---|---|---|
| 六轴 Z 轴方向相反 | 板子平放 `az = -1.02g`，而代码按 +1g 判断「屏幕朝下」，预热一结束就息屏，画面停在最后一帧 | 翻转判据反号；`roll` 用 `-gz` 做参考 |
| roll 恒为 177° | 视线一直顶在边上 | 同上 |
| 插拔震动误触发 | 开机瞬间 shake 把应用切走了 | 上电 2.5s 内不判手势 |
| `rtc_init` 与 IDF 内部符号重名 | 链接失败：`multiple definition of rtc_init` | 改名 `rtc_clock_init` |

## 3. 性能：从 5fps 到 35fps

第一次跑：`draw=196ms`，5fps。分段计时 `bg=15.7 glow=98.6 body=72.8 feat=9.9`。

根因不是「代码写得重」，是**每个像素都在调 libm**：

```
$ xtensa-esp32s3-elf-nm -u gfx.c.obj
U __divsf3   U fmaxf   U sqrtf   ...
```

`sqrtf` 不会内联成 `sqrt.s`（要维护 errno），圆角半径 114 又让几乎整个体块都落在 sqrt 分支里，
一帧 8 万次函数调用。

逐项优化（实测）：

| 改动 | 效果 |
|---|---|
| `fmaxf` → 三元、`-fno-math-errno` | 170ms → 106ms |
| `sqrt(d²)` → 一阶近似 `(d²-r²)/(2r)`（误差 <0.01px） | body 60 → 40ms |
| 圆角矩形改**按行求区间**：内部整段直写，只有两侧 1~2 像素算 AA | body 40 → 10.4ms |
| 文字逐行裁剪，去掉每像素边界判断 | feat 8.6 → 4.7ms |
| 面板 PCLK 18MHz → 12MHz（64.7Hz → 43Hz），把 PSRAM 带宽让给渲染 | bg 15.7 → 13.2ms |

内存基准（这才是硬上限）：

```
write fb     450KB: 15.9 ms (28.8 MB/s)
write psram  450KB: 15.7 ms (29.3 MB/s)   ← 帧缓冲并不特殊，PSRAM 就这个速度
write dram   128KB:  0.31ms (422 MB/s)
read  fb     450KB: 11.3 ms (40.9 MB/s)
```

**结论：480×480×2B = 450KB/帧，PSRAM 写只有 ~30MB/s，所以整屏重绘的下限就是 ~15ms。**
全屏 60fps 在这块板子上不可能；30~40fps 是合理目标。

## 4. 现在的开销（每帧）

| 段 | 耗时 |
|---|---|
| 背景（450KB 逐行渐变） | 13.2ms |
| 体块（86k px 区间填充） | 10.4ms |
| 五官 | 4.7ms |
| 外发光 | 已关闭（原 45ms） |

## 5. 还没做 / 已知缺口

- **外发光**：逐像素半透明要读改写 30k 像素，单独就 45ms。要补的话走「开机预渲染一张辉光纹理，每帧只在环带上 blit」。
- 背景每帧重写 450KB 是最大单项开销。彻底解法是三个缓冲各自保留静态背景，只重绘脏区（切换应用时全刷）。
- 网页后台、Lua 插件、NTP 都还没开始（M2）。

## 6. 调试手段

- `MOCHI_DEBUG_DUMP=1`：串口每 6s 打一张 60×30 的 ASCII 灰度截图，没有摄像头也能看画面。
- `tools/capture.py COM10 20`：抓原始串口日志（去 ANSI）。
- 串口 `'s'` / `'n'` 命令：仅当主控台切到 USB-JTAG 时可用；当前主控台是 UART0（43/44），所以没启用。

## 7. 桌宠动画 + TF 卡（2026-11-12 下午）

素材：53 帧 480×480 PNG（吃米饭）→ JPEG q82 → `pet.mjp` 1.14MB（22KB/帧，黑底压缩率高）。

- 解码：`esp_new_jpeg` 直接解进 LCD 帧缓冲（零拷贝），约 36ms/帧
- 播放：按标称 24fps 节流（解码快于 24fps 时 `vTaskDelay` 到该帧时间点），实测 **24.0fps**
- JPEG 输入缓冲放内部 RAM（64KB），比放 PSRAM 快一点

TF 卡（这块板是 SDMMC 1-bit，不是 SPI）：

```
SD_CLK = GPIO2   SD_CMD = GPIO1   SD_D0 = GPIO42   SD_D3/CS = EXIO4
```

- `SD_D3` 挂在 TCA9554 上，必须在挂卡前拉高，否则卡会进 SPI 模式
- GPIO1/2 和屏幕初始化的 3 线 SPI 共用 → `lcd_init()` 里用完要 `spi_bus_free(SPI2_HOST)`
- `SDMMC_HOST_SLOT_1` + 1-bit + 20MHz，挂载 32GB 卡正常

运行时策略：卡上优先。**不整段缓存**——后台任务环形预读 12 帧（576KB），主循环边解边放。
实测 40MHz 顺序读 1.23MB/s，播放消耗 ~0.5MB/s（53 帧版），余量很大；帧率 24.0fps，PSRAM 空闲 5.6MB。
flash 里那份副本已擦除（`esptool erase_region 0x620000 0x9E0000`），分区留给插件。

> 一开始是整段 5MB 读进 PSRAM（旧 121 帧素材，19fps、只剩 1.1MB），改成流式后内存还回去 4.5MB。

### 顺手修掉的坑

| 现象 | 原因 | 修法 |
|---|---|---|
| 每 5 秒 `task_wdt: IDLE0` 触发 | 解码 50ms > vsync 周期 23ms，信号量永远就绪，主循环从不阻塞，IDLE0 被饿死 | 每帧 `vTaskDelay(1)` |
| 卡上 0 字节 `pet.mjp` 导致一直显示 NO ASSET | 只判断文件存在，不判断大小 | 小于 64 字节删除重拷 |

内存现状：加载素材后 PSRAM 剩 ~5.6MB、内部 RAM 剩 ~1.4MB。

## 8. Lua 运行时（M2）

Lua 5.4.7 从 `github.com/lua/lua` 的 v5.4.7 标签逐个文件拉取（本机代理只放行 raw.githubusercontent，
lua.org 仍然 TLS 失败），共 58 个 .c/.h，编成 `components/lua`。

- `LUA_32BITS`：`lua_Number` = float，走 S3 的单精度 FPU
- 固件 586KB → **720KB**（Lua 约 134KB）
- 只注册 base/string/table/math/utf8/coroutine

实机：

```
W (1115) lua: no /sdcard/plugins/demo/main.lua, running built-in demo
I (1117) lua: plugin ready, heap 12/192 KB
```

内置 demo 连续绘制十几秒，没有任何 `on_draw failed`（每帧一次 pcall，出错会立刻打日志并停用插件）。
主任务栈从 8KB 提到 20KB（Lua 解析器是递归下降）。

**待补的验证**：这一屏的像素没有肉眼/截图确认（ASCII dump 当时是关的）；
等网页后台做完，可以通过 `/api/gesture` 或直接切换应用做确定性验证。

## 9. 网页后台（M2 后半）

设备自检（自己访问自己的 AP 地址，不依赖手机）：

```
web: selftest status: {"fw":"0.2.0","app":"pet","ssid":"Mochi-AD41","ip":"192.168.4.1","sd":true,"fps":24.0}
plugins: delete test: ok
web: uploaded test/manifest.json (55 bytes)
web: uploaded test/main.lua (228 bytes)
web: installed test
web: selftest apps: {"apps":[{"id":"test","name":"SELFTEST","version":"0.1","active":true}],"active":"test"}
```

上传、安装、列表、激活整条链路都通了。固件 1.31MB，分区剩 55%，PSRAM 剩 ~5.1MB。

### 踩到的坑：FATFS 默认不支持长文件名

现象：`main.lua` 能上传成功，`manifest.json` 却一直 `fopen` 失败（errno 22 / EINVAL），
而且「同一个文件第一次失败、第二次成功」，非常像时序问题，加了重试也没用。

真正原因：ESP-IDF 的 FATFS 默认 `CONFIG_FATFS_LFN_NONE`。**8.3 短文件名**（`MAIN.LUA`）能建，
长文件名（`manifest.json`）创建直接返回 `FR_INVALID_NAME` → `EINVAL`。

修法：

```
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
```

顺带：目录名回读可能是 8.3 的大写形式（`TEST`），所以插件 id 以 `manifest.json` 里的 `id` 为准，
不要用 `readdir` 的名字。

### 其他

- `sd_open_write()`：带重试的写打开，挂载后第一次写偶尔失败，留着兜底
- 自检默认关闭（`MOCHI_WEB_SELFTEST`），打开时每次开机都会重装 `test` 插件

## 10. 网络屏 + 局域网访问

需求：连上家里 WiFi 后能用路由器分配的 IP 访问，并且屏幕上要显示这个 IP；没连时提示去连热点。

- `net.c` 在 `IP_EVENT_STA_GOT_IP` 里记下地址和 SSID，`net_sta_ip()` / `net_sta_ssid()` 对外
- `/api/status` 同时返回 `ap_ip` 和 `sta_ip`
- 新增「网络」屏（应用索引 0）：连上显示 IP + `http://<ip>` + SSID；没连显示热点名、密码、
  `http://192.168.4.1`；开机时没连上就停在这一屏，连上后 6 秒自动进桌宠
- 开机时有保存的 WiFi 会给 1.5 秒连接时间：连上直接进桌宠，否则先停在网络屏

实机（未配 WiFi 的情况，用 ASCII 截图确认过版面）：

```
mochi: boot screen: net (sta=0 creds=0)
--- frame ---
   WIFI NOT CONNECTED
     JOIN HOTSPOT
      Mochi-AD41
    PASS mochi1234
       THEN OPEN
   http://192.168.4.1
```

### 又踩一个：`LUA_32BITS` 被头文件覆盖

`components/lua/src/luaconf.h` 里有一行默认值 `#define LUA_32BITS 0`。
命令行传 `-DLUA_32BITS` 会先定义成 1，紧接着被这行覆盖回 0（只是 `redefined` 警告，很容易漏看），
结果 Lua 一直用 64 位 double —— 在 S3 上是软浮点。
正确做法是直接改 `luaconf.h` 为 1，命令行不要再传。

## 11. WiFi 扫描与连接反馈

问题：网页填了 SSID 后只显示「已保存，正在连接」，没有任何后续；也看不到失败原因。

三个根因：

1. **事件回调里 `vTaskDelay(5s)`**：断线重连写在了 WiFi 事件回调里，直接把整个事件任务堵死，
   后续事件（包括连上/断开）都处理不及时 → 表现就是「没反应」。
   改成独立的重试任务 + `xTaskNotifyGive`，回调里只记录原因码。
2. **扫描失败**：没配 WiFi 时工作在纯 AP 模式，STA 接口没起来，`esp_wifi_scan_start` 直接报错。
   改成常驻 `WIFI_MODE_APSTA`（没配 WiFi 时 STA 只挂着不连），扫描才有接口可用。
3. **没有反馈**：`/api/status` 现在带 `sta_state`（idle/connecting/connected/failed）、
   `sta_reason`（断线原因码）、`sta_rssi`。

实测扫描（设备自检）：

```
net: scan found 4 networks
{"networks":[{"ssid":"CLOVES-COFFEE","rssi":-70,"auth":4},{"ssid":"iTV-hdQF","rssi":-70,"auth":4},
{"ssid":"<中文名>","rssi":-72,"auth":7},{"ssid":"CU_P6Me","rssi":-75,"auth":4}]}
```

网页侧：扫描按钮列 SSID + 信号强度，点一下填进输入框；连接后每秒轮询状态，
失败时按原因码给中文提示（15/204/205 密码错、201 没找到、203 信号弱）。
屏幕在网络屏底部也会显示 `ERR <code>`。

## 12. 插件详情 / 设置 / 天气（M2 收尾）

### 设置链路（实机）

```
web: selftest install weather: 21
builtin: wrote /sdcard/plugins/weather/manifest.json (466 bytes)
builtin: wrote /sdcard/plugins/weather/main.lua (2045 bytes)
pconf: saved 59 bytes to /sdcard/plugins/weather/settings.json
selftest readback: {"id":"weather",...,"values":{"city":"Shenzhen","lat":"22.54",...}}
```

manifest → 网页表单 → settings.json → `app.get()`，闭环。

### 天气插件（真联网）

`https://api.open-meteo.com`，HTTPS，证书走 mbedtls bundle：

```
esp-x509-crt-bundle: Certificate validated
lua: weather 27.2 OVERCAST
```

回调在绘制线程执行（`http.get` 的结果先入队，由 `lua_rt_draw` 取出再 `pcall`），
Lua 状态始终只在主任务里被碰。

### 这一轮踩的坑

| 现象 | 原因 | 修法 |
|---|---|---|
| 打开插件详情直接崩（StoreProhibited） | `plugin_conf_read` 在栈上开 4KB + `app_get` 2KB，而 httpd 任务栈只有 8KB | 大缓冲改 PSRAM 堆，栈提到 12KB |
| 屏幕时间比本地慢 8 小时 | SNTP 给的是 UTC，直接写进 RTC 又被当成本地时间 | 加时区偏移（默认 +480 分钟），`gmtime_r` 后再写 |
| 天气失败后 10 分钟才重试 | `next_fetch` 在发起请求时按 `err` 判断，但 `err` 是回调里才设置的 | 把重试间隔写进回调 |
| STA 开机偶发连不上（reason 4 / 205），要重试两三次 | AP 侧「Association refused temporarily」，且 APSTA 省电模式影响 | `esp_wifi_set_ps(WIFI_PS_NONE)`；5 秒重试一次 |

> 剩下的：STA 首次连接仍可能失败一两次（路由器忙），15 秒内基本能连上。

## 13. 时钟重写 + 多插件槽位

### 星期显示不出来的真正原因

不是字体缺字（`周`/`四` 都在 138 字的子集里），是**缓冲区太小**：

```c
char buf[16];
snprintf(buf, sizeof(buf), "%d月%d日 周%s", ...);   // "11月12日 周四" = 17 字节
```

`snprintf` 截断在 15 字节，正好把「四」丢掉，屏幕上是「11月12日 周」。

### 时钟改成插件

- 三种样式：`cute` / `tech` / `minimal`，都在 `main.lua` 里用 `ui.*` 画
- `minimal` 只有 12 个刻度和三根指针，背景/指针/秒针颜色用 `type: "color"` 的色板
- 时间为屏幕正中（y=240），不再是原来偏上的 222

### 应用列表支持多插件

原来是「网络 / 桌宠 / Lua 单槽」，只能同时跑一个插件（时钟和天气二选一）。
改成：固定应用 + **每个已安装插件一个槽位**，各槽共用一套 enter/draw，靠索引找到自己的 `main.lua`。

顺带修了一个 bug：`lua_rt_start` 原来按「全局激活插件」读设置，多槽位后天气会读到时钟的配置；
现在按槽位 id 读。

### 内置插件自动更新

`builtin_sync()` 每次开机比对 manifest 里的版本号，缺的装、旧的重装，用户自己装的插件不动。
这次实测：

```
builtin: clock: (no version) -> 1.1.0, (re)installing
builtin: wrote /sdcard/plugins/clock/manifest.json (630 bytes)
builtin: wrote /sdcard/plugins/clock/main.lua (3485 bytes)
```

## 14. 统一应用列表 + 启用开关

### 桌宠和网络也变成「插件」

代码还是 C（桌宠要跑 MJPEG 解码，不能塞进 Lua），但它们在 `builtin_apps.c` 里注册了 manifest，
于是和卡上的插件共用同一套东西：列表项、详情页、启用开关、设置存储。

`/api/apps` 实测：

```
{"apps":[
  {"id":"net","kind":"builtin","enabled":true},
  {"id":"pet","kind":"builtin","enabled":true},
  {"id":"clock","kind":"lua","enabled":true},
  {"id":"weather","kind":"lua","enabled":false}]}
```

### 启用 / 停用

停用名单存在 NVS 的 `off` 键（逗号分隔），默认全开。停用后不进轮换：

```
plugins: weather disabled (off list: 'weather')
```

轮换实测变成 net → pet → clock → net，天气被跳过。

### 极简配色「改不了」

先怀疑是颜色没落盘，实测反过来——存了 87 字节后背景立刻从深色变纯白：

```
pconf: saved 87 bytes to /sdcard/plugins/clock/settings.json
--- frame --- (整屏 @ )
```

真正原因是浏览器缓存了旧页面：`/` 之前没带 `Cache-Control`。现在加了 `no-store`，
并且保存后会自动重新拉一次设置回显。

### 一个自己挖的坑：PowerShell 改坏了源码编码

用 `(Get-Content -Raw) -replace ... | Set-Content` 批量改 `#define` 时，中文字符被按 GBK 解码后重写，
源码变成非法 UTF-8（部分字节成了 `?`）。代码本身没错，编译也过，但中文注释全乱码。
处理：`bytes.decode('utf-8', errors='replace')` 重写成合法 UTF-8。
**教训：改源码一律用编辑工具，别用 PowerShell 管道。**

## 15. 桌宠叠加 DeepSeek 余额

接口：[Get User Balance](https://api-docs.deepseek.com/api/get-user-balance/)，`GET /user/balance` +
`Authorization: Bearer <API_KEY>`，不需要账号登录。

实测（真 key，真机）：

```
dsec: key set, refresh 600s, peak
esp-x509-crt-bundle: Certificate validated
dsec: balance 5.79, today 0.02
...
dsec: balance 5.69, today 0.12
```

第二次是刷了不含 key 的正式固件之后，key 从 `/sdcard/plugins/pet/settings.json` 读出来，
余额 5.69、今日 0.12（基线在 NVS 里，跨重启保留）。

### 三个数据怎么来的

| 数据 | 来源 |
|---|---|
| 余额 | 官方接口 `total_balance`（CNY）|
| 今日消耗 | 当天首次读数存 NVS 当基线，`今日 = 基线 - 当前`。官方没有用量接口 |
| 峰谷 | 本地时间判断，北京时间 00:30–08:30 为谷时（对应 UTC 16:30–00:30 的优惠时段）|

### 验证过程

1. 先用假 key 跑，确认 401 分支：`dsec: 401 invalid api key`
2. 再用真 key 临时编进自检固件，POST 到 `/api/app/settings?id=pet`，确认拿到真实余额
3. 然后删掉源码里的 key、关自检、重新构建，并用脚本检查 `mochi.bin` 里不含 key 字符串
4. 刷入后确认仍能读到余额（说明 key 只来自 SD 卡上的设置文件）

### 已知限制

- 今日消耗是「余额差」估算：设备当天没联网的那段时间产生的消耗会算漏
- key 明文存 SD 卡；泄露了就去官网重置
- 余额刷新间隔在详情页选（5/10/30/60 分钟），进入桌宠时若超过 2 分钟会立刻刷一次

## 16. 三段动画 + 多文件素材包

### 素材包（电脑不能写卡）

`tools/pack_assets.py` 把多个 `.mjp` 打成一个 `assets.bin`：

```
magic 'AS1\0' | u32 count | u32 rsv | count * {u32 off, u32 size, char name[40]} | blobs
```

烧到 assets 分区（0x620000），开机 `assets_sync()` 逐个比对卡上文件大小，只拷不一致的。

```
assets: asset sync: 0/3 file(s) updated     ← 已同步过，跳过
```

三个文件：pet.mjp 1.14MB / work.mjp 1.16MB / sleep.mjp 1.99MB，合计 4.29MB。

### 动画状态机

余额每 5 分钟查一次；记下「最后一次变化」的时间（NVS `dchg` + 上次余额 `dbal`）：

| 距上次变化 | 动画 |
|---|---|
| < 5 分钟 | work.mjp |
| 5–30 分钟 | pet.mjp |
| > 30 分钟 | sleep.mjp |

实机验证（测试模式 30/60/90 秒一轮）：

```
I (30911) pet: stream from SD: 55 frames @ 24 fps ... clip: /sdcard/work.mjp
I (60939) pet: stream from SD: 92 frames @ 16 fps ... clip: /sdcard/sleep.mjp
I (90938) pet: stream from SD: 53 frames @ 24 fps ... clip: /sdcard/pet.mjp
```

切片段的做法：`clip_stop()` 给 producer 一个信号量让它退出 → 关文件 → 清空环形缓冲 →
`clip_open()` 重新解析表并起 producer。不能用「直接换 FILE*」：producer 可能正卡在满队列上。

### HUD 版面（超出圆形屏的问题）

原布局：面板 332 宽 + 徽章中心在 x=402。y=419 处圆形屏的可见宽度是 `2*sqrt(240²-179²)=320`，
也就是 x 只能在 80–400，所以右边被切掉了。

新布局按圆方程算过：

| 元素 | 位置 | 该 y 处可用宽度 |
|---|---|---|
| 余额气泡 148×50 | 中心 (160, 400) | ±179 → 61–419 |
| 今日气泡 156×50 | 中心 (320, 400) | 同上 |
| 峰谷徽章 104×34 | 中心 (240, 447) | ±121 → 119–361 |

角色素材的 alpha 包围盒是 y 90–390，所以 y≥395 这一条是空的，HUD 不会挡脸。

### 性能

工作片段 55 帧 @24fps，实测 `draw=44.1ms`、22.4fps，略低于标称 24（每帧差 2.4ms）。
原因是抓帧+解码本身就要 41ms 左右，HUD 又加了几毫秒。想更顺可以把片段降到 20fps。

## 17. 峰谷时段修正

之前按「北京时间 00:30–08:30 优惠」写的，是更早的规则。官方定价页现在写的是：

> Peak hours are 01:00 - 04:00 and 06:00 - 10:00 UTC, Monday through Friday,
> excluding Chinese public holidays. All other hours are off-peak.

换算北京时间：**峰时 09:00–12:00 与 14:00–18:00（工作日）**，其余全谷时。
18:21 实测 `off-peak`，与页面一致。

实现：`calc_peak()` 用 `wday` 判周末，用分钟数判两个窗口；后台任务每 30 秒重算一次，
跨过 12:00 / 18:00 会自动切。法定节假日没有日历，按工作日处理。

## 18. 天气「一直 SYNC」的真因

```
E (3116) esp-tls: couldn't get hostname for :api.open-meteo.com: getaddrinfo() returns 202
W (3128) lua: http 0: https://api.open-meteo.com/...
```

插件在 3.1 秒就发起请求，而 WiFi 到 16.5 秒才拿到 IP —— DNS 必然失败，
然后按 30 秒重试。这台路由器 STA 还会间歇掉线（reason 4/205），所以经常一直停在 SYNC。

三个修法：

1. **`net.up()`**：Lua 多一个函数，没网时不发请求，每 3 秒重试
2. 占位文字中文化：`等待网络` / `加载中` / `网络错误`
3. **`http.get` 加 2 分钟响应缓存**（按 URL，缓存放 C 里，切屏重建 Lua 状态也不会丢），
   来回翻页不再重复请求

### 顺带发现的老 bug

`apps_index_of()` 在 `apps_init()` 里才构建列表，但 main 里在 `apps_init()` **之前**调用它选开机屏，
永远返回 -1，所以「连上 WiFi 进桌宠、没连进网络屏」这个逻辑从来没生效过。
改成 `apps_init(const char *start_id)`，先 rebuild 再按 id 找。

### 中文 + ℃

字体加了 30 多个字：天气词（多/阵/雷/雹/雾/小/中/大/暴/转/局/部）、`℃` `℉`、
以及 20 个常见城市名。WMO code 映射到「晴 / 多云 / 阴 / 小雨 / 中雨 / 大雨 / 暴雨 / 雷阵雨 / 雪 / 雾」。

温度右边紧跟 ℃ 需要知道数字宽度，所以加了 `ui.measure(s, size)`：

```lua
local w = ui.measure(s, 106)
ui.text(s, 240 - 14, 172, 106, ...)
ui.text("℃", 240 - 14 + w * 0.5 + 20, 196, 29, ...)
```

实测：

```
lua: weather now 12.8, tomorrow 11.0~18.7
```

## 19. 动态图标（meteocons）

图标是 meteocons 的 **SMIL 动画**，不是我自己做的位移动效。

### 怎么把动画变成帧

1. `npm pack @meteocons/svg` 拿到带动画的 SVG
2. 用 **Edge headless + puppeteer-core@13**（Node 14 只支持旧版）打开 SVG，
   `svg.pauseAnimations(); svg.setCurrentTime(t)` 逐帧截图（`omitBackground` 保证透明）
3. 每个图标按自己的 `dur` 取 8 帧（clear-day 是 6s，雨是 3s），帧间隔 = dur/8

15 个图标全部渲染成功，帧之间确实有差异（rain/snow 8 帧互不相同）。

### 素材放哪

**TF 卡**，一点闪存不占：

```
/sdcard/icons.bin   3.3 MB
```

格式：`ICN1` + 条目表（name / off / size / w / h / 帧数 / 帧间隔），
每像素 3 字节（RGB565 小端 + A8）。设备直接整块贴 + alpha 混合，不需要解码器。

分发还是走 assets 分区（`assets.bin` 现在 7.6MB：三个动画 + 图标），
开机 `assets_sync()` 只拷大小不一致的文件。

### 踩的坑：每帧都去读卡

第一版只缓存 1 个图标，而一屏要画 4 个（1 大 3 小），key 来回切换导致每帧重读 221KB：

```
app=weather render=4.8 draw=207.6ms   ← 4.8fps
```

改成 4 槽 LRU 缓存后：

```
app=weather render=42.9 draw=20.2ms   ← 42.9fps
```

代价是 PSRAM 多占约 880KB（4 × 221KB），还剩 3.3MB。

