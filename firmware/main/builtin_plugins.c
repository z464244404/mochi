#include "builtin_plugins.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "plugins.h"
#include "sd.h"
#include "cJSON.h"

static const char *TAG = "builtin";

/* ---------------- 时钟（两种样式，参数在插件详情页里改） ---------------- */

static const char *CLOCK_MANIFEST =
    "{\n"
    "  \"id\": \"clock\",\n"
    "  \"name\": \"时钟\",\n"
    "  \"version\": \"1.1.0\",\n"
    "  \"api\": 1,\n"
    "  \"author\": \"mochi\",\n"
    "  \"settings\": [\n"
    "    {\"key\": \"style\", \"label\": \"样式\", \"type\": \"select\", "
    "\"options\": [\"cute\", \"tech\", \"minimal\"], \"default\": \"cute\"},\n"
    "    {\"key\": \"seconds\", \"label\": \"秒针\", \"type\": \"select\", "
    "\"options\": [\"on\", \"off\"], \"default\": \"on\"},\n"
    "    {\"key\": \"bg\", \"label\": \"背景颜色（极简）\", \"type\": \"color\", "
    "\"default\": \"#0F1115\"},\n"
    "    {\"key\": \"hand\", \"label\": \"指针颜色（极简）\", \"type\": \"color\", "
    "\"default\": \"#E8F4F2\"},\n"
    "    {\"key\": \"sec_hand\", \"label\": \"秒针颜色（极简）\", \"type\": \"color\", "
    "\"default\": \"#5BE0BD\"}\n"
    "  ]\n"
    "}\n";

