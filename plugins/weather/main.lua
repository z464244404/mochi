-- 天气：Open-Meteo（免费、无需注册），实时 + 未来三天
-- 图标和背景都是 meteocons 素材，存在 TF 卡（/sdcard/icons.bin、/sdcard/bg.bin）
local t = 0
local next_fetch = 0
local cur, days, err
local city, unit, lat, lon

local function icon_for(code, night)
  if code == 0 then return night and "clear-night" or "clear-day" end
  if code <= 2 then return night and "partly-cloudy-night" or "partly-cloudy-day" end
  if code == 3 then return "cloudy" end
  if code == 45 or code == 48 then return "fog" end
  if code >= 51 and code <= 57 then return "drizzle" end
  if code == 61 or code == 63 then return "rain" end
  if code == 65 then return "overcast-rain" end
  if code == 66 or code == 67 then return "sleet" end
  if code >= 71 and code <= 77 then return "snow" end
  if code == 80 or code == 81 then return "partly-cloudy-day-rain" end
  if code == 82 then return "overcast-rain" end
  if code >= 85 and code <= 86 then return "snow" end
  if code >= 95 then return "thunderstorms-rain" end
  return "cloudy"
end

-- 白天的几张背景太亮，配深色版，文字才看得清
local function bg_for(icon)
  if icon == "clear-day" then return "clear-day-dark" end
  if icon == "partly-cloudy-day" then return "partly-cloudy-day-dark" end
  if icon == "partly-cloudy-day-rain" then return "partly-cloudy-day-rain-dark" end
  return icon
end

local WMO = {
  [0]="晴", [1]="晴", [2]="多云", [3]="阴",
  [45]="雾", [48]="雾",
  [51]="小雨", [53]="小雨", [55]="小雨",
  [61]="小雨", [63]="中雨", [65]="大雨",
  [66]="雨", [67]="雨",
  [71]="小雪", [73]="中雪", [75]="大雪", [77]="雪",
  [80]="阵雨", [81]="阵雨", [82]="暴雨",
  [85]="阵雪", [86]="阵雪",
  [95]="雷阵雨", [96]="雷阵雨", [99]="雷阵雨"
}

-- 设置留空时 Lua 里是空串（真值），要用 == "" 兜住
local function text_or(v, def)
  if v == nil or v == "" then return def end
  return v
end

-- "2026-11-13" -> "11/13"
local function short_date(s)
  if not s or #s < 10 then return "" end
  return s:sub(6, 7) .. "/" .. s:sub(9, 10)
end

local function apply(j)
  if not j or not j.current or not j.daily then return false end
  cur = { temp = j.current.temperature_2m, code = j.current.weather_code }
  days = {}
  for i = 2, 4 do
    if j.daily.temperature_2m_max[i] then
      days[#days + 1] = { hi = j.daily.temperature_2m_max[i],
                          lo = j.daily.temperature_2m_min[i],
                          code = j.daily.weather_code[i],
                          date = short_date(j.daily.time and j.daily.time[i]) }
    end
  end
  return true
end

local function fetch()
  if not net.up() then next_fetch = t + 3; return end
  local u = (unit == "f") and "fahrenheit" or "celsius"
  local url = "https://api.open-meteo.com/v1/forecast?latitude=" .. lat ..
              "&longitude=" .. lon ..
              "&current=temperature_2m,weather_code" ..
              "&daily=weather_code,temperature_2m_max,temperature_2m_min" ..
              "&timezone=auto&forecast_days=4&temperature_unit=" .. u
  http.get(url, function(st, body)
    if st ~= 200 then err = "HTTP " .. st; next_fetch = t + 20; return end
    local j = json.decode(body)
    if not apply(j) then err = "BAD JSON"; next_fetch = t + 20; return end

    store.set("wx", body)
    store.set("wx_ts", tostring(time.epoch()))
    err = nil
    next_fetch = t + 300
    log("weather " .. cur.temp .. " city=" .. city)
  end)
end

function on_start()
  city = text_or(app.get("city"), "深圳")
  unit = text_or(app.get("unit"), "c")
  lat = text_or(app.get("lat"), "22.54")
  lon = text_or(app.get("lon"), "114.06")

  local raw = store.get("wx")
  if raw then apply(json.decode(raw)) end
  local ts = tonumber(store.get("wx_ts") or "0") or 0
  -- 只等「剩余」的时间：每次进屏 t 都从 0 开始，不能直接 t+600，
  -- 否则来回换屏等于永远不刷新
  local left = 300 - (time.epoch() - ts)
  if left <= 0 then next_fetch = 0 else next_fetch = t + left end
end

-- 未来三天：横排三列，日期 / 图标 / 高温(橙) / 低温(蓝)
local function draw_days()
  if not days then return end
  local sym = (unit == "f") and "℉" or "℃"
  for i = 1, 3 do
    local d = days[i]
    if d then
      local x = 118 + (i - 1) * 122
      if d.date then ui.text(d.date, x, 296, 22, 0x8a96a4) end
      ui.icon(icon_for(d.code, false), 44, x, 334)
      ui.text(string.format("%d%s", math.floor(d.hi + 0.5), sym), x, 370, 22, 0xff9f43)
      ui.text(string.format("%d%s", math.floor(d.lo + 0.5), sym), x, 396, 22, 0x54b8ff)
    end
  end
end

function on_draw(dt)
  t = t + dt
  if t > next_fetch then fetch() end

  if cur then
    local h = time.now().hour
    local night = (h < 6 or h >= 18)
    local icon = icon_for(cur.code, night)

    if not ui.bg(bg_for(icon)) then ui.clear(0x0b0d12) end
    ui.icon(icon, 120, 138, 148)

    local s = string.format("%d", math.floor(cur.temp + 0.5))
    local w = ui.measure(s, 106)
    ui.text(s, 322, 140, 106, 0xf4f7f8)
    ui.text(unit == "f" and "℉" or "℃", 322 + w * 0.5 + 20, 164, 29, 0x5be0bd)
    ui.text(WMO[cur.code] or "阴", 322, 204, 29, 0xdfe8f0)
  else
    ui.clear(0x0b0d12)
    local msg = "加载中"
    if not net.up() then msg = "等待网络" elseif err then msg = "网络错误" end
    ui.text(msg, 240, 180, 29, 0xffc86b)
  end

  ui.text(city, 240, 44, 29, 0xdfe8f0)
  draw_days()
end
