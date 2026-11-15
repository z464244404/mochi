[中文](api.md) · **English**

# HTTP API

The device is itself a web server (esp_http_server); the web console calls exactly these endpoints,
and you can also drive them directly with `curl`.

- Hotspot address: `http://192.168.4.1`
- LAN address: the IP shown on the network screen (`sta_ip` in `/api/status`)
- All responses are `application/json` (the page itself is gzipped HTML at `/`)
- No authentication: anyone on the hotspot/same LAN can use it, so do not expose it to the public internet

## Status

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

| Field | Description |
|---|---|
| `app` | Current screen id (`net` / `pet` / `clock` / `weather` …) |
| `ip` / `ap_ip` | Hotspot address, always `192.168.4.1` |
| `sta_ip` | LAN address after connecting to the router; an empty string when not connected |
| `sta_state` | `idle` not configured / `connecting` / `connected` / `failed` |
| `sta_reason` | Disconnect reason code (15/204/205 wrong password, 201 AP not found, 203 weak signal) |
| `fps` | Render frame rate over the last second |
| `heap` / `psram` | Free heap (including PSRAM) / free PSRAM (bytes) |

## Apps (plugins)

### `GET /api/apps`

```json
{"apps":[
  {"id":"net","name":"网络","version":"1.0.0","kind":"builtin","enabled":true},
  {"id":"clock","name":"时钟","version":"1.1.0","kind":"lua","enabled":true}
]}
```

`kind`: `builtin` is compiled into the firmware, `lua` lives on the card.

### `GET /api/app?id=<id>`

Returns the plugin manifest **plus** the current setting values:

```json
{
  "id": "weather", "name": "天气", "version": "2.5.0", "api": 1,
  "settings": [{"key":"city","label":"地名","type":"text","default":"深圳"}],
  "values": {"city":"贵阳","lat":"26.62","lon":"106.63","unit":"c"},
  "enabled": true, "builtin": false
}
```

`settings` is the "form description", `values` is what the user entered — the two would collide in the web UI if they shared names, hence they are kept separate.

### `POST /api/app/settings?id=<id>`

The request body is flat JSON:

```json
{"city":"贵阳","lat":"26.62","lon":"106.63","unit":"c"}
```

It is written to disk immediately at `/sdcard/plugins/<id>/settings.json` and the plugin is reloaded.

### `POST /api/app/enable?id=<id>&on=1|0`

Enable/disable. The disabled list is kept in NVS; disabled plugins are skipped in rotation (if everything is disabled, the network screen is shown as a fallback).

### `DELETE /api/app?id=<id>`

Only Lua plugins on the card can be deleted (builtins return 400).

### `POST /api/upload?id=<id>&file=<name>`

The request body is the raw file bytes, written to `/sdcard/plugins/<id>/<name>`.
File names must not contain path separators; missing directories are created automatically.

### `POST /api/install?id=<id>`

Validates that `manifest.json` parses, then makes it the "current plugin".

### `GET /api/builtin` / `POST /api/builtin/install?id=<id>`

List of firmware built-in sample plugins / one-click install to the card (clock, weather).

## WiFi

### `GET /api/scan`

Blocks for 2–4 seconds to scan once, returning networks sorted by signal strength:

```json
{"networks":[
  {"ssid":"CLOVES-COFFEE","rssi":-70,"auth":4},
  {"ssid":"iTV-hdQF","rssi":-70,"auth":4}
]}
```

`auth`: 0 open, 4/5 WPA2, 7 WPA3. Hidden networks do not appear.

### `POST /api/wifi?ssid=<ssid>&pass=<pass>`

Save and connect. An empty password string means an open network. The hotspot always stays up.

## Other

### `POST /api/gesture?event=next|prev|shake|tap`

Simulate a gesture: `next`/`prev` switch apps, `shake`/`tap` are dispatched directly to the current app.

```bash
curl -X POST "http://192.168.4.1/api/gesture?event=next"
```

### `POST /api/ota`

The request body is the firmware `.bin` (the `mochi.bin` produced by `idf.py build`). It is written to the OTA partition, verified, then the device reboots automatically.

```bash
curl -X POST --data-binary @build/mochi.bin http://192.168.4.1/api/ota
```

### `GET /`

Returns the embedded web page (gzip + `Cache-Control: no-store`).

## A typical flow with curl

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