static const char *CLOCK_LUA =
    "-- 时钟：cute / tech / minimal 三种样式，参数来自插件设置\n"
    "local style = \"cute\"\n"
    "local show_sec = true\n"
    "\n"
    "local CN = {\"日\", \"一\", \"二\", \"三\", \"四\", \"五\", \"六\"}\n"
    "local EN = {\"SUN\", \"MON\", \"TUE\", \"WED\", \"THU\", \"FRI\", \"SAT\"}\n"
    "\n"
    "-- \"#RRGGBB\" -> 0xRRGGBB\n"
    "local function hexc(s, def)\n"
    "  if type(s) ~= \"string\" then return def end\n"
    "  local n = tonumber((s:gsub(\"#\", \"\")), 16)\n"
    "  if n then return n end\n"
    "  return def\n"
    "end\n"
    "\n"
    "function on_start()\n"
    "  style = app.get(\"style\") or \"cute\"\n"
    "  show_sec = (app.get(\"seconds\") or \"on\") ~= \"off\"\n"
    "end\n"
    "\n"
    "local function draw_cute(now, hh)\n"
    "  ui.clear(0xFFF6EC)\n"
    "  ui.circle(240, 96, 13, 0xF7B267)\n"
    "  for i = 0, 7 do\n"
    "    local a = i * 0.7854\n"
    "    ui.line(240 + math.cos(a) * 21, 96 + math.sin(a) * 21,\n"
    "            240 + math.cos(a) * 27, 96 + math.sin(a) * 27, 3, 0xF7B267)\n"
    "  end\n"
    "  if show_sec then\n"
    "    ui.arc(240, 240, 210, 5, -90, 270, 0xF3DFCB)\n"
    "    ui.arc(240, 240, 210, 5, -90, -90 + now.sec * 6, 0xF09A5B)\n"
    "  end\n"
    "  ui.text(hh, 240, 236, 106, 0x3D2B1F)\n"
    "  ui.round_rect(118, 306, 244, 48, 24, 0xF1D7BE)\n"
    "  ui.text(string.format(\"%d月%d日 周%s\", now.month, now.day, CN[(now.wday % 7) + 1]),\n"
    "          240, 330, 29, 0x6B4A36)\n"
    "end\n"
    "\n"
    "local function draw_tech(now, hh)\n"
    "  ui.clear(0x05090E)\n"
    "  for i = 0, 59 do\n"
    "    local a = math.rad(i * 6 - 90)\n"
    "    local major = (i % 5 == 0)\n"
    "    local r0 = major and 192 or 199\n"
    "    local c = major and 0x2E6B7E or 0x16323C\n"
    "    local th = major and 3 or 2\n"
    "    ui.line(240 + math.cos(a) * r0, 240 + math.sin(a) * r0,\n"
    "            240 + math.cos(a) * 206, 240 + math.sin(a) * 206, th, c)\n"
    "  end\n"
    "  if show_sec then\n"
    "    ui.arc(240, 240, 214, 3, -90, -90 + now.sec * 6, 0x35E0C0)\n"
    "    local a = math.rad(now.sec * 6 - 90)\n"
    "    ui.circle(240 + math.cos(a) * 214, 240 + math.sin(a) * 214, 5, 0x35E0C0)\n"
    "  end\n"
    "  ui.text(hh, 240, 236, 106, 0xE6FBFF)\n"
    "  ui.text(string.format(\"%04d.%02d.%02d  %s\", now.year, now.month, now.day,\n"
    "           EN[(now.wday % 7) + 1]), 240, 342, 29, 0x3E7A8C)\n"
    "  ui.circle(216, 402, 3, 0x35E0C0)\n"
    "  ui.circle(228, 402, 3, 0x1B3A46)\n"
    "  ui.circle(240, 402, 3, 0x1B3A46)\n"
    "end\n"
    "\n"
    "local function draw_minimal(now)\n"
    "  local bg = hexc(app.get(\"bg\"), 0x0F1115)\n"
    "  local fg = hexc(app.get(\"hand\"), 0xE8F4F2)\n"
    "  local sc = hexc(app.get(\"sec_hand\"), 0x5BE0BD)\n"
    "  ui.clear(bg)\n"
    "  -- 12 个刻度\n"
    "  for i = 0, 11 do\n"
    "    local a = math.rad(i * 30 - 90)\n"
    "    local major = (i % 3 == 0)\n"
    "    local r0 = major and 184 or 193\n"
    "    local th = major and 6 or 3\n"
    "    ui.line(240 + math.cos(a) * r0, 240 + math.sin(a) * r0,\n"
    "            240 + math.cos(a) * 206, 240 + math.sin(a) * 206, th, fg)\n"
    "  end\n"
    "  local hh = (now.hour % 12) + now.min / 60\n"
    "  local ma = math.rad(hh * 30 - 90)\n"
    "  local na = math.rad(now.min * 6 - 90)\n"
    "  local sa = math.rad(now.sec * 6 - 90)\n"
    "  -- 时针 / 分针 / 秒针\n"
    "  ui.line(240 - math.cos(ma) * 18, 240 - math.sin(ma) * 18,\n"
    "          240 + math.cos(ma) * 108, 240 + math.sin(ma) * 108, 11, fg)\n"
    "  ui.line(240 - math.cos(na) * 22, 240 - math.sin(na) * 22,\n"
    "          240 + math.cos(na) * 152, 240 + math.sin(na) * 152, 7, fg)\n"
    "  if show_sec then\n"
    "    ui.line(240 - math.cos(sa) * 26, 240 - math.sin(sa) * 26,\n"
    "            240 + math.cos(sa) * 168, 240 + math.sin(sa) * 168, 3, sc)\n"
    "    ui.circle(240, 240, 8, sc)\n"
    "  end\n"
    "  ui.circle(240, 240, 7, bg)\n"
    "  ui.circle(240, 240, 4, fg)\n"
    "end\n"
    "\n"
    "function on_draw(dt)\n"
    "  local now = time.now()\n"
    "  if style == \"minimal\" then\n"
    "    draw_minimal(now)\n"
    "    return\n"
    "  end\n"
    "  local hh = string.format(\"%02d:%02d\", now.hour, now.min)\n"
    "  if style == \"tech\" then draw_tech(now, hh) else draw_cute(now, hh) end\n"
    "end\n";

