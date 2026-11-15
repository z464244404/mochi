-- 哔哩哔哩粉丝数：背景（logo / 粉丝 / FANS）已经烘焙在 fan-bg.png 里，
-- 这里只负责把数字画到版式留出来的槽位：居中，基线 y=300，106px，白色。
local t = 0
local next_fetch = 0
local err, status
local uid, follower, ts

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

-- NTP 没同步前 time.epoch() 是 0，直接拿去算「多久没更新」会得出负数，
-- 必须把「时间不可信」当成「数据过期」
local function now_s()
  local e = time.epoch()
  return (e > 1000000000) and e or 0
end

local function load()
  -- 注意：uid 来自插件设置（app.get），不能从 store 读，
  -- 否则会把 on_start 里设好的值覆盖成空
  follower = tonumber(store.get("follower") or "0") or 0
  ts = tonumber(store.get("ts") or "0") or 0
end

local function fetch()
  if uid == "" then
    err = "NO UID"
    next_fetch = t + 30
    return
  end
  if not net.up() then
    status = "NO NET"
    next_fetch = t + 3
    return
  end

  err = nil
  next_fetch = t + 15 -- 请求在飞的时候别每帧重复发
  http.get(API .. uid, HEADERS, function(st, body)
    local j = json.decode(body or "")
    if not j or tonumber(j.code) ~= 0 or not j.data then
      err = "ERR " .. tostring(j and j.code or st)
      next_fetch = t + 60
      return
    end
    follower = math.floor(tonumber(j.data.follower) or 0)
    ts = now_s()
    store.set("follower", tostring(follower))
    store.set("following", tostring(math.floor(tonumber(j.data.following) or 0)))
    store.set("ts", tostring(ts))
    err = nil
    status = nil
    next_fetch = t + 300
    log("bili " .. uid .. " followers " .. follower)
  end)
end

function on_start()
  uid = text_or(app.get("uid"), "")
  uid = uid:gsub("%s", "")
  load()
  local e = now_s()
  local stale = (e == 0) or (ts == 0) or (e - ts > 300)
  next_fetch = stale and 0 or (t + 300)
end

function on_draw(dt)
  t = t + dt
  if t > next_fetch then fetch() end

  -- 背景是打包进 bg.bin 的 "bili"（fan-bg.png）
  if not ui.bg("bili") then ui.clear(0x12141A) end

  if err then
    ui.text(err, 240, 260, 29, 0xFFC86B)
    return
  end
  if status or follower <= 0 then
    return
  end

  -- ui.text 的 y 是「文字竖直中心」，不是基线。
  -- 版式给数字留的槽是 y 220~300（基线 300），中心就是 260。
  ui.text(string.format("%d", follower), 240, 260, 106, 0xFFFFFF)
end
