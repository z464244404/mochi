-- 时钟：cute / tech / minimal 三种样式，参数来自插件设置
local style = "cute"
local show_sec = true

local CN = {"日", "一", "二", "三", "四", "五", "六"}
local EN = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"}

-- "#RRGGBB" -> 0xRRGGBB
local function hexc(s, def)
  if type(s) ~= "string" then return def end
  local n = tonumber((s:gsub("#", "")), 16)
  if n then return n end
  return def
end

function on_start()
  style = app.get("style") or "cute"
  show_sec = (app.get("seconds") or "on") ~= "off"
end

local function draw_cute(now, hh)
  ui.clear(0xFFF6EC)
  ui.circle(240, 96, 13, 0xF7B267)
  for i = 0, 7 do
    local a = i * 0.7854
    ui.line(240 + math.cos(a) * 21, 96 + math.sin(a) * 21,
            240 + math.cos(a) * 27, 96 + math.sin(a) * 27, 3, 0xF7B267)
  end
  if show_sec then
    ui.arc(240, 240, 210, 5, -90, 270, 0xF3DFCB)
    ui.arc(240, 240, 210, 5, -90, -90 + now.sec * 6, 0xF09A5B)
  end
  ui.text(hh, 240, 236, 106, 0x3D2B1F)
  ui.round_rect(118, 306, 244, 48, 24, 0xF1D7BE)
  ui.text(string.format("%d月%d日 周%s", now.month, now.day, CN[(now.wday % 7) + 1]),
          240, 330, 29, 0x6B4A36)
end

local function draw_tech(now, hh)
  ui.clear(0x05090E)
  for i = 0, 59 do
    local a = math.rad(i * 6 - 90)
    local major = (i % 5 == 0)
    local r0 = major and 192 or 199
    local c = major and 0x2E6B7E or 0x16323C
    local th = major and 3 or 2
    ui.line(240 + math.cos(a) * r0, 240 + math.sin(a) * r0,
            240 + math.cos(a) * 206, 240 + math.sin(a) * 206, th, c)
  end
  if show_sec then
    ui.arc(240, 240, 214, 3, -90, -90 + now.sec * 6, 0x35E0C0)
    local a = math.rad(now.sec * 6 - 90)
    ui.circle(240 + math.cos(a) * 214, 240 + math.sin(a) * 214, 5, 0x35E0C0)
  end
  ui.text(hh, 240, 236, 106, 0xE6FBFF)
  ui.text(string.format("%04d.%02d.%02d  %s", now.year, now.month, now.day,
           EN[(now.wday % 7) + 1]), 240, 342, 29, 0x3E7A8C)
  ui.circle(216, 402, 3, 0x35E0C0)
  ui.circle(228, 402, 3, 0x1B3A46)
  ui.circle(240, 402, 3, 0x1B3A46)
end

local function draw_minimal(now)
  local bg = hexc(app.get("bg"), 0x0F1115)
  local fg = hexc(app.get("hand"), 0xE8F4F2)
  local sc = hexc(app.get("sec_hand"), 0x5BE0BD)
  ui.clear(bg)
  -- 12 个刻度
  for i = 0, 11 do
    local a = math.rad(i * 30 - 90)
    local major = (i % 3 == 0)
    local r0 = major and 184 or 193
    local th = major and 6 or 3
    ui.line(240 + math.cos(a) * r0, 240 + math.sin(a) * r0,
            240 + math.cos(a) * 206, 240 + math.sin(a) * 206, th, fg)
  end
  local hh = (now.hour % 12) + now.min / 60
  local ma = math.rad(hh * 30 - 90)
  local na = math.rad(now.min * 6 - 90)
  local sa = math.rad(now.sec * 6 - 90)
  -- 时针 / 分针 / 秒针
  ui.line(240 - math.cos(ma) * 18, 240 - math.sin(ma) * 18,
          240 + math.cos(ma) * 108, 240 + math.sin(ma) * 108, 11, fg)
  ui.line(240 - math.cos(na) * 22, 240 - math.sin(na) * 22,
          240 + math.cos(na) * 152, 240 + math.sin(na) * 152, 7, fg)
  if show_sec then
    ui.line(240 - math.cos(sa) * 26, 240 - math.sin(sa) * 26,
            240 + math.cos(sa) * 168, 240 + math.sin(sa) * 168, 3, sc)
    ui.circle(240, 240, 8, sc)
  end
  ui.circle(240, 240, 7, bg)
  ui.circle(240, 240, 4, fg)
end

function on_draw(dt)
  local now = time.now()
  if style == "minimal" then
    draw_minimal(now)
    return
  end
  local hh = string.format("%02d:%02d", now.hour, now.min)
  if style == "tech" then draw_tech(now, hh) else draw_cute(now, hh) end
end