/* ---------------- 天气（Open-Meteo，免注册、无需 key） ---------------- */

static const char *WEATHER_MANIFEST =
    "{\n"
    "  \"id\": \"weather\",\n"
    "  \"name\": \"天气\",\n"
    "  \"version\": \"2.5.0\",\n"
    "  \"api\": 1,\n"
    "  \"author\": \"mochi\",\n"
    "  \"settings\": [\n"
    "    {\"key\": \"city\", \"label\": \"地名（显示用）\", \"type\": \"text\", "
    "\"default\": \"深圳\"},\n"
    "    {\"key\": \"lat\", \"label\": \"纬度\", \"type\": \"number\", \"default\": \"22.54\"},\n"
    "    {\"key\": \"lon\", \"label\": \"经度\", \"type\": \"number\", \"default\": \"114.06\"},\n"
    "    {\"key\": \"unit\", \"label\": \"单位\", \"type\": \"select\", \"options\": [\"c\", \"f\"], "
    "\"default\": \"c\"}\n"
    "  ]\n"
    "}\n";

static const char *WEATHER_LUA =
    "-- 天气：Open-Meteo（免费、无需注册），实时 + 未来三天\n"
    "-- 图标和背景都是 meteocons 素材，存在 TF 卡（/sdcard/icons.bin、/sdcard/bg.bin）\n"
    "local t = 0\n"
    "local next_fetch = 0\n"
    "local cur, days, err\n"
    "local city, unit, lat, lon\n"
    "\n"
    "local function icon_for(code, night)\n"
    "  if code == 0 then return night and \"clear-night\" or \"clear-day\" end\n"
    "  if code <= 2 then return night and \"partly-cloudy-night\" or \"partly-cloudy-day\" end\n"
    "  if code == 3 then return \"cloudy\" end\n"
    "  if code == 45 or code == 48 then return \"fog\" end\n"
    "  if code >= 51 and code <= 57 then return \"drizzle\" end\n"
    "  if code == 61 or code == 63 then return \"rain\" end\n"
    "  if code == 65 then return \"overcast-rain\" end\n"
    "  if code == 66 or code == 67 then return \"sleet\" end\n"
    "  if code >= 71 and code <= 77 then return \"snow\" end\n"
    "  if code == 80 or code == 81 then return \"partly-cloudy-day-rain\" end\n"
    "  if code == 82 then return \"overcast-rain\" end\n"
    "  if code >= 85 and code <= 86 then return \"snow\" end\n"
    "  if code >= 95 then return \"thunderstorms-rain\" end\n"
    "  return \"cloudy\"\n"
    "end\n"
    "\n"
    "-- 白天的几张背景太亮，配深色版，文字才看得清\n"
    "local function bg_for(icon)\n"
    "  if icon == \"clear-day\" then return \"clear-day-dark\" end\n"
    "  if icon == \"partly-cloudy-day\" then return \"partly-cloudy-day-dark\" end\n"
    "  if icon == \"partly-cloudy-day-rain\" then return \"partly-cloudy-day-rain-dark\" end\n"
    "  return icon\n"
    "end\n"
    "\n"
    "local WMO = {\n"
    "  [0]=\"晴\", [1]=\"晴\", [2]=\"多云\", [3]=\"阴\",\n"
    "  [45]=\"雾\", [48]=\"雾\",\n"
    "  [51]=\"小雨\", [53]=\"小雨\", [55]=\"小雨\",\n"
    "  [61]=\"小雨\", [63]=\"中雨\", [65]=\"大雨\",\n"
    "  [66]=\"雨\", [67]=\"雨\",\n"
    "  [71]=\"小雪\", [73]=\"中雪\", [75]=\"大雪\", [77]=\"雪\",\n"
    "  [80]=\"阵雨\", [81]=\"阵雨\", [82]=\"暴雨\",\n"
    "  [85]=\"阵雪\", [86]=\"阵雪\",\n"
    "  [95]=\"雷阵雨\", [96]=\"雷阵雨\", [99]=\"雷阵雨\"\n"
    "}\n"
    "\n"
    "-- 设置留空时 Lua 里是空串（真值），要用 == \"\" 兜住\n"
    "local function text_or(v, def)\n"
    "  if v == nil or v == \"\" then return def end\n"
    "  return v\n"
    "end\n"
    "\n"
    "-- \"2026-11-13\" -> \"11/13\"\n"
    "local function short_date(s)\n"
    "  if not s or #s < 10 then return \"\" end\n"
    "  return s:sub(6, 7) .. \"/\" .. s:sub(9, 10)\n"
    "end\n"
    "\n"
    "local function apply(j)\n"
    "  if not j or not j.current or not j.daily then return false end\n"
    "  cur = { temp = j.current.temperature_2m, code = j.current.weather_code }\n"
    "  days = {}\n"
    "  for i = 2, 4 do\n"
    "    if j.daily.temperature_2m_max[i] then\n"
    "      days[#days + 1] = { hi = j.daily.temperature_2m_max[i],\n"
    "                          lo = j.daily.temperature_2m_min[i],\n"
    "                          code = j.daily.weather_code[i],\n"
    "                          date = short_date(j.daily.time and j.daily.time[i]) }\n"
    "    end\n"
    "  end\n"
    "  return true\n"
    "end\n"
    "\n"
    "local function fetch()\n"
    "  if not net.up() then next_fetch = t + 3; return end\n"
    "  local u = (unit == \"f\") and \"fahrenheit\" or \"celsius\"\n"
    "  local url = \"https://api.open-meteo.com/v1/forecast?latitude=\" .. lat ..\n"
    "              \"&longitude=\" .. lon ..\n"
    "              \"&current=temperature_2m,weather_code\" ..\n"
    "              \"&daily=weather_code,temperature_2m_max,temperature_2m_min\" ..\n"
    "              \"&timezone=auto&forecast_days=4&temperature_unit=\" .. u\n"
    "  http.get(url, function(st, body)\n"
    "    if st ~= 200 then err = \"HTTP \" .. st; next_fetch = t + 20; return end\n"
    "    local j = json.decode(body)\n"
    "    if not apply(j) then err = \"BAD JSON\"; next_fetch = t + 20; return end\n"
    "\n"
    "    store.set(\"wx\", body)\n"
    "    store.set(\"wx_ts\", tostring(time.epoch()))\n"
    "    err = nil\n"
    "    next_fetch = t + 600\n"
    "    log(\"weather \" .. cur.temp .. \" city=\" .. city)\n"
    "  end)\n"
    "end\n"
    "\n"
    "function on_start()\n"
    "  city = text_or(app.get(\"city\"), \"深圳\")\n"
    "  unit = text_or(app.get(\"unit\"), \"c\")\n"
    "  lat = text_or(app.get(\"lat\"), \"22.54\")\n"
    "  lon = text_or(app.get(\"lon\"), \"114.06\")\n"
    "\n"
    "  local raw = store.get(\"wx\")\n"
    "  if raw then apply(json.decode(raw)) end\n"
    "  local ts = tonumber(store.get(\"wx_ts\") or \"0\") or 0\n"
    "  if time.epoch() - ts > 600 then next_fetch = 0 else next_fetch = t + 600 end\n"
    "end\n"
    "\n"
    "-- 未来三天：横排三列，日期 / 图标 / 高温(橙) / 低温(蓝)\n"
    "local function draw_days()\n"
    "  if not days then return end\n"
    "  local sym = (unit == \"f\") and \"℉\" or \"℃\"\n"
    "  for i = 1, 3 do\n"
    "    local d = days[i]\n"
    "    if d then\n"
    "      local x = 118 + (i - 1) * 122\n"
    "      if d.date then ui.text(d.date, x, 296, 22, 0x8a96a4) end\n"
    "      ui.icon(icon_for(d.code, false), 44, x, 334)\n"
    "      ui.text(string.format(\"%d%s\", math.floor(d.hi + 0.5), sym), x, 370, 22, 0xff9f43)\n"
    "      ui.text(string.format(\"%d%s\", math.floor(d.lo + 0.5), sym), x, 396, 22, 0x54b8ff)\n"
    "    end\n"
    "  end\n"
    "end\n"
    "\n"
    "function on_draw(dt)\n"
    "  t = t + dt\n"
    "  if t > next_fetch then fetch() end\n"
    "\n"
    "  if cur then\n"
    "    local h = time.now().hour\n"
    "    local night = (h < 6 or h >= 18)\n"
    "    local icon = icon_for(cur.code, night)\n"
    "\n"
    "    if not ui.bg(bg_for(icon)) then ui.clear(0x0b0d12) end\n"
    "    ui.icon(icon, 120, 138, 148)\n"
    "\n"
    "    local s = string.format(\"%d\", math.floor(cur.temp + 0.5))\n"
    "    local w = ui.measure(s, 106)\n"
    "    ui.text(s, 322, 140, 106, 0xf4f7f8)\n"
    "    ui.text(unit == \"f\" and \"℉\" or \"℃\", 322 + w * 0.5 + 20, 164, 29, 0x5be0bd)\n"
    "    ui.text(WMO[cur.code] or \"阴\", 322, 204, 29, 0xdfe8f0)\n"
    "  else\n"
    "    ui.clear(0x0b0d12)\n"
    "    local msg = \"加载中\"\n"
    "    if not net.up() then msg = \"等待网络\" elseif err then msg = \"网络错误\" end\n"
    "    ui.text(msg, 240, 180, 29, 0xffc86b)\n"
    "  end\n"
    "\n"
    "  ui.text(city, 240, 44, 29, 0xdfe8f0)\n"
    "  draw_days()\n"
    "end\n"
    ;

