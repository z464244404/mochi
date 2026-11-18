#!/usr/bin/env python3
"""抓桌宠三种状态：连拍 12 张（覆盖 90 秒循环），按聚类挑出三个不同姿势。"""
import io
import json
import os
import time
import urllib.request

import numpy as np
from PIL import Image, ImageDraw

HOST = "192.168.1.29"
BASE = "http://" + HOST
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "photos", "intro")
os.makedirs(OUT, exist_ok=True)


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

frames = []
print("连拍 12 张（每 8 秒一张）")
for i in range(12):
    goto_pet()
    time.sleep(0.4)
    im = Image.open(io.BytesIO(get("/api/shot.bmp"))).convert("RGB")
    frames.append((i, np.asarray(im, dtype=np.int16), im))
    im.save(os.path.join(OUT, "_raw%02d.png" % i))
    print("  frame", i, flush=True)
    time.sleep(7.6)

# 用中心区域做聚类
sig = [f[1][120:380, 120:380].astype(np.float32) for f in frames]
clusters = []          # [(代表下标, [成员下标])]
for i, s in enumerate(sig):
    for rep, members in clusters:
        if np.abs(s - sig[rep]).mean() < 6:
            members.append(i)
            break
    else:
        clusters.append((i, [i]))

clusters.sort(key=lambda c: -len(c[1]))
print("聚类数:", len(clusters), "各组大小:", [len(c[1]) for c in clusters])

names = ["08-pet-eat", "09-pet-work", "10-pet-sleep"]
chosen = [c[1][len(c[1]) // 2] for c in clusters[:3]]
for name, idx in zip(names, chosen):
    im = frames[idx][2]
    im.save(os.path.join(OUT, name + ".png"))
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, im.size[0] - 1, im.size[1] - 1), fill=255)
    c = im.convert("RGBA")
    c.putalpha(mask)
    c.save(os.path.join(OUT, name + "-circle.png"))
    print("  ->", name, "= frame", idx)

small = [Image.open(os.path.join(OUT, n + ".png")).resize((300, 300)) for n in names]
out = Image.new("RGB", (900, 300), (14, 16, 20))
for i, s in enumerate(small):
    out.paste(s, (i * 300, 0))
out.save(os.path.join(OUT, "_pet3.png"))
for i in range(12):
    os.remove(os.path.join(OUT, "_raw%02d.png" % i))
print("完成")
