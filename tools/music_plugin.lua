-- 音乐副屏：电脑上的 music_sync.py 把系统正在播放的歌推过来
-- 封面是 240x240 的 RGB565 裸数据（/sdcard/plugins/music/cover.565），设备不解码
local t = 0
local last_reload = -1
local title, artist, pos, dur, playing, ts

local function text_or(v, def)
  if v == nil or v == "" then return def end
  return v
end

local function load()
  title = store.get("title") or ""
  artist = store.get("artist") or ""
  pos = tonumber(store.get("pos") or "0") or 0
  dur = tonumber(store.get("dur") or "0") or 0
  playing = (store.get("playing") or "0") == "1"
  ts = tonumber(store.get("ts") or "0") or 0
end

-- 按宽度裁剪标题，超长加省略号（字体里没有 …，用 . 代替）
local function clip(s, size, maxw)
  if ui.measure(s, size) <= maxw then return s end
  local out = s
  while #out > 1 and ui.measure(out .. "..", size) > maxw do
    out = out:sub(1, #out - 1)
  end
  return out .. ".."
end

local function mmss(sec)
  if sec < 0 then sec = 0 end
  return string.format("%d:%02d", math.floor(sec / 60), math.floor(sec % 60))
end

function on_start()
  load()
end

local function draw_idle()
  ui.circle(240, 176, 86, 0x1A1F27)
  ui.ring(240, 176, 60, 3, 0x2C3542)
  ui.text("等待播放", 240, 350, 29, 0x6b7785)
  ui.text("电脑上运行 music_sync.py", 240, 392, 22, 0x4E5A68)
end

function on_draw(dt)
  t = t + dt
  if t - last_reload > 2 then
    last_reload = t
    store.reload()
    load()
  end

  ui.clear(0x0B0D12)

  if title == "" then
    draw_idle()
    return
  end

  -- 封面（电脑推了才有）
  if not ui.cover(240, 176, 240) then
    ui.circle(240, 176, 120, 0x1A1F27)
    ui.text("?", 240, 176, 106, 0x2C3542)
  end

  ui.text(clip(title, 29, 400), 240, 336, 29, 0xF2F6FA)
  if artist ~= "" then
    ui.text(clip(artist, 22, 400), 240, 372, 22, 0x7E8C9C)
  end

  -- 进度：本地按时间插值，避免每帧都等推送
  local elapsed = pos
  if playing and ts > 0 then
    local now = time.epoch()
    if now > 1000000000 and now > ts then elapsed = pos + (now - ts) end
  end
  if dur > 0 and elapsed > dur then elapsed = dur end

  local x0, x1, y = 110, 370, 404
  ui.round_rect(x0, y - 2, x1 - x0, 5, 2, 0x232A34)
  if dur > 0 then
    local w = math.floor((x1 - x0) * elapsed / dur)
    if w > 0 then ui.round_rect(x0, y - 2, w, 5, 2, playing and 0x35D07F or 0x7E8C9C) end
  end

  ui.text(mmss(elapsed), x0 - 26, 432, 22, 0x6b7785)
  ui.text(dur > 0 and mmss(dur) or "--:--", x1 + 26, 432, 22, 0x6b7785)
end