static const char *DFM_MANIFEST =
    "{\n"
    "  \"id\": \"dfm\",\n"
    "  \"name\": \"三角洲\",\n"
    "  \"version\": \"2.0.0\",\n"
    "  \"api\": 1,\n"
    "  \"author\": \"mochi\",\n"
    "  \"settings\": [\n"
    "    {\"key\": \"hint\", \"label\": \"数据来源\", \"type\": \"text\",\n"
    "     \"default\": \"电脑上运行 python tools/df_sync.py 推送\"}\n"
    "  ]\n"
    "}\n"
    ;

static const char *DFM_LUA =
    "-- 三角洲行动 玩家数据（只负责显示）\n"
    "-- 数据由电脑上的 tools/df_sync.py 拉腾讯接口后推过来：POST /api/dfm\n"
    "local t = 0\n"
    "local last_reload = -1\n"
    "local nick, rank, liquid, noncurrent, gain, ts\n"
    "\n"
    "local function fmt(n)\n"
    "  n = tonumber(n) or 0\n"
    "  local a = math.abs(n)\n"
    "  if a >= 100000000 then return string.format(\"%.2f亿\", n / 100000000) end\n"
    "  if a >= 10000 then return string.format(\"%.1f万\", n / 10000) end\n"
    "  return string.format(\"%d\", n)\n"
    "end\n"
    "\n"
    "local function signed(n)\n"
    "  local v = tonumber(n) or 0\n"
    "  if v > 0 then return \"+\" .. fmt(v) end\n"
    "  return fmt(v)\n"
    "end\n"
    "\n"
    "local function load()\n"
    "  nick = store.get(\"nick\") or \"\"\n"
    "  rank = tonumber(store.get(\"rank\") or \"0\") or 0\n"
    "  liquid = tonumber(store.get(\"liquid\") or \"0\") or 0\n"
    "  noncurrent = tonumber(store.get(\"noncur\") or \"0\") or 0\n"
    "  gain = tonumber(store.get(\"gain\") or \"0\") or 0\n"
    "  ts = tonumber(store.get(\"ts\") or \"0\") or 0\n"
    "end\n"
    "\n"
    "function on_start()\n"
    "  load()\n"
    "end\n"
    "\n"
    "local function draw_wait()\n"
    "  ui.text(\"等待数据\", 240, 210, 29, 0xffc86b)\n"
    "  ui.text(\"电脑上运行 df_sync.py\", 240, 256, 22, 0x6b7785)\n"
    "  ui.text(\"会把数据推过来\", 240, 288, 22, 0x6b7785)\n"
    "end\n"
    "\n"
    "function on_draw(dt)\n"
    "  t = t + dt\n"
    "  -- 每 2 秒重新读一次卡上的 data.json（脚本随时可能推送）\n"
    "  if t - last_reload > 2 then\n"
    "    last_reload = t\n"
    "    store.reload()\n"
    "    load()\n"
    "  end\n"
    "\n"
    "  ui.clear(0x0b0d12)\n"
    "  if not nick or nick == \"\" then\n"
    "    draw_wait()\n"
    "    return\n"
    "  end\n"
    "\n"
    "  ui.text(nick, 240, 54, 22, 0x8fa0b4)\n"
    "  ui.text(tostring(rank), 240, 148, 106, 0xf4f7f8)\n"
    "  ui.text(\"烽火段位\", 240, 214, 22, 0x8fa0b4)\n"
    "\n"
    "  ui.text(\"流动\", 132, 268, 22, 0x8fa0b4)\n"
    "  ui.text(\"非流动\", 348, 268, 22, 0x8fa0b4)\n"
    "  ui.text(fmt(liquid), 132, 302, 29, 0xdfe8f0)\n"
    "  ui.text(fmt(noncurrent), 348, 302, 29, 0xdfe8f0)\n"
    "\n"
    "  ui.text(\"最近收益\", 240, 356, 22, 0x8fa0b4)\n"
    "  if gain == 0 then\n"
    "    ui.text(\"0\", 240, 392, 29, 0x8fa0b4)\n"
    "  elseif gain > 0 then\n"
    "    ui.text(signed(gain) .. \" ↑\", 240, 392, 29, 0xff5a5a)\n"
    "  else\n"
    "    ui.text(signed(gain) .. \" ↓\", 240, 392, 29, 0x35d07f)\n"
    "  end\n"
    "\n"
    "  -- 数据陈旧提示\n"
    "  local age = time.epoch() - ts\n"
    "  if ts > 0 and age > 1800 then\n"
    "    ui.text(\"数据 \" .. string.format(\"%d\", math.floor(age / 60)) .. \" 分钟前\",\n"
    "            240, 440, 22, 0xff9f43)\n"
    "  end\n"
    "end\n"
    ;

