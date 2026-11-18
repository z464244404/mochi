-- 电脑状态：CPU 仪表环 + 内存 / 音量 / 网速 / 开机时长
-- 数据由电脑上的 tools/pc_monitor.py 每秒推过来（纯内存，不写卡）
local cpu, mem, up, down, vol, mute, host, up_sec = 0, 0, 0, 0, 0, false, "", 0
local has = false

local CPX, CPY, CR = 240, 190, 126
local A0, SWEEP = 140, 260      -- 弧从 140° 顺时针扫 260°

local function read()
  local s = sys.get()
  if not s then
    has = false
    return false
  end
  has = true
  host = s.host or ""
  cpu = tonumber(s.cpu) or 0
  mem = tonumber(s.mem) or 0
  up = tonumber(s.up) or 0
  down = tonumber(s.down) or 0
  vol = tonumber(s.vol) or 0
  mute = s.mute and true or false
  up_sec = tonumber(s.up_sec) or 0
  return true
end

local function speed(v)
  if v >= 10 then return string.format("%.0f", v) end
  if v >= 1 then return string.format("%.1f", v) end
  return string.format("%.2f", v)
end

local function level_color(p)
  if p >= 85 then return 0xFF5C5C end
  if p >= 60 then return 0xFF9F43 end
  return 0x5BE0BD
end

local function fmt_uptime(sec)
  local d = math.floor(sec / 86400)
  local h = math.floor(sec % 86400 / 3600)
  local m = math.floor(sec % 3600 / 60)
  if d > 0 then return string.format("%d 天 %d 时", d, h) end
  if h > 0 then return string.format("%d 时 %d 分", h, m) end
  return string.format("%d 分", m)
end

function on_start()
  read()
end

local function draw_idle()
  ui.ring(240, 196, CR, 2, 0x2C3542)
  ui.text("等待电脑数据", 240, 320, 29, 0x6B7785)
  ui.text("电脑上运行 pc_monitor.py", 240, 362, 22, 0x4E5A68)
end

function on_draw(dt)
  read()
  ui.clear(0x090B10)

  if not has then
    draw_idle()
    return
  end

  -- CPU 仪表环
  local col = level_color(cpu)
  ui.arc(CPX, CPY, CR, 12, A0, A0 + SWEEP, 0x222A36)
  if cpu > 0.5 then
    ui.arc(CPX, CPY, CR, 12, A0, A0 + SWEEP * math.min(cpu, 100) / 100, col)
  end

  -- 中央大数字
  local txt = string.format("%d", math.floor(cpu + 0.5))
  local w = ui.measure(txt, 106)
  ui.text(txt, CPX - 10, CPY - 2, 106, col)
  ui.text("%", CPX - 10 + w * 0.5 + 20, CPY + 30, 29, 0x7E8C9C)
  ui.text("CPU", CPX, CPY + 72, 22, 0x6B7785)

  -- 主机名
  if host ~= "" then
    ui.text(host, 240, 34, 22, 0x8A96A4)
  end

  -- 音量（左） / 内存（右）
  local vtxt
  if mute then
    vtxt = "音量 静音"
  else
    vtxt = string.format("音量 %.0f%%", vol)
  end
  ui.text(vtxt, 116, 352, 22, 0xB9C6D4)
  ui.text(string.format("内存 %.0f%%", mem), 364, 352, 22, level_color(mem))

  -- 网速
  ui.text("↑ " .. speed(up) .. " M/s", 118, 392, 22, 0x54B8FF)
  ui.text("↓ " .. speed(down) .. " M/s", 362, 392, 22, 0x54B8FF)

  -- 开机时长
  ui.text("开机 " .. fmt_uptime(up_sec), 240, 428, 22, 0x5E6B7A)
end
