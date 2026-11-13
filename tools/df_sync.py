#!/usr/bin/env python3
"""
三角洲行动玩家数据 → Mochi 摆件

在电脑上跑：拉腾讯 AMS 接口，把昵称/段位分/资产/最近收益推给桌面摆件。
摆件上的「三角洲」插件只负责显示，不做任何网络请求。

用法：
    # 1. 先登录一次（令牌有效 60 天）
    python df_sync.py login

    # 2. 把数据推给设备（设备 IP 看它「网络」屏上显示的那个）
    python df_sync.py run --host 10.165.41.102

    # 只推一次
    python df_sync.py run --host 10.165.41.102 --once

依赖：只用标准库，Python 3.8+。
"""
import argparse
import json
import os
import ssl
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TOKEN_FILE = os.path.join(HERE, "df_token.json")

AUTHORIZE = (
    "https://graph.qq.com/oauth2.0/authorize?response_type=code&client_id=101491592"
    "&redirect_uri=https%3A%2F%2Fmilo.qq.com%2Fcomm-htdocs%2Flogin%2Fqc_redirect.html"
    "%3Fparent_domain%3Dhttps%3A%2F%2Fdf.qq.com%26isMiloSDK%3D1%26isPc%3D1&state=mochi"
)
REDIRECT = "https://milo.qq.com/comm-htdocs/login/qc_redirect.html"
LOGIN = "https://ams.game.qq.com/ams/userLoginSvr"
IDE = "https://comm.ams.game.qq.com/ide/"

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126 Safari/537.36"

# 免证书校验（部分公司网络会拦证书；可用 --insecure 关闭）
SSL_CTX = ssl.create_default_context()


def http(url, data=None, headers=None, timeout=25):
    hdrs = {"User-Agent": UA}
    hdrs.update(headers or {})
    req = urllib.request.Request(url, data=data, headers=hdrs)
    with urllib.request.urlopen(req, timeout=timeout, context=SSL_CTX) as r:
        return r.read().decode("utf-8", "replace")


# ---------------------------------------------------------------- 登录

def load_token():
    if not os.path.exists(TOKEN_FILE):
        return None
    try:
        with open(TOKEN_FILE, encoding="utf-8") as f:
            t = json.load(f)
        if not t.get("access_token") or not t.get("openid"):
            return None
        return t
    except Exception:
        return None


def save_token(openid, token, expires_in):
    with open(TOKEN_FILE, "w", encoding="utf-8") as f:
        json.dump(
            {
                "openid": openid,
                "access_token": token,
                "exp": int(time.time()) + int(expires_in),
            },
            f,
            ensure_ascii=False,
            indent=2,
        )
    print("令牌已保存到 %s（60 天内不用再登录）" % TOKEN_FILE)


def do_login():
    print("=" * 60)
    print("第 1 步：在浏览器里打开下面这个地址，用 QQ 登录：")
    print()
    print(AUTHORIZE)
    print()
    print("第 2 步：登录完浏览器会跳到 qccallback.html?....code=XXXX")
    print("        把地址栏里【整条链接】复制下来（别刷新那个页面）")
    print("=" * 60)
    try:
        import webbrowser

        webbrowser.open(AUTHORIZE)
    except Exception:
        pass

    cb = input("把整条回跳链接粘到这里: ").strip()
    qs = urllib.parse.parse_qs(urllib.parse.urlparse(cb).query)
    code = (qs.get("code") or [""])[0]
    if not code:
        print("× 链接里没有 code，请重新复制")
        return None

    url = "%s?a=qcCodeToOpenId&qc_code=%s&appid=101491592&redirect_uri=%s&callback=cb" % (
        LOGIN,
        code,
        urllib.parse.quote(REDIRECT, safe=""),
    )
    body = http(url)
    start, end = body.find("{"), body.rfind("}")
    if start < 0 or end < 0:
        print("× 返回看不懂：%s" % body[:200])
        return None
    try:
        j = json.loads(body[start : end + 1])
    except Exception:
        print("× 解析失败：%s" % body[:200])
        return None

    if str(j.get("iRet")) != "0":
        print("× 登录失败 iRet=%s（通常是链接过期了，重新走一次）" % j.get("iRet"))
        return None

    save_token(j["openid"], j["access_token"], j.get("expires_in") or 5184000)
    return load_token()


