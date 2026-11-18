#!/usr/bin/env python3
"""整理桌宠三态图 + 生成文章用的头图。"""
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "docs", "photos", "intro")

FONT_BD = r"C:\Windows\Fonts\msyhbd.ttc"
FONT = r"C:\Windows\Fonts\msyh.ttc"


def load(path, size):
    try:
        return ImageFont.truetype(path, size)
    except Exception:
        return ImageFont.load_default()


def circle(im):
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, im.size[0] - 1, im.size[1] - 1), fill=255)
    c = im.convert("RGBA")
    c.putalpha(mask)
    return c


# ---------- 桌宠三态 ----------
picks = {"08-pet-eat": 2, "09-pet-work": 7, "10-pet-sleep": 13}
labels = {"08-pet-eat": "余额健康 · 干饭", "09-pet-work": "余额下降 · 打工", "10-pet-sleep": "余额见底 · 睡觉"}
for name, idx in picks.items():
    im = Image.open(os.path.join(OUT, "_seq", "%02d.png" % idx)).convert("RGB")
    im.save(os.path.join(OUT, name + ".png"))
    circle(im).save(os.path.join(OUT, name + "-circle.png"))

# 三态拼图（带文字标签）
CELL = 360
pad = 24
sheet = Image.new("RGB", (CELL * 3 + pad * 4, CELL + 96), (12, 14, 18))
d = ImageDraw.Draw(sheet)
f = load(FONT_BD, 26)
fw = load(FONT, 18)
for i, (name, idx) in enumerate(picks.items()):
    im = Image.open(os.path.join(OUT, name + ".png")).resize((CELL, CELL))
    x = pad + i * (CELL + pad)
    sheet.paste(im, (x, pad))
    d.text((x + CELL // 2, CELL + 40), labels[name], font=f, fill=(230, 236, 244), anchor="mm")
    d.text((x + CELL // 2, CELL + 70), "pet / %s" % name.split("-")[-1], font=fw,
           fill=(120, 132, 148), anchor="mm")
sheet.save(os.path.join(OUT, "11-pet-states.png"))

# ---------- 头图 ----------
shots = ["01-clock", "02-weather", "03-bili", "04-music", "05-pc", "07-dfm"]
hero = Image.new("RGB", (1600, 900), (8, 9, 12))
d = ImageDraw.Draw(hero)
for y in range(900):
    t = y / 899
    d.line([(0, y), (1600, y)], fill=(int(8 + 16 * t), int(9 + 18 * t), int(12 + 26 * t)))

SIZE = 360
positions = [(80, 150), (500, 90), (920, 150), (1330, 90), (300, 520), (1150, 520)]
for i, (name, pos) in enumerate(zip(shots, positions)):
    p = os.path.join(OUT, name + "-circle.png")
    if not os.path.exists(p):
        continue
    im = Image.open(p).resize((SIZE, SIZE))
    hero.paste(im, pos, im)

fb = load(FONT_BD, 64)
fs = load(FONT, 30)
d.text((800, 60), "MOCHI", font=fb, fill=(240, 245, 250), anchor="mm")
d.text((800, 812), "ESP32-S3 · 480×480 圆屏 · Lua 插件化桌面摆件", font=fs, fill=(150, 162, 178), anchor="mm")
hero.save(os.path.join(OUT, "00-hero.png"))

print("整理完成")
for f_ in sorted(os.listdir(OUT)):
    if f_.endswith(".png") and not f_.startswith("_"):
        print("  ", f_)
