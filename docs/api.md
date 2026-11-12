# HTTP API

设备自己就是一台 web 服务器（esp_http_server），网页后台调的就是这些接口，
你也可以用 `curl` 直接操作。

- 热点地址：`http://192.168.4.1`
- 局域网地址：网络屏上显示的那个 IP（`/api/status` 里的 `sta_ip`）
- 所有响应都是 `application/json`（网页本身是 gzip 的 HTML，路径 `/`）
- 没有鉴权：谁连上热点/同一局域网谁就能用，别把它暴露到公网

## 状态

### `GET /api/status`

```json
{
  "fw": "1.0.0",
  "app": "pet",
  "ssid": "Mochi-AD41",
  "ip": "192.168.4.1",
  "ap_ip": "192.168.4.1",
  "sta_ip": "10.140.30.102",
  "sta_ssid": "meizu",
  "sta": true,
  "sta_state": "connected",
  "sta_reason": 0,
  "sta_rssi": -52,
  "sd": true,
  "fps": 24.0,
  "heap": 5064680,
  "psram": 5006356
}
```

| 字段 | 说明 |
|---|---|
| `app` | 当前屏幕的 id（`net` / `pet` / `clock` / `weather` …） |
| `ip` / `ap_ip` | 热点地址，固定 `192.168.4.1` |
| `sta_ip` | 连上路由器后的局域网地址，没连上是空串 |
| `sta_state` | `idle` 没配置 / `connecting` / `connected` / `failed` |
| `sta_reason` | 断线原因码（15/204/205 密码错，201 没找到 AP，203 信号弱） |
| `fps` | 最近一秒的渲染帧率 |
| `heap` / `psram` | 空闲堆（含 PSRAM）/ 空闲 PSRAM（字节） |

## 应用（插件）

### `GET /api/apps`

```json
{"apps":[
  {"id":"net","name":"网络","version":"1.0.0","kind":"builtin","enabled":true},
  {"id":"clock","name":"时钟","version":"1.1.0","kind":"lua","enabled":true}
]}
```

`kind`：`builtin` 是编进固件的，`lua` 是卡上的。

### `GET /api/app?id=<id>`

返回插件的 manifest **加上**当前设置值：

```json
{
  "id": "weather", "name": "天气", "version": "2.5.0", "api": 1,
  "settings": [{"key":"city","label":"地名","type":"text","default":"深圳"}],
  "values": {"city":"贵阳","lat":"26.62","lon":"106.63","unit":"c"},
  "enabled": true, "builtin": false
}
```

`settings` 是「表单描述」，`values` 是用户填的值——两者同名会在网页端打架，所以分开。

### `POST /api/app/settings?id=<id>`

请求体是扁平 JSON：

```json
{"city":"贵阳","lat":"26.62","lon":"106.63","unit":"c"}
```

写完立刻落盘到 `/sdcard/plugins/<id>/settings.json`，并让插件重新加载。

### `POST /api/app/enable?id=<id>&on=1|0`

启用/停用。停用名单存在 NVS，停用的插件不进轮换（全停用时会保底显示网络屏）。

### `DELETE /api/app?id=<id>`

只能删卡上的 Lua 插件（内置的返回 400）。

### `POST /api/upload?id=<id>&file=<name>`

请求体就是文件原始字节，写到 `/sdcard/plugins/<id>/<name>`。
文件名不允许带路径分隔符；目录不存在会自动创建。

### `POST /api/install?id=<id>`

校验 `manifest.json` 能被解析，然后把它设为「当前插件」。

### `GET /api/builtin` / `POST /api/builtin/install?id=<id>`

固件内置的示例插件列表 / 一键安装到卡上（时钟、天气）。

## WiFi

### `GET /api/scan`

阻塞 2–4 秒扫一遍，返回按信号强度排序的网络：

```json
{"networks":[
  {"ssid":"CLOVES-COFFEE","rssi":-70,"auth":4},
  {"ssid":"iTV-hdQF","rssi":-70,"auth":4}
]}
```

`auth`：0 开放，4/5 WPA2，7 WPA3。隐藏网络不会出现。

### `POST /api/wifi?ssid=<ssid>&pass=<pass>`

保存并连接。密码空字符串表示开放网络。热点始终保留。

## 其它

### `POST /api/gesture?event=next|prev|shake|tap`

模拟手势：`next`/`prev` 切应用，`shake`/`tap` 直接派发给当前应用。

```bash
curl -X POST "http://192.168.4.1/api/gesture?event=next"
```

### `POST /api/ota`

请求体是固件 `.bin`（`idf.py build` 出来的 `mochi.bin`）。写入 OTA 分区、校验后自动重启。

```bash
curl -X POST --data-binary @build/mochi.bin http://192.168.4.1/api/ota
```

### `GET /`

返回内嵌的网页（gzip + `Cache-Control: no-store`）。

## 用 curl 走一遍典型流程

```bash
# 看状态
curl -s http://192.168.4.1/api/status | jq .

# 装一个内置示例插件
curl -X POST "http://192.168.4.1/api/builtin/install?id=weather"

# 改它的设置
curl -X POST http://192.168.4.1/api/app/settings?id=weather \
     -H 'Content-Type: application/json' \
     -d '{"city":"贵阳","lat":"26.62","lon":"106.63","unit":"c"}'

# 强制切到天气屏
curl -X POST "http://192.168.4.1/api/gesture?event=next"

# 连 WiFi
curl -X POST "http://192.168.4.1/api/wifi?ssid=meizu&pass=你的密码"
```