# ---------------------------------------------------------------- 拉数据

def ide_post(token, body):
    hdr = {
        "Cookie": "openid=%s; access_token=%s; acctype=qc; appid=101491592"
        % (token["openid"], token["access_token"]),
        "Referer": "https://df.qq.com/",
        "Content-Type": "application/x-www-form-urlencoded",
    }
    raw = http(IDE, data=body.encode(), headers=hdr)
    return json.loads(raw)


def fetch_player(token):
    j = ide_post(token, "iChartId=317814&iSubChartId=317814&sIdeToken=QIRBwm")
    if int(j.get("ret", -1)) != 0:
        raise RuntimeError("ret=%s（令牌可能过期，重新 login）" % j.get("ret"))

    d = j.get("jData") or {}
    u = d.get("userData") or {}
    c = d.get("careerData") or {}

    nick = urllib.parse.unquote(u.get("charac_name") or "")
    rank = int(c.get("rankpoint") or 0)
    total = int(c.get("totalprice") or 0)
    noncur = int(c.get("noncurrentasset") or 0)

    gain = 0
    try:
        g = ide_post(
            token,
            "iChartId=316969&iSubChartId=316969&sIdeToken=NoOapI"
            "&method=dfm%2Fcenter.recent.detail&source=5"
            "&param=%7B%22resourceType%22%3A%22sol%22%7D",
        )
        sol = (((g.get("jData") or {}).get("data") or {}).get("data") or {}).get("solDetail") or {}
        gain = int(sol.get("recentGain") or 0)
    except Exception as e:
        print("  (收益接口跳过: %s)" % e)

    return {
        "nick": nick,
        "rank": rank,
        "liquid": total - noncur,
        "noncur": noncur,
        "gain": gain,
        "ts": int(time.time()),
    }


# ---------------------------------------------------------------- 推给摆件

def push(host, p):
    q = urllib.parse.urlencode(
        {
            "nick": p["nick"],
            "rank": p["rank"],
            "liquid": p["liquid"],
            "noncur": p["noncur"],
            "gain": p["gain"],
            "ts": p["ts"],
        }
    )
    url = "http://%s/api/dfm?%s" % (host, q)
    req = urllib.request.Request(url, data=b"", headers={"User-Agent": "dfm-sync"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.read().decode("utf-8", "replace")


def human(n):
    a = abs(n)
    if a >= 100000000:
        return "%.2f亿" % (n / 100000000)
    if a >= 10000:
        return "%.1f万" % (n / 10000)
    return str(n)


def one_round(token, host):
    p = fetch_player(token)
    resp = push(host, p)
    sign = "+" if p["gain"] > 0 else ""
    print(
        "  %s  段位 %d  流动 %s  非流动 %s  收益 %s%s  -> %s"
        % (
            p["nick"],
            p["rank"],
            human(p["liquid"]),
            human(p["noncur"]),
            sign,
            human(p["gain"]),
            resp.strip(),
        )
    )


def main():
    ap = argparse.ArgumentParser(description="三角洲数据 → Mochi 摆件")
    ap.add_argument("action", choices=["login", "run"], help="login 登录 / run 推送")
    ap.add_argument("--host", help="摆件 IP（看设备『网络』屏）")
    ap.add_argument("--interval", type=int, default=300, help="推送间隔秒，默认 300")
    ap.add_argument("--once", action="store_true", help="只推一次")
    args = ap.parse_args()

    global SSL_CTX
    if os.environ.get("DF_INSECURE"):
        SSL_CTX = ssl._create_unverified_context()

    if args.action == "login":
        do_login()
        return

    token = load_token()
    if not token:
        print("还没有令牌，先跑：python df_sync.py login")
        sys.exit(1)
    if not args.host:
        print("需要 --host，例如 --host 10.165.41.102（设备『网络』屏上的 IP）")
        sys.exit(1)

    print("开始推送 -> %s（每 %d 秒）" % (args.host, args.interval))
    while True:
        try:
            one_round(token, args.host)
        except RuntimeError as e:
            print("× %s" % e)
            if "ret=" in str(e):
                print("  令牌失效，重新跑 python df_sync.py login")
                sys.exit(2)
        except Exception as e:
            print("× %s" % e)
        if args.once:
            break
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