static const builtin_file_t CLOCK_FILES[] = {
    {"manifest.json", NULL},
    {"main.lua", NULL},
};

static const builtin_file_t DFM_FILES[] = {
    {"manifest.json", NULL},
    {"main.lua", NULL},
};

static const builtin_file_t WEATHER_FILES[] = {
    {"manifest.json", NULL}, /* 运行时填 */
    {"main.lua", NULL},
};

/* 用指针表把上面两个字符串接进来（C 里不好在静态初始化里引用） */
static const char *file_content(const builtin_plugin_t *p, const char *name)
{
    if (!strcmp(p->id, "clock")) {
        if (!strcmp(name, "manifest.json")) return CLOCK_MANIFEST;
        if (!strcmp(name, "main.lua")) return CLOCK_LUA;
    }
    if (!strcmp(p->id, "dfm")) {
        if (!strcmp(name, "manifest.json")) return DFM_MANIFEST;
        if (!strcmp(name, "main.lua")) return DFM_LUA;
    }
    if (!strcmp(p->id, "weather")) {
        if (!strcmp(name, "manifest.json")) return WEATHER_MANIFEST;
        if (!strcmp(name, "main.lua")) return WEATHER_LUA;
    }
    return NULL;
}

static const builtin_plugin_t BUILTINS[] = {
    {"clock", "时钟（cute / tech / minimal）", "1.1.0", CLOCK_FILES},
    {"dfm", "三角洲", "2.0.0", DFM_FILES},
    {"weather", "天气（实时 + 未来三天）", "2.5.0", WEATHER_FILES},
    {NULL, NULL, NULL, NULL},
};

