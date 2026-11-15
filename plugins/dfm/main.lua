-- 三角洲行动 玩家数据（只负责显示）
-- 数据由电脑上的 tools/df_sync.py 拉腾讯接口后推过来：POST /api/dfm
local t = 0
local last_reload = -1
local nick, rank, liquid, noncurrent, gain, ts

local function fmt(n)
  n = tonumber(n) or 0
  local a = math.abs(n)
  if a >= 100000000 then return string.format("%.2f亿", n / 100000000) end
  if a >= 10000 then return string.format("%.1f万", n / 10000) end
  return string.format("%d", n)
end

local function signed(n)
  local v = tonumber(n) or 0
  if v > 0 then return "+" .. fmt(v) end
  return fmt(v)
end

local function load()
  nick = store.get("nick") or ""
  rank = tonumber(store.get("rank") or "0") or 0
  liquid = tonumber(store.get("liquid") or "0") or 0
  noncurrent = tonumber(store.get("noncur") or "0") or 0
  gain = tonumber(store.get("gain") or "0") or 0
  ts = tonumber(store.get("ts") or "0") or 0
end

function on_start()
  load()
end

local function draw_wait()
  ui.text("等待数据", 240, 210, 29, 0xffc86b)
  ui.text("电脑上运行 df_sync.py", 240, 256, 22, 0x6b7785)
  ui.text("会把数据推过来", 240, 288, 22, 0x6b7785)
end

function on_draw(dt)
  t = t + dt
  -- 每 2 秒重新读一次卡上的 data.json（脚本随时可能推送）
  if t - last_reload > 2 then
    last_reload = t
    store.reload()
    load()
  end

  ui.clear(0x0b0d12)
  if not nick or nick == "" then
    draw_wait()
    return
  end

  ui.text(nick, 240, 54, 22, 0x8fa0b4)
  ui.text(tostring(rank), 240, 148, 106, 0xf4f7f8)
  ui.text("烽火段位", 240, 214, 22, 0x8fa0b4)

  ui.text("流动", 132, 268, 22, 0x8fa0b4)
  ui.text("非流动", 348, 268, 22, 0x8fa0b4)
  ui.text(fmt(liquid), 132, 302, 29, 0xdfe8f0)
  ui.text(fmt(noncurrent), 348, 302, 29, 0xdfe8f0)

  ui.text("最近收益", 240, 356, 22, 0x8fa0b4)
  if gain == 0 then
    ui.text("0", 240, 392, 29, 0x8fa0b4)
  elseif gain > 0 then
    ui.text(signed(gain) .. " ↑", 240, 392, 29, 0xff5a5a)
  else
    ui.text(signed(gain) .. " ↓", 240, 392, 29, 0x35d07f)
  end

  -- 数据陈旧提示
  local age = time.epoch() - ts
  if ts > 0 and age > 1800 then
    ui.text("数据 " .. string.format("%d", math.floor(age / 60)) .. " 分钟前",
            240, 440, 22, 0xff9f43)
  end
end
