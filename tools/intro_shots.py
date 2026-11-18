#!/usr/bin/env python3
"""给图文介绍批量截图：切屏 -> 抓 /api/shot.bmp -> 存正方形 + 圆形两种。"""
import io
import os
import time
import urllib.parse
import urllib.request

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HOST = "192.168.1.29"
BASE = "http://" + HOST
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "photos", "intro")
os.makedirs(OUT, exist_ok=True)


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=15) as r:
        return r.read()


def post(path, data=b""):
    req = urllib.request.Request(BASE + path, data=data, method="POST")
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read().decode(errors="replace")


def status():
    import json
    return json.loads(get("/api/status"))


def goto(app, limit=14):
    for _ in range(limit):
        if status().get("app") == app:
            return True
        post("/api/gesture?event=next")
        time.sleep(0.9)
    return False


def shot(name):
    raw = get("/api/shot.bmp")
    im = Image.open(io.BytesIO(raw)).convert("RGB")
    im.save(os.path.join(OUT, name + ".png"))
    # 圆形版（用于图文里好看）
    mask = Image.new("L", im.size, 0)
    d = ImageDraw.Draw(mask)
    d.ellipse((0, 0, im.size[0] - 1, im.size[1] - 1), fill=255)
    circ = im.convert("RGBA")
    circ.putalpha(mask)
    circ.save(os.path.join(OUT, name + "-circle.png"))
    print(  [ok], name)
    return im


def make_cover(title, artist, hue=(40, 60, 110)):
    """生成一张 240x240 的唱片封面（渐变 + 文字）"""
    S = 240
    im = Image.new("RGB", (S, S), hue)
    d = ImageDraw.Draw(im)
    for y in range(S):
        t = y / (S - 1)
        d.line([(0, y), (S, y)],
               fill=(int(hue[0] + 60 * t), int(hue[1] + 40 * t), int(hue[2] + 70 * t)))
    # 加一点光斑
    d.ellipse((150, -40, 320, 130), fill=(70, 58, 92))
    try:
        f1 = ImageFont.truetype(r"C:\Windows\Fonts\msyhbd.ttc", 30)
        f2 = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 18)
    except Exception:
        f1 = f2 = ImageFont.load_default()
    d.text((20, 150), title, font=f1, fill=(245, 245, 250))
    d.text((20, 190), artist, font=f2, fill=(190, 190, 205))
    return im


def push_cover(im):
    a = np.asarray(im.convert("RGB"), dtype=np.uint16)
    raw = (((a[:, :, 0] & 0xF8) << 8) | ((a[:, :, 1] & 0xFC) << 3) | (a[:, :, 2] >> 3))
    post("/api/music/cover", raw.astype("<u2").tobytes())


# ---------------- 准备数据 ----------------
print("准备数据…")
now = int(time.time())

# 桌宠：填 key（屏幕上就不会出现 SET API KEY）
try:
    post("/api/app/settings?id=pet",
         b'{"api_key":"sk-53c51330f4174b628401e7c39ec47b6e"}')
    print("  [ok] 桌宠 key 已填")
except Exception as e:
    print("  [x] 桌宠 key:", e)

# DeepSeek 余额
try:
    q = urllib.parse.urlencode({"nick": "zemo", "rank": "12", "liquid": "23.47",
                                "noncur": "58.90", "gain": "35.43", "ts": now})
    post("/api/dfm?" + q)
    print("  [ok] 余额数据")
except Exception as e:
    print("  [x] dfm:", e)

# 音乐
try:
    q = urllib.parse.urlencode({"title": "我用什么把你留住", "artist": "福禄寿FloruitShow",
                                "pos": "138", "dur": "329", "playing": "1", "ts": now})
    post("/api/music?" + q)
    push_cover(make_cover("我用什么把你留住", "福禄寿FloruitShow", (36, 44, 96)))
    print("  [ok] 音乐数据 + 封面")
except Exception as e:
    print("  [x] 音乐:", e)

time.sleep(1.5)

# ---------------- 逐屏截图 ----------------
print("切屏截图…")
PLAN = [
    ("clock", "01-clock", "时钟"),
    ("weather", "02-weather", "天气"),
    ("bili", "03-bili", "哔哩哔哩"),
    ("music", "04-music", "音乐"),
    ("pc", "05-pc", "电脑状态"),
    ("pet", "06-pet", "桌宠"),
]
for app, name, label in PLAN:
    if app == "dfm":
        pass
    ok = goto(app)
    time.sleep(2.2)
    if app == "music":
        # 补一次数据，保证进度条和封面都在
        q = urllib.parse.urlencode({"title": "我用什么把你留住", "artist": "福禄寿FloruitShow",
                                    "pos": "138", "dur": "329", "playing": "1", "ts": int(time.time())})
        post("/api/music?" + q)
        time.sleep(1.0)
    if not ok:
        print("  [!] 没能切到", app)
    shot(name)
    time.sleep(0.4)

# 余额屏（如果启用了）
try:
    apps = [a["id"] for a in __import__("json").loads(get("/api/apps"))["apps"]]
    if "dfm" in apps:
        post("/api/app/enable?id=dfm&on=1")
        time.sleep(1.5)
        if goto("dfm"):
            time.sleep(2.0)
            q = urllib.parse.urlencode({"nick": "zemo", "rank": "12", "liquid": "23.47",
                                        "noncur": "58.90", "gain": "35.43", "ts": int(time.time())})
            post("/api/dfm?" + q)
            time.sleep(1.0)
            shot("07-dfm")
except Exception as e:
    print("  [x] 余额屏:", e)

print("完成 →", OUT)