int builtin_count(void)
{
    int n = 0;
    while (BUILTINS[n].id) n++;
    return n;
}

const builtin_plugin_t *builtin_get(int i)
{
    if (i < 0 || i >= builtin_count()) return NULL;
    return &BUILTINS[i];
}

const builtin_plugin_t *builtin_find(const char *id)
{
    for (int i = 0; i < builtin_count(); i++) {
        if (!strcmp(BUILTINS[i].id, id)) return &BUILTINS[i];
    }
    return NULL;
}

int builtin_install(const char *id)
{    const builtin_plugin_t *p = builtin_find(id);
    if (!p) return 0;

    char dir[160];
    plugins_path(id, NULL, dir, sizeof(dir));
    mkdir(PLUGIN_ROOT, 0777);
    mkdir(dir, 0777);

    int ok = 0;
    for (const builtin_file_t *f = p->files; f->file; f++) {
        const char *content = file_content(p, f->file);
        if (!content) continue;

        char path[200];
        plugins_path(id, f->file, path, sizeof(path));
        FILE *fp = sd_open_write(path, 10);
        if (!fp) {
            ESP_LOGE(TAG, "open %s failed", path);
            continue;
        }
        size_t len = strlen(content);
        size_t w = fwrite(content, 1, len, fp);
        fclose(fp);
        if (w == len) ok++;
        ESP_LOGI(TAG, "wrote %s (%u bytes)", path, (unsigned)w);
    }
    return ok;
}

