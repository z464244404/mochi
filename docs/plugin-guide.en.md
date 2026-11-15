[中文](plugin-guide.md) · **English**

# Plugin Development Guide

A plugin is just a directory on the TF card containing two files. When you shake the device
onto this screen, the firmware starts a Lua state machine to run it and calls your `on_draw()`
every frame; you can only draw through `ui.*`.

- [1. Minimal Plugin](#1-minimal-plugin)
- [2. manifest.json](#2-manifestjson)
- [3. Settings](#3-settings)
- [4. Lifecycle](#4-lifecycle)
- [5. Lua API Reference](#5-lua-api-reference)
- [6. Coordinates, Colors, Fonts](#6-coordinates-colors-fonts)
- [7. Runtime and Limits](#7-runtime-and-limits)
- [8. Debugging](#8-debugging)
- [9. Full Example: Clock](#9-full-example-clock)
- [10. Installation and Distribution](#10-installation-and-distribution)

---

## 1. Minimal Plugin

```
/sdcard/plugins/hello/
├── manifest.json
└── main.lua
```

`manifest.json`:

```json
{
  "id": "hello",
  "name": "Hello",
  "version": "1.0.0",
  "api": 1,
  "author": "you"
}
```

`main.lua`:

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

Install it on the card (upload from the web page, or copy the directory straight over) → shake
the device until this screen comes up and it runs.

## 2. manifest.json

| Field | Required | Description |
|---|---|---|
| `id` | ✓ | Directory name; use only `a-z 0-9 . _ -`, max 31 characters |
| `name` | ✓ | Name shown on the web page (may be Chinese) |
| `version` | | String; built-in plugins use it to decide whether an update is needed |
| `api` | | Currently fixed at `1` |
| `author` | | Shown on the detail page |
| `settings` | | Array of settings, see below |

## 3. Settings

Declare them in the manifest and the web page will **auto-generate a form**; on save they are
written to `/sdcard/plugins/<id>/settings.json`, and the plugin reads them with `app.get("key")`.

```json
"settings": [
  {"key": "city",  "label": "地名",     "type": "text",   "default": "深圳"},
  {"key": "lat",   "label": "纬度",     "type": "number", "default": "22.54"},
  {"key": "unit",  "label": "单位",     "type": "select", "options": ["c", "f"], "default": "c"},
  {"key": "bg",    "label": "背景色",   "type": "color",  "default": "#0F1115"}
]
```

| type | Control | What `app.get()` returns |
|---|---|---|
| `text` | Single-line input | string |
| `number` | Numeric input | string (**run tonumber on it yourself**) |
| `select` | Dropdown | string (one of the options) |
| `color` | Color picker | `"#rrggbb"` |

Notes:

- If the user has never saved, or saved an empty string, `app.get()` may return `nil` or `""`.
  In Lua `""` is truthy, so `v or "default"` **does not catch an empty string**; write:
  ```lua
  local function text_or(v, def) if v == nil or v == "" then return def end return v end
  ```
- Parse colors yourself: `tonumber((s:gsub("#", "")), 16)` (the parentheses are required, otherwise
  the second return value of `gsub` is taken as the base).

## 4. Lifecycle

| Function | When it is called |
|---|---|
| `on_start()` | On entering this screen (the Lua state is rebuilt every time, **globals are lost**) |
| `on_draw(dt)` | Every frame; `dt` is the number of seconds since the previous frame (float) |

- There is no `on_stop`; to persist anything, write to `store`.
- The whole screen is redrawn every frame, and there is no notion of a "dirty rectangle": call
  `ui.clear` or `ui.bg` first, then draw your content.
- The frame rate is capped at the panel's 43Hz, but a single frame's budget is only **about 23ms**
  (a full-screen fill/copy alone costs 15–18ms), so don't draw anything too heavy.

## 5. Lua API Reference

### ui.*

| Function | Description |
|---|---|
| `ui.clear(color)` | Fill the entire screen |
| `ui.bg(name)` | Draw a built-in weather background (480×480); returns `true` on success |
| `ui.rect(x, y, w, h, color)` | Rectangle; **x/y is the top-left corner** |
| `ui.round_rect(x, y, w, h, r, color)` | Rounded rectangle; x/y is the top-left corner, r is the corner radius |
| `ui.circle(cx, cy, r, color)` | Filled circle |
| `ui.ring(cx, cy, r, thick, color)` | Ring |
| `ui.arc(cx, cy, r, thick, a0, a1, color)` | Arc; **degrees, 0° = due right, clockwise** |
| `ui.line(x0, y0, x1, y1, thick, color)` | Line segment (with round caps) |
| `ui.text(s, x, y, size, color)` | Text; **x/y is the center of the text** |
| `ui.measure(s, size)` | Returns the pixel width of the text, for layout |
| `ui.icon(name, size, cx, cy)` | Draw a built-in weather icon; `size` is best at 120 or 64 |

Colors are `0xRRGGBB`; for translucency write `0xAARRGGBB` (for example `0x38FFFFFF` is 22% white).

### time.*

| Function | Returns |
|---|---|
| `time.now()` | `{year, mon, day, hour, min, sec, wday}`; `wday` 0 = Sunday |
| `time.epoch()` | Unix seconds (meaningful only after NTP sync) |

### app.*

| Function | Description |
|---|---|
| `app.get(key)` | Read this plugin's settings; `nil` if unset |

### http.*

```lua
http.get(url, function(status, body) ... end)
```

- **Asynchronous**: returns immediately, and the callback fires when the result arrives (the
  callback always runs on the drawing thread, so it is safe to call `ui.*`)
- `status` is the HTTP status code; 0 means it could not connect (DNS/timeout)
- `body` is a string; anything over 8KB is truncated
- Repeated requests for the same URL within 2 minutes **hit the cache directly** and are never
  actually sent out
- GET only; for APIs that need POST, wrap them yourself (forward it through an `app`, or use a
  different service)

### json.*

| Function | Description |
|---|---|
| `json.decode(s)` | JSON → table; returns `nil` on failure; arrays start at 1 |

### net.*

| Function | Description |
|---|---|
| `net.up()` | Whether WiFi is connected to the internet (has an IP) |

**Always check before sending a request**, otherwise DNS will fail in the first instant after boot:

```lua
if not net.up() then next_try = t + 3; return end
```

### store.*

| Function | Description |
|---|---|
| `store.get(key)` | Read this plugin's own small data (stored in `/sdcard/plugins/<id>/data.json`) |
| `store.set(key, value)` | Write (strings only, flushed to disk immediately) |

Use it to cache API results so you don't re-request every time the screen is entered. Switching
screens rebuilds the Lua state, but `store` lives on the card and is not lost.

### log

```lua
log("任意字符串")
```

Prints to the serial port (`lua: xxx` is visible in `idf.py monitor`).

## 6. Coordinates, Colors, Fonts

- The screen is **480×480**, center (240, 240), visible circular diameter 480
- Content near the four corners gets clipped by the circular edge, so work it out before placing
  anything: the usable half-width at `y` ≈ `sqrt(240² - (y-240)²)`
- There are only four font sizes:

| size | Font | Content |
|---|---|---|
| `>= 60` | 106px | **Digits, `.` and `:` only** |
| `45–59` | 56px | Digits, `.`, `:` |
| `25–44` | 29px | ASCII + built-in Chinese character table |
| `<= 24` | 22px | Digits, `℃ ℉ / - . :` |

### How Chinese Characters (CJK) Are Displayed

The firmware has roughly 220 common characters compiled in (time words, weather words, city
names); that is everything displayable by default. All other Chinese characters come from a
**TF card font pack**:

```
/sdcard/fonts/fontpack.bin     # 7543 个字形（ASCII + GB2312 全集），29px
```

Upload it once in the "Font Pack" section of the web admin page (about 5.8 MB, 10 seconds); once
installed, **any Chinese character can be displayed**, with no firmware change needed. To generate it:

```bash
python tools/pack_font.py      # -> release/fontpack.bin
```

Lookup order: **built-in font (by size) → card font pack**. Therefore:

- 29px Chinese prefers the built-in glyphs (fast), and only reads from the card for missing characters
- 22px Chinese falls through to the font pack, whose glyphs are the 29px ones — one size larger than
  expected, so watch the layout
- With no font pack on the card, missing characters still render as blank (same as before)
- The font pack caches 12 glyphs, so static text is basically read once; **very long Chinese text that
  changes every frame will drag the frame rate down**

## 7. Runtime and Limits

| Item | Value |
|---|---|
| Lua version | 5.4.7, **32-bit integers / single-precision floats** (`LUA_32BITS`) |
| Heap | In PSRAM, **hard cap 192KB**; exceeding it raises `not enough memory` |
| Instruction budget | 120,000 instructions per frame; runaway code is caught by pcall and shows an error screen |
| Available libraries | `base / string / table / math / utf8 / coroutine` |
| **Not available** | `io`, `os`, `package`, `debug` — no file I/O, no threads |
| Network | Only `http.get` (HTTPS uses the built-in certificate chain) |

## 8. Debugging

1. Watch the logs on the serial port: `idf.py -p COM10 monitor`
   ```
   lua: loading /sdcard/plugins/hello/main.lua (312 bytes)
   lua: plugin ready, heap 18/192 KB
   lua: hello 启动
   ```
2. Syntax/runtime errors are shown right on the screen (red `PLUGIN ERROR` + the error message),
   and the serial port also prints `load failed: ...`
3. To inspect a variable, use `log(tostring(x))` — `log(1)` raises an error in Lua, **you must pass a string**
4. No reboot needed after editing a plugin: switch to another screen and back, and it reloads

## 9. Full Example: Clock

This is a simplified version of the firmware's built-in clock, covering settings reads, time
formatting, and layout measurement:

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

For how to write a weather plugin, look at the one built into the firmware: after
"Example Plugins → Weather → Install" on the web page, open `/sdcard/plugins/weather/main.lua`
with a card reader — it shows the full use of `http.get` + `json.decode` + `store` caching.

## 10. Installation and Distribution

**Option 1: Web upload**

1. Open the admin page → "Install Plugin" → choose the files
2. **You must select both `manifest.json` and `main.lua`** (the file names cannot change)
3. Click upload → on success it appears in the list

**Option 2: Copy to the card**

Copy the whole directory to `/sdcard/plugins/<id>/` and reboot the device.

**Distribution**: just zip the two files and send them to someone; do not include
`settings.json` / `data.json` (that is the user's own data).
