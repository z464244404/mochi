-- 哔哩哔哩粉丝数（官方公开接口 x/relation/stat）
-- 背景图：/sdcard/fonts? 不，背景走 bg.bin 里的 "bili"（还没打进去就退化成渐变色）
local t = 0
local next_fetch = 0
local err, status
local uid, follower, following, ts

local API = "https://api.bilibili.com/x/relation/stat?vmid="

local HEADERS = {
  ["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126 Safari/537.36",
  ["Referer"] = "https://www.bilibili.com/",
  ["Accept"] = "application/json"
}

local function text_or(v, def)
  if v == nil or v == "" then return def end
  return v
end

local function load()
  uid = store.get("uid") or ""
  follower = tonumber(store.get("follower") or "0") or 0
  following = tonumber(store.get("following") or "0") or 0
  ts = tonumber(store.get("ts") or "0") or 0
end

-- 1438247 -> "143.8万"
local function big(n)
  n = tonumber(n) or 0
  if n >= 100000000 then return string.format("%.2f", n / 100000000), "亿" end
  if n >= 10000 then return string.format("%.1f", n / 10000), "万" end
  return tostring(n), ""
end

local function fetch()
  if uid == "" then
    err = "请填写 UID"
    next_fetch = t + 30
    return
  end
  if not net.up() then
    status = "等待网络"
    next_fetch = t + 3
    return
  end

  status = "读取中"
  err = nil
  http.get(API .. uid, HEADERS, function(st, body)
    status = nil
    local j = json.decode(body or "")
    if not j or tonumber(j.code) ~= 0 or not j.data then
      err = "接口 code=" .. tostring(j and j.code or st)
      next_fetch = t + 60
      return
    end
    follower = tonumber(j.data.follower) or 0
    following = tonumber(j.data.following) or 0
    ts = time.epoch()
    store.set("follower", tostring(follower))
    store.set("following", tostring(following))
    store.set("ts", tostring(ts))
    err = nil
    next_fetch = t + 300
    log("bili " .. uid .. " followers " .. follower)
  end)
end

function on_start()
  uid = text_or(app.get("uid"), "")
  load()
  local stale = (time.epoch() - ts) > 300
  next_fetch = stale and 0 or (t + 300)
end

-- 背景：有 "bili" 背景图就用，没有就自己画一层
local function draw_bg()
  if ui.bg("bili") then return end
  ui.clear(0x0E1014)
  ui.circle(240, 168, 176, 0x1A3A414F)
  ui.circle(240, 168, 130, 0x1A4A5568)
end

function on_draw(dt)
  t = t + dt
  if t > next_fetch then fetch() end

  draw_bg()

  ui.text("哔哩哔哩", 240, 62, 29, 0xFB7299)

  if err then
    ui.text(err, 240, 230, 29, 0xffc86b)
    ui.text("插件详情里填 UID", 240, 276, 22, 0x6b7785)
    return
  end
  if status then
    ui.text(status, 240, 230, 29, 0xdfe8f0)
    return
  end

  local s, unit = big(follower)
  local w = ui.measure(s, 106)
  ui.text(s, 240 - (unit ~= "" and 18 or 0), 158, 106, 0xF4F7F8)
  if unit ~= "" then
    ui.text(unit, 240 - (unit ~= "" and 18 or 0) + w * 0.5 + 18, 184, 29, 0xFB7299)
  end
  ui.text("粉丝", 240, 224, 29, 0x8fa0b4)

  ui.text("关注 " .. tostring(following), 240, 300, 29, 0xB8C4D0)
  ui.text("UID " .. uid, 240, 352, 22, 0x5E6A78)

  local age = time.epoch() - ts
  if ts > 0 and age > 1800 then
    ui.text(string.format("%d 分钟前", math.floor(age / 60)), 240, 404, 22, 0x6b7785)
  end
end