/* 读已安装插件的 manifest 版本 */
static void installed_version(const char *id, char *out, size_t n)
{
    out[0] = 0;
    char path[200];
    plugins_path(id, "manifest.json", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char buf[1024];
    size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = 0;

    cJSON *j = cJSON_Parse(buf);
    if (!j) return;
    cJSON *v = cJSON_GetObjectItem(j, "version");
    if (cJSON_IsString(v) && v->valuestring) snprintf(out, n, "%.15s", v->valuestring);
    cJSON_Delete(j);
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    long sz = -1;
    if (fseek(f, 0, SEEK_END) == 0) sz = ftell(f);
    fclose(f);
    return sz;
}

void builtin_sync(void)
{
    for (int i = 0; i < builtin_count(); i++) {
        const builtin_plugin_t *p = builtin_get(i);
        char ver[16] = {0};
        char main_path[200];
        plugins_path(p->id, "main.lua", main_path, sizeof(main_path));

        bool have = plugins_has(p->id, "main.lua");
        if (have) installed_version(p->id, ver, sizeof(ver));

        /* 版本对、主文件也在、大小还一致，才算装好。
         * 只比版本号会漏掉「版本写进去了但 main.lua 没写完」这种半截状态。 */
        const char *src = file_content(p, "main.lua");
        long sz = have ? file_size(main_path) : -1;
        bool size_ok = src && sz == (long)strlen(src);

        if (!have || strcmp(ver, p->version) != 0 || !size_ok) {
            ESP_LOGW(TAG, "%s: ver %s->%s, main.lua %ld/%d bytes, (re)installing", p->id,
                     have ? (ver[0] ? ver : "(none)") : "(missing)", p->version, (long)sz,
                     src ? (int)strlen(src) : -1);
            builtin_install(p->id);
        }
    }

    /* 一个都没激活过就默认用时钟 */
    if (plugins_active()[0] == 0) plugins_set_active("clock");
}
