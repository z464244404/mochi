#!/usr/bin/env python3
"""抓 30 帧（每 3 秒），拼成联系表，人工挑出三种姿势。"""
import io
import json
import os
import time
import urllib.request

from PIL import Image, ImageDraw

HOST = "192.168.1.29"
BASE = "http://" + HOST
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "photos", "intro")
RAW = os.path.join(OUT, "_seq")
os.makedirs(RAW, exist_ok=True)


def get(p):
    with urllib.request.urlopen(BASE + p, timeout=15) as r:
        return r.read()


def post(p):
    urllib.request.urlopen(urllib.request.Request(BASE + p, data=b"", method="POST"), timeout=20).read()


def goto_pet():
    for _ in range(14):
        if json.loads(get("/api/status")).get("app") == "pet":
            return True
        post("/api/gesture?event=next")
        time.sleep(0.8)
    return False


for _ in range(40):
    try:
        get("/api/status")
        break
    except Exception:
        time.sleep(1.0)

goto_pet()

N = 30
print("抓 %d 帧，每 3 秒一帧" % N, flush=True)
for i in range(N):
    goto_pet()
    im = Image.open(io.BytesIO(get("/api/shot.bmp"))).convert("RGB")
    im.save(os.path.join(RAW, "%02d.png" % i))
    print(" ", i, flush=True)
    time.sleep(2.4)

# 联系表 6 列 x 5 行
COLS, ROWS, CELL = 6, 5, 200
sheet = Image.new("RGB", (COLS * CELL, ROWS * CELL), (10, 10, 12))
d = ImageDraw.Draw(sheet)
for i in range(N):
    im = Image.open(os.path.join(RAW, "%02d.png" % i)).resize((CELL, CELL))
    x, y = (i % COLS) * CELL, (i // COLS) * CELL
    sheet.paste(im, (x, y))
    d.text((x + 6, y + 4), str(i), fill=(255, 220, 120))
sheet.save(os.path.join(OUT, "_contact.png"))
print("联系表已生成")
