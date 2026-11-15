[中文](architecture.md) · **English**

# Mochi Architecture Design

> A 480×480 round screen + ESP32-S3, made into a little thing that is "alive" on your desk:
> by default it is a face that blinks, follows your tilt with its gaze, and smiles when petted; shake it to switch to the next app (clock, weather, pomodoro…),
> apps are uploaded and installed from the web UI, with no firmware reflash.

This document is the design of the **platform layer**. M1 (current) implements only the kernel: renderer + face + clock + gestures + RTC.
The plugin runtime (Lua) and the web backend come in M2.

---

## 1. Why Layer It This Way

The essence of AWTRIX is not that 8×32 matrix but "small screen + extensible apps + web configuration".
Moving to a 480×480 round screen, the biggest change is **450KB per frame** (480×480×2B):

- A full-screen repaint must write straight into the PSRAM framebuffer; no more copying data around;
- A plugin must never touch pixels itself, otherwise a single `for` loop is enough to kill the frame rate;
- So a plugin only describes **what** to draw, and the C layer does the rasterization.

```
┌──────────────────────────────────────────────────────────┐
│ apps / plugins                                           │
│ builtin (C): face · clock  3rd-party (Lua, M2): weather… │
│ same callback set: enter / draw(gfx, dt) / event / leave │
├──────────────────────────────────────────────────────────┤
│ app manager   transition (fade out/in), event dispatch   │
├──────────────────────────────────────────────────────────┤
│ gfx           480×480 RGB565 software renderer, SDF AA   │
│               round_rect / circle / ellipse / arc / text │
├──────────────────────────────────────────────────────────┤
│ lcd           ST7701S + RGB565 parallel, 3 FB, zero-copy │
│ imu           QMI8658 50Hz → shake / tap / flip / tilt   │
│ rtc           PCF85063                                   │
└──────────────────────────────────────────────────────────┘
```

### Why Not LVGL

LVGL suits "widget + event" UIs; it does not suit a desk ornament that repaints the whole screen every frame and whose graphics are mostly geometric primitives:

- A full-screen 480×480 canvas in LVGL still goes through the object/refresh machinery — one extra layer of overhead;
- We only need 6 primitives + fonts, and our own renderer is ~600 lines: controllable, testable, fast to start;
- To later give plugins a "sandboxed canvas", our own imperative API is safer than exposing the LVGL object model.

### Why Plugins Use Lua Instead of Native Code

| Approach | Installable from the web UI | Risk / cost |
|---|---|---|
| Native `.so` | ✗ | Xtensa has no MMU isolation and IDF does not support `dlopen`; every new plugin would mean reflashing the firmware |
| MicroPython | ✓ | Slow startup, large RAM footprint, complicated glue to the C renderer |
| WASM | ✓ | Interpreted execution is 5–10× slower; heavy toolchain |
| **Lua 5.4** | ✓ | ~250KB Flash, pure C, easy to sandbox, enough performance for logic (it runs logic only, not pixels) |

---

## 2. Plugin Package Format

A plugin is just a zip (extension `.mochi`, a zip underneath):

```
weather.mochi
├─ manifest.json
├─ main.lua
└─ assets/
   ├─ icon.png        # 96×96 icon shown in the list
   └─ sun.png
```

`manifest.json`:

```json
{
  "id": "com.mochi.weather",
  "name": "Weather",
  "version": "1.0.0",
  "api": 1,
  "author": "mochi",
  "entry": "main.lua",
  "icon": "assets/icon.png",
  "permissions": ["net", "storage"],
  "settings": [
    { "key": "city", "type": "string", "label": "City", "default": "Shenzhen" },
    { "key": "unit", "type": "enum", "label": "Unit", "options": ["C", "F"], "default": "C" }
  ]
}
```

Rules:

- `id` is a reverse domain name, globally unique, and also the directory name `/<plugins>/<id>/`;
- A mismatched `api` rejects the installation outright (a firmware upgrade will not silently break plugins);
- Installation is atomic: unzip into `<id>.tmp`, verify the manifest + entry file + total size, then rename;
- A single plugin is capped at 2MB (assets counted separately); when total space runs out the web UI prompts the user to delete plugins;
- Uninstall = delete the directory + delete `/data/<id>/`.

### Permissions

| Permission | Capability |
|---|---|
| `net` | `http.get/post`, https only, configurable domain allowlist |
| `storage` | read/write its own `/data/<id>/`, quota 64KB |
| `notify` | pop a toast into other apps (M3) |

No permissions by default. There is no `fs`, `os`, `io`, `package.loadlib`.

---

## 3. Lua API (M2)

```lua
local ui   = require("ui")     -- canvas
local time = require("time")
local http = require("http")
local app  = require("app")    -- settings, logging, navigation

function on_start()
  app.log("weather started")
  fetch()
  app.every(600000, fetch)     -- 10 minutes
end

function on_draw(c, dt)        -- every frame; c is the 480×480 canvas
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
      state = json.decode(res.body)   -- the json library ships with the firmware
    end
  end)
end
```

### Canvas Methods (v1)

| Method | Description |
|---|---|
| `clear(color)` | fill the whole screen |
| `rect(x, y, w, h, color)` / `round_rect(..., r)` | top-left coordinates |
| `circle(cx, cy, r, color)` / `ellipse` | |
| `ring(cx, cy, r, thick, color)` / `arc(cx, cy, r, thick, a0, a1, color)` | degrees, 0° = right, clockwise |
| `line(x0, y0, x1, y1, thick, color)` | |
| `text(str, x, y, opts)` | `opts.size` supports only the two built-in sizes, 29 / 106 (M3 adds more sizes); `align` = left/center/right |
| `image(name, x, y, w, h)` | `name` is a file name inside the package's `assets/`, decoded and cached in PSRAM |

