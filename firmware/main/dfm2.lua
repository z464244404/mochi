-- 三角洲行动 玩家数据（腾讯 AMS IDE 接口）
-- 令牌由浏览器登录 QQ 换出：把回跳链接粘到插件设置里，本插件自己兑换并存卡上
local t = 0
local next_fetch = 0
local status, err
local nick, rank, liquid, noncurrent, gain
local auth_url = ""

local IDE = "https://comm.ams.game.qq.com/ide/"
local LOGIN = "https://ams.game.qq.com/ams/userLoginSvr"
local REDIRECT = "https%3A%2F%2Fmilo.qq.com%2Fcomm-htdocs%2Flogin%2Fqc_redirect.html"

local function urldecode(s)
  return (s:gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end))
end

local function text_or(v, def)
  if v == nil or v == "" then return def end
  return v
end

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

-- code 换 openid + access_token（60 天）
local function exchange()
  local code = auth_url:match("[?&]code=([%w%-_%.]+)")
  if not code then
    err = "链接里没有 code"
    next_fetch = t + 30
    return
  end
  status = "登录中"
  local url = LOGIN .. "?a=qcCodeToOpenId&qc_code=" .. code ..
              "&appid=101491592&redirect_uri=" .. REDIRECT .. "&callback=cb"
  http.get(url, function(st, body)
    status = nil
    local inner = body and (body:match("cb%((%b{})%)") or body:match("(%b{})"))
    local j = inner and json.decode(inner) or nil
    if not j or tostring(j.iRet) ~= "0" then
      err = "登录失败 " .. tostring(j and j.iRet or ("网络 " .. tostring(st)))
      next_fetch = t + 15      -- 网络没好就过一会儿再试
      return
    end
    store.set("openid", j.openid or "")
    store.set("token", j.access_token or "")
    store.set("exp", tostring(time.epoch() + (tonumber(j.expires_in) or 0)))
    store.set("auth", auth_url)
    err = nil
    next_fetch = 0
  end)
end

local function headers()
  local ck = string.format("openid=%s; access_token=%s; acctype=qc; appid=101491592",
                           store.get("openid") or "", store.get("token") or "")
  return { ["Cookie"] = ck, ["Referer"] = "https://df.qq.com/", ["Accept"] = "*/*" }
end

local function save_all()
  store.set("nick", nick or "")
  store.set("rank", tostring(rank or 0))
  store.set("liquid", tostring(liquid or 0))
  store.set("noncur", tostring(noncurrent or 0))
  store.set("gain", tostring(gain or 0))
  store.set("ts", tostring(time.epoch()))
end

local function load_all()
  nick = store.get("nick")
  rank = tonumber(store.get("rank") or "0") or 0
  liquid = tonumber(store.get("liquid") or "0") or 0
  noncurrent = tonumber(store.get("noncur") or "0") or 0
  gain = tonumber(store.get("gain") or "0") or 0
end

-- 图表 316969：最近收益
local function fetch_gain()
  local body = "iChartId=316969&iSubChartId=316969&sIdeToken=NoOapI" ..
               "&method=dfm%2Fcenter.recent.detail&source=5" ..
               "&param=%7B%22resourceType%22%3A%22sol%22%7D"
  http.post(IDE, body, headers(), function(st, r)
    status = nil
    local j = json.decode(r or "")
    if not j or tonumber(j.ret) ~= 0 then
      err = "收益接口 ret=" .. tostring(j and j.ret or st)
      return
    end
    local ok, d = pcall(function() return j.jData.data.data.solDetail end)
    if ok and d then
      gain = tonumber(d.recentGain) or 0
    end
    err = nil
    next_fetch = t + 300
    save_all()
    log("dfm " .. tostring(nick) .. " rank " .. tostring(rank) .. " gain " .. tostring(gain))
  end)
end

-- 图表 317814：昵称 + 生涯（段位分 / 总资产 / 非流动资产）
local function fetch()
  if not net.up() then
    status = "等待网络"
    next_fetch = t + 3
    return
  end

  -- 还没令牌：用设置里的链接换（失败了每 15 秒重试，直到码过期或成功）
  if (store.get("token") or "") == "" then
    if auth_url ~= "" and auth_url ~= (store.get("auth") or "") then
      exchange()
    else
      err = "请填写授权链接"
    end
    next_fetch = t + 15
    return
  end

  status = "加载中"
  err = nil
  local body = "iChartId=317814&iSubChartId=317814&sIdeToken=QIRBwm"
  http.post(IDE, body, headers(), function(st, r)
    local j = json.decode(r or "")
    if not j or tonumber(j.ret) ~= 0 then
      status = nil
      err = "ret=" .. tostring(j and j.ret or st) .. "（登录可能过期）"
      next_fetch = t + 60
      return
    end
    local u = j.jData and j.jData.userData or {}
    local cd = j.jData and j.jData.careerData or {}
    nick = urldecode(u.charac_name or "")
    rank = tonumber(cd.rankpoint) or 0
    local total = tonumber(cd.totalprice) or 0
    noncurrent = tonumber(cd.noncurrentasset) or 0
    liquid = total - noncurrent
    save_all()
    fetch_gain()
  end)
end

function on_start()
  auth_url = text_or(app.get("auth"), "")
  local alias = text_or(app.get("name"), "")
  if alias ~= "" then nick = alias end
  load_all()

  -- 换了新链接就说明要重新登录，旧令牌作废
  if auth_url ~= "" and auth_url ~= (store.get("auth") or "") then
    store.set("token", "")
  end

  local age = time.epoch() - (tonumber(store.get("ts") or "0") or 0)
  next_fetch = (age > 300) and 0 or (t + 300)
end

local function draw_assets()
  ui.text("流动", 132, 268, 22, 0x8fa0b4)
  ui.text("非流动", 348, 268, 22, 0x8fa0b4)
  ui.text(fmt(liquid), 132, 302, 29, 0xdfe8f0)
  ui.text(fmt(noncurrent), 348, 302, 29, 0xdfe8f0)
end

local function draw_gain()
  ui.text("最近收益", 240, 356, 22, 0x8fa0b4)
  local v = tonumber(gain) or 0
  if v == 0 then
    ui.text("0", 240, 392, 29, 0x8fa0b4)
  elseif v > 0 then
    ui.text(signed(v) .. " ↑", 240, 392, 29, 0xff5a5a)
  else
    ui.text(signed(v) .. " ↓", 240, 392, 29, 0x35d07f)
  end
end

function on_draw(dt)
  t = t + dt
  if t > next_fetch then fetch() end

  ui.clear(0x0b0d12)
  ui.text((nick and #nick > 0) and nick or "三角洲", 240, 54, 22, 0x8fa0b4)

  if err then
    ui.text(err, 240, 200, 29, 0xffc86b)
    ui.text("插件详情里粘贴授权链接", 240, 250, 22, 0x6b7785)
    return
  end
  if status then
    ui.text(status, 240, 220, 29, 0xffc86b)
    return
  end

  ui.text(tostring(rank), 240, 148, 106, 0xf4f7f8)
  ui.text("烽火段位", 240, 214, 22, 0x8fa0b4)
  draw_assets()
  draw_gain()
end
