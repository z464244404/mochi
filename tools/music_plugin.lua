-- 音乐副屏：电脑上的 music_sync.py 把系统正在播放的歌推过来
-- 数据走内存（music.get()），不写 SD 卡，所以可以每 2 秒推一次
-- 版式严格按 music-bg/README.md：
--   封面 圆心(240,150) 直径240 / 曲名 基线310 29px / 歌手 基线344 22px
--   进度条 x100→380 y378 / 时间 基线404 22px，左贴100、右贴380
-- ui.text 的 y 是「竖直中心」：29px 基线 = y+11，22px 基线 = y+8.5
local t = 0
local title, artist = "", ""   -- 初值必须是空串，nil 会让 ui.measure 报错
local pos, dur, playing, ts
local angle = 0
local SPEED = 25           -- 播放时转速：度/秒（360/25 ≈ 14.4 秒一圈）
local bgname = "music-vinyl"

local GREEN = 0x5BE0BD
local GREY = 0x5A6270
local TRACK = 0x3E444E

local TITLE_Y = 299
local ARTIST_Y = 336
local TIME_Y = 396
local BAR_X0, BAR_X1, BAR_Y = 100, 380, 378

local function read()
  local m = music.get()
  if not m then return false end
  title = m.title or ""
  artist = m.artist or ""
  pos = tonumber(m.pos) or 0
  dur = tonumber(m.dur) or 0
  playing = m.playing and true or false
  ts = tonumber(m.ts) or 0
  return title ~= ""
end

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
  local h = math.floor(sec / 3600)
  if h > 0 then
    return string.format("%d:%02d:%02d", h, math.floor(sec % 3600 / 60), math.floor(sec % 60))
  end
  return string.format("%d:%02d", math.floor(sec / 60), math.floor(sec % 60))
end

function on_start()
  local style = app.get("bg")
  if style == nil or style == "" then style = "vinyl" end
  bgname = "music-" .. style
  read()
end

local function draw_idle()
  ui.ring(240, 150, 118, 2, 0x2C3542)
  ui.text("等待播放", 240, 336, 29, 0x6B7785)
  ui.text("电脑上运行 music_sync.py", 240, 372, 22, 0x4E5A68)
end

function on_draw(dt)
  t = t + dt
  read()

  if not ui.bg(bgname) then ui.clear(0x0B0D12) end

  if title == "" then
    draw_idle()
    return
  end

  if playing then
    angle = angle + dt * SPEED
    if angle >= 360 then angle = angle - 360 end
  end

  if not ui.cover(240, 150, 240, angle) then
    ui.circle(240, 150, 120, 0x1A1F27)
  end

  -- 唱片中心轴孔
  ui.circle(240, 150, 9, 0x14171C)
  ui.ring(240, 150, 9, 1, 0x2C3542)

  ui.text(clip(title, 29, 360), 240, TITLE_Y, 29, 0xF2F6FA)
  if artist ~= "" then
    ui.text(clip(artist, 22, 360), 240, ARTIST_Y, 22, 0x9AA8B8)
  end

  -- 进度：直接用电脑推过来的 pos，不做本地插值
  -- （插值要依赖两端时钟一致，实测会往回跳；宁可一秒一跳也别跳错）
  local elapsed = pos
  if dur > 0 and elapsed > dur then elapsed = dur end

  local col = playing and GREEN or GREY
  ui.round_rect(BAR_X0, BAR_Y - 2, BAR_X1 - BAR_X0, 4, 2, TRACK)
  if dur > 0 then
    local w = math.floor((BAR_X1 - BAR_X0) * elapsed / dur)
    if w > 1 then
      ui.round_rect(BAR_X0, BAR_Y - 2, w, 4, 2, col)
    end
    ui.circle(BAR_X0 + w, BAR_Y, 7, col)
  end

  local l = mmss(elapsed)
  local r = dur > 0 and mmss(dur) or "--:--"
  ui.text(l, BAR_X0 + ui.measure(l, 22) / 2, TIME_Y, 22, 0x7E8C9C)
  ui.text(r, BAR_X1 - ui.measure(r, 22) / 2, TIME_Y, 22, 0x7E8C9C)
end
