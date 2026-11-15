[中文](user-guide.md) · **English**

# User Guide

For people who just got the board; no code involved. To write plugins, see [plugin-guide.en.md](plugin-guide.en.md).

- [1. First boot](#1-first-boot)
- [2. The four screens](#2-the-four-screens)
- [3. Gestures](#3-gestures)
- [4. Web console](#4-web-console)
- [5. DeepSeek balance](#5-deepseek-balance)
- [6. FAQ](#6-faq)

---

## 1. First boot

1. Insert the TF card (**required**: all assets and plugins live on the card), then supply power over USB.
2. The screen goes black for a moment (panel init), then enters the **network screen**:
   ```
       WIFI NOT CONNECTED
          JOIN HOTSPOT
           Mochi-XXXX        ← 后四位是 MAC
         PASS mochi1234
            THEN OPEN
        http://192.168.4.1
   ```
3. Connect your phone to this hotspot (password `mochi1234`) and open **http://192.168.4.1** in a browser.
4. On the page, go to "WiFi" → Scan → pick your 2.4G SSID → enter the password → Connect.
5. Once connected the screen shows the IP assigned by your router (e.g. `192.168.4.102`), and a few seconds later it enters the pet automatically.
   After that, **switch your phone back to your home WiFi**; the same IP still opens the console.

> **2.4G only.** A 5G SSID may show up in a scan, but it will not connect.

## 2. The four screens

Shake to cycle in order: **network → pet → clock → weather → (other plugins you installed)**.

### Network screen
- Not connected: hotspot info + `http://192.168.4.1`
- Connected: `CONNECTED` + local IP + `http://<ip>`
- On failure the bottom shows `ERR <reason code>` (15/204/205 = wrong password, 201 = not found)

### Pet
- Three animations switch by DeepSeek balance state: **balance dropping → typing**, **unchanged for 5 minutes → eating**, **unchanged for 30 minutes → sleeping**
- Bottom overlay: "Balance" on the left, "Today" on the right, "Peak/Off-peak" in the middle
- Without an API key it shows `SET API KEY`

### Clock
Three styles, switched under "Plugins → Clock → Details":

| Style | Look |
|---|---|
| `cute` | Cream background, little sun, large time, date pill (with weekday), seconds progress ring |
| `tech` | Dark, 60 tick marks, seconds arc + cursor, `2026.11.12 THU` |
| `minimal` | Hour/minute/second hands + 12 tick marks only; background/hand/second-hand colors are all configurable |

### Weather
- Place name at the top (the "Place name" setting in the plugin config)
- Large temperature + ℃, weather word (clear/partly cloudy/overcast/light rain/moderate rain/heavy rain/rainstorm/thunderstorm/snow/fog)
- The large icon is meteocons' **original animation**, and the background changes with the weather
- Three days below: date / icon / high (orange) / low (blue)
- Refreshes every 10 minutes; shows "Waiting for network" when offline, and with a cache it shows the cache directly without an error

## 3. Gestures

| Action | Effect |
|---|---|
| Shake | Next app |
| Press the BOOT button | Next app (no shake needed) |
| Place the screen face down | Screen off (sleep); flip it back to wake |
| Tap (on the pet screen) | No special behavior yet, reserved for later |

## 4. Web console

Address: `http://192.168.4.1` on the hotspot, or the IP shown on the device on your home WiFi.

### Status card
Firmware version, current app, frame rate, free PSRAM, hotspot name, **hotspot address** and **LAN address**, WiFi state.

### Plugins
- Each row is one plugin: `builtin`/on-card plugin; the check on the right is the **enable toggle** (disabled plugins are skipped in rotation)
- Click a name for the **details page**: it shows the manifest info and auto-generates a form from `settings`
  - Supports text, number, dropdown, color picker
  - Click "Save settings" → written to the card immediately and the plugin reloads, no reboot
- "Delete" only works on Lua plugins on the card; builtins cannot be deleted

### Sample plugins
The firmware ships two samples; clicking "Install" writes them to the card:

- **Clock** (cute / tech / minimal)
- **Weather** (current + three days, Open-Meteo, no signup)

Builtins update automatically by version: after a firmware upgrade, the sample plugins on the card are reinstalled at the new version on boot.
If you edited their code yourself it will be overwritten, so back it up first if that matters.

### WiFi
- **Scan nearby WiFi**: lists SSID + signal strength; click one to fill the input field
- Connection state refreshes live; failures report a reason in Chinese
- The hotspot is always kept; connecting to home WiFi does not affect it

### Firmware (OTA)
Upload `mochi.bin` to upgrade, no serial cable needed. The device reboots automatically after upgrading.

## 5. DeepSeek balance

The data strip at the bottom of the pet comes from the official DeepSeek API and requires your own API key.

**How to set it**: web → "Pet" → Details → `DeepSeek API Key` → Save.

| Display | Meaning |
|---|---|
| `Balance 12.34` | Total account balance (CNY) |
| `Today 0.56` | Spent so far today (the sum of every balance decrease) |
| `Off-peak` / `Peak` | Whether the current time is discounted: **weekdays 09:00–12:00 and 14:00–18:00 (Beijing time) are peak**; everything else (including weekends) is off-peak |

Notes:

- The balance is polled every 5 minutes; a failed poll does not affect the display (the previous value is kept)
- The key is stored in plaintext at `/sdcard/plugins/pet/settings.json` and is **never compiled into the firmware**
- There is no official "today's usage" endpoint; this is estimated by summing balance decreases. Spending and top-ups that happen while the device is off cannot be accounted for
- There is no built-in public holiday calendar, so holidays are still treated as weekdays for peak/off-peak

## 6. FAQ

**The screen shows `NO PLUGIN` / `PLUGIN MISSING`**
`main.lua` is missing from the plugin directory. When entering that screen the firmware reinstalls it once from the built-in copy;
if that does not help, delete it in the web console and install it again.

**Weather stays on "Waiting for network"**
Check whether the network screen has an IP. If it does, the router is most likely blocking DNS; the serial log will show a `getaddrinfo` error.

**The screen shows `NO ASSET`**
`pet.mjp` is not on the TF card. Run a full flash again (`idf.py flash`) and the device syncs the assets to the card automatically.

**I changed settings but nothing happened**
The plugin reloads after saving on the details page, but it still has to draw a new frame. If it still looks old, switch to another screen and back.

**What happens if I pull the card**
Assets and plugins are gone, the screen shows `NO ASSET`, and the web console cannot read the plugin list either. Put it back and reboot.

**How do I restore factory settings**
Delete all plugins in the web console and flash the firmware again; the WiFi/settings in NVS are preserved.
To clear them, run `idf.py -p COM10 erase-flash` (this erases the firmware as well).