Colors accept `"#rrggbb"` and `"#aarrggbb"`.

### Limits (sandbox)

| Item | Limit | What happens when exceeded |
|---|---|---|
| Lua time per frame | 6ms | skip drawing this frame, record one warning |
| Instructions per callback | 200k (`lua_sethook`) | abort the callback, caught by `pcall` |
| Memory per plugin | 256KB (accounted by a custom allocator) | allocation fails → Lua raises an error |
| Network callback | 10s timeout | error code returned to the plugin |
| Consecutive errors | 5 | the plugin is disabled automatically, the web UI shows the reason |

A plugin crash must not affect the main loop: every entry point goes through `lua_pcall`, and when the Lua stack/state is corrupted the whole plugin is unloaded and reloaded.

---

## 4. Gestures

The QMI8658 samples at 50Hz; a low-pass filter first estimates gravity, and the difference then yields linear acceleration.

| Gesture | Criterion | Action |
|---|---|---|
| Shake | ≥4 linear-acceleration spikes >0.45g within 450ms | next app (1.2s cooldown) |
| Tap | a single >1.1g hit after 300ms of quiet, not part of a multi-tap | the current app receives `tap` |
| Double tap | two taps <420ms apart | the face goes dizzy (`dizzy`) |
| Flip face down | gravity z < -0.55g for 700ms | screen off |
| Flip back | z > 0.55g for 400ms | screen on |
| Tilt | roll / pitch | the face's gaze follows (±13px) |

All thresholds sit at the top of `imu.c` and are tuned on the real device. If the left/right or front/back direction is inverted, just change the sign of `s_roll` / `s_pitch`.

**Why shake switches apps directly**: it is the most intuitive "give me another one" gesture for users. The face's "dizzy" reaction lives on double tap,
so the two do not compete for the same gesture.

---

## 5. Web Backend (M2)

The device runs AP + STA concurrently: before Wi-Fi is provisioned it is a `Mochi-XXXX` hotspot, and after provisioning it also joins the home Wi-Fi.

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | firmware version, memory, frame rate, current app, IP |
| GET | `/api/apps` | list of installed plugins (including built-in) |
| POST | `/api/apps` | upload a `.mochi` (streamed to Flash, validated while receiving) |
| DELETE | `/api/apps/<id>` | uninstall |
| POST | `/api/apps/<id>/enable` | enable/disable |
| GET/POST | `/api/apps/<id>/settings` | read/write plugin settings (the form is generated from the manifest) |
| POST | `/api/order` | reorder (determines the rotation order for shake) |
| POST | `/api/gesture` | inject a gesture, for debugging (`{"event":"shake"}`) |
| GET | `/api/preview` | MJPEG live view (5fps; see in the browser what the device is displaying) |
| POST | `/api/ota` | firmware update |
| POST | `/api/wifi` | save the home Wi-Fi |

The frontend still takes the old route of "single inlined file + gzip into the firmware" (see smart-emblem) and depends on no external CDN.

---

## 6. Storage

Allocation of the 16MB Flash (`partitions.csv`):

```
0x000000  bootloader
0x009000  nvs        24KB    config
0x00f000  otadata     8KB
0x011000  phy_init    4KB
0x020000  ota_0       3MB    firmware (Lua + fonts + built-in apps)
0x320000  ota_1       3MB
0x620000  assets    9.875MB  littlefs: plugins, images, fonts, logs
```

The firmware is currently ~1.1MB (fonts 113KB), about 1.4MB with Lua added, so the 3MB partition is enough.

---

## 7. Performance Budget (480×480 / 30Hz)

The panel PCLK is 18MHz and a full-screen refresh is about 30Hz, so the render target is 30fps (33ms/frame).

| Item | Estimate |
|---|---|
| Background (per-row gradient fill, 230k px) | ~2ms |
| Body (SDF + gradient, 87k px, sqrt only at the edges) | ~6ms |
| Outer glow, two layers | ~3ms |
| Eyes / mouth / blush | <1ms |
| Clock digits (5 glyphs × 8.5k px) | ~2ms |
| **Total** | **~14ms** |

Optimizations: interior SDF pixels skip the square root (`d < -1` means full coverage directly), gradients are computed per row, and tick marks use line segments instead of per-pixel angle tests.

---

## 8. Milestones

| | Content | Status |
|---|---|---|
| M1 | renderer + face + clock + shake/tap/flip + RTC | code complete, pending bring-up on hardware |
| M2 | Lua runtime + plugin install/uninstall + web backend + NTP | to do |
| M3 | plugin marketplace (static index.json), more font sizes/icons, toast, pomodoro/weather samples | to do |
| M4 | desktop simulator (SDL/Lua, same API), themes, low power | to do |

---

## 9. Not Decided Yet

- Plugin signing: whether to verify ed25519 signatures (tamper protection, but likely too much hassle for a personal project) — not for now, manifest validation is enough.
- Chinese font subset: 138 glyphs are built in today; M2 needs either fonts shipped inside each plugin package or the full 3500 glyphs (about 2MB).
- Whether to support MQTT / Home Assistant (AWTRIX users care a lot about this) — evaluate in M3.
