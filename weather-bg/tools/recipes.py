# -*- coding: utf-8 -*-
"""
天气背景配方表 —— 想改色调/强度，只动这个文件，别动 gen.py。

每个天气一项：
  base    竖渐变 [(位置 0..1, 色)]，从屏幕顶到底
  lights  环境光 [(cx, cy, rx, ry, 色, 强度, 衰减幂)]
  clouds  云（见下方 cloud()）
  motifs  天气元素 [("rain"/"snow"/"stars"/"moon"/"sun"/"rays"/"fog"/"bolt", {...})]
  grain   颗粒强度（0.004~0.008，压 RGB565 色带用）

坐标都是 480x480 画布内的像素，圆心 (240,240)。
云的 cx 尽量避开 x=150..330 的上半屏 —— 城市名就写在那儿。
"""
from collections import OrderedDict


def cloud(seed, cx, cy, sx, sy, n, rmin, rmax, soft=0.28, blur=11.0,
          color="#93a6bd", gain=1.0, texture=0.30, dark=False, flat=1.9):
    return dict(seed=seed, cx=cx, cy=cy, sx=sx, sy=sy, n=n, rmin=rmin, rmax=rmax,
                soft=soft, blur=blur, color=color, gain=gain, texture=texture,
                dark=dark, flat=flat)


def rain(n, angle, length, width, alpha, y0=-60, blur=0.6):
    return ("rain", dict(n=n, angle=angle, length=length, width=width,
                         alpha=alpha, y0=y0, blur=blur))


def snow(n, rmin, rmax, alpha):
    return ("snow", dict(n=n, rmin=rmin, rmax=rmax, alpha=alpha))


RECIPES = OrderedDict([

    # ---------------- 晴 ----------------
    ("clear-day", dict(
        zh="晴", codes="0 / 1",
        base=[(0.0, "#3a99db"), (0.42, "#5fb2e6"), (0.75, "#8acff4"), (1.0, "#9fdbf8")],
        lights=[(372, 118, 210, 170, "#ffe9c0", 0.18, 1.4),
                (372, 118, 96, 96, "#fff0cc", 0.16, 1.9)],
        clouds=[cloud(11, 104, 268, 100, 28, 7, 66, 112, soft=0.30, blur=8,
                      color="#ffffff", gain=0.45, texture=0.50),
                cloud(12, 392, 348, 92, 24, 6, 58, 96, soft=0.20, blur=10,
                      color="#f2fbff", gain=0.35, texture=0.45)],
        motifs=[("sun", dict(cx=372, cy=118, r=26))],
        vignette=0.20,
        city_scrim=0.0,
        palette="light",
    )),

    # 深色备选：和上面同一张构图，换成原有的夜间友好配色
    ("clear-day-dark", dict(
        zh="晴（深色备选）", codes="0 / 1",
        base=[(0.0, "#3a2409"), (0.34, "#241a10"), (0.72, "#111420"), (1.0, "#080a10")],
        lights=[(380, 112, 240, 190, "#f7a41d", 0.34, 1.8),
                (392, 104, 110, 110, "#ffd08a", 0.30, 2.4),
                (140, 412, 320, 150, "#1d3a72", 0.22, 1.8)],
        clouds=[cloud(11, 118, 330, 150, 44, 9, 46, 96, soft=0.10, blur=14,
                      color="#a8703a", gain=0.45, texture=0.55)],
        motifs=[("sun", dict(cx=392, cy=104, r=30))],
        alt=True,
    )),

    ("clear-night", dict(
        zh="晴（夜间）", codes="0 / 1 · 夜",
        base=[(0.0, "#0f1a36"), (0.42, "#0b1224"), (1.0, "#06080f")],
        lights=[(372, 80, 300, 300, "#2f4a9e", 0.40, 1.4),
                (386, 68, 78, 78, "#cfd9ff", 0.30, 1.8)],
        clouds=[cloud(23, 116, 356, 140, 46, 6, 50, 92, soft=0.10, blur=16,
                      color="#4a5a86", gain=0.7, texture=0.4)],
        motifs=[("stars", dict(n=150)), ("moon", dict(cx=386, cy=68, r=22))],
    )),

    ("partly-cloudy-day", dict(
        zh="多云", codes="2",
        base=[(0.0, "#3792d2"), (0.42, "#5babe0"), (0.75, "#87caf1"), (1.0, "#9dd7f6")],
        lights=[(378, 96, 200, 160, "#ffe9c0", 0.16, 1.4),
                (378, 96, 88, 88, "#fff0cc", 0.14, 1.9)],
        clouds=[cloud(31, 128, 210, 62, 26, 8, 58, 104, soft=1.00, blur=4.5,
                      color="#ffffff", gain=0.80, texture=0.50, flat=1.35),
                cloud(32, 300, 178, 52, 22, 7, 52, 94, soft=0.95, blur=4.5,
                      color="#fbfdff", gain=0.75, texture=0.50, flat=1.35),
                cloud(33, 390, 286, 44, 20, 6, 46, 86, soft=0.85, blur=5.0,
                      color="#f4faff", gain=0.62, texture=0.50, flat=1.30),
                cloud(34, 210, 340, 120, 24, 7, 50, 96, soft=0.22, blur=12,
                      color="#ffffff", gain=0.26, texture=0.5)],
        motifs=[("sun", dict(cx=378, cy=96, r=22)),
                ("rays", 378, 96, dict(k=0.05, reach=200))],
        vignette=0.20,
        city_scrim=0.0,
        palette="light",
    )),

    ("partly-cloudy-day-dark", dict(
        zh="多云（深色备选）", codes="2",
        base=[(0.0, "#122a45"), (0.42, "#0d1626"), (1.0, "#070a11")],
        lights=[(404, 66, 280, 280, "#2e6be6", 0.34, 1.4),
                (404, 62, 96, 96, "#f7b32b", 0.22, 2.0)],
        clouds=[cloud(31, 296, 84, 112, 38, 12, 34, 80, soft=0.34, blur=8,
                      color="#a7bfdd", gain=1.20),
                cloud(32, 104, 232, 122, 36, 9, 40, 88, soft=0.16, blur=14,
                      color="#6e89ab", gain=0.85)],
        motifs=[("rays", 404, 62, dict(k=0.05, reach=250))],
        alt=True,
    )),

    ("partly-cloudy-night", dict(
        zh="多云（夜间）", codes="2 · 夜",
        base=[(0.0, "#0c1730"), (0.42, "#0a1222"), (1.0, "#06080f")],
        lights=[(360, 72, 260, 260, "#2b3a6e", 0.38, 1.4),
                (392, 62, 72, 72, "#cdd8ff", 0.22, 1.8)],
        clouds=[cloud(41, 280, 96, 110, 36, 11, 34, 78, soft=0.32, blur=9,
                      color="#5f7099", gain=1.05),
                cloud(42, 122, 238, 118, 34, 9, 40, 86, soft=0.14, blur=14,
                      color="#44557c", gain=0.85)],
        motifs=[("stars", dict(n=110)), ("moon", dict(cx=392, cy=62, r=19))],
    )),

    # ---------------- 阴 ----------------
    ("cloudy", dict(
        zh="阴", codes="3",
        base=[(0.0, "#1e2733"), (0.42, "#141b25"), (1.0, "#0a0e14")],
        lights=[(240, 26, 344, 220, "#6b7f96", 0.26, 1.4),
                (110, 130, 220, 200, "#44566c", 0.20, 1.6)],
        clouds=[cloud(51, 132, 96, 150, 40, 13, 44, 96, soft=0.40, blur=10,
                      color="#a3b4c8", gain=1.20),
                cloud(52, 356, 140, 140, 36, 11, 40, 88, soft=0.34, blur=11,
                      color="#8ea2b8", gain=1.05),
                cloud(53, 210, 268, 160, 34, 9, 40, 80, soft=0.18, blur=15,
                      color="#5e7186", gain=0.8)],
        motifs=[],
    )),

    ("fog", dict(
        zh="雾", codes="45 / 48",
        base=[(0.0, "#1c242d"), (0.42, "#151c23"), (1.0, "#0b1015")],
        lights=[(240, 210, 340, 260, "#93a7b5", 0.20, 1.4)],
        clouds=[cloud(61, 240, 306, 190, 60, 9, 70, 140, soft=0.16, blur=20,
                      color="#9fb0bd", gain=0.85, texture=0.6)],
        motifs=[("fog", dict(k=0.30, layers=[(300, 52, 0.55), (378, 44, 0.70),
                                             (238, 34, 0.40), (170, 24, 0.22)]))],
        city_scrim=0.24,
        days_scrim=0.0,
    )),

    # ---------------- 雨 ----------------
    ("drizzle", dict(
        zh="毛毛雨", codes="51 / 53 / 55",
        base=[(0.0, "#10263a"), (0.42, "#0b1720"), (1.0, "#070b12")],
        lights=[(240, 40, 330, 230, "#2b86c8", 0.30, 1.4),
                (150, 96, 200, 180, "#3aa8c0", 0.18, 1.6)],
        clouds=[cloud(71, 138, 76, 140, 34, 11, 40, 88, soft=0.32, blur=10,
                      color="#8fb0cc", gain=1.05),
                cloud(72, 352, 108, 130, 32, 9, 40, 86, soft=0.24, blur=12,
                      color="#6f8fa8", gain=0.9)],
        motifs=[rain(120, 9, 26, 1.0, 0.30),
                rain(70, 9, 16, 0.8, 0.18, y0=190, blur=1.7)],
    )),

    ("rain", dict(
        zh="雨", codes="61 / 63 / 80 / 81",
        base=[(0.0, "#0e1f30"), (0.42, "#0a141d"), (1.0, "#060a10")],
        lights=[(240, 30, 340, 240, "#3a8fd6", 0.34, 1.4),
                (392, 120, 210, 220, "#22689e", 0.22, 1.6)],
        clouds=[cloud(81, 142, 70, 150, 36, 12, 42, 92, soft=0.36, blur=9,
                      color="#9dbcd8", gain=1.20),
                cloud(82, 358, 112, 130, 32, 10, 40, 86, soft=0.26, blur=11,
                      color="#7794ad", gain=1.0)],
        motifs=[rain(210, 13, 46, 1.3, 0.34),
                rain(100, 13, 28, 0.9, 0.22, y0=210, blur=1.8)],
    )),

    ("overcast-rain", dict(
        zh="大雨 / 暴雨", codes="65 / 82",
        base=[(0.0, "#18222d"), (0.42, "#101720"), (1.0, "#07090d")],
        lights=[(240, 22, 350, 220, "#56687c", 0.26, 1.4),
                (84, 112, 220, 200, "#3b4e62", 0.20, 1.6)],
        clouds=[cloud(91, 130, 58, 160, 40, 14, 46, 100, soft=0.42, blur=9,
                      color="#aebdcb", gain=1.25),
                cloud(92, 366, 104, 140, 32, 10, 44, 90, soft=0.30, blur=10,
                      color="#8a99a8", gain=1.05)],
        motifs=[rain(280, 15, 54, 1.5, 0.32),
                rain(130, 15, 32, 1.0, 0.22, y0=225, blur=2.0)],
    )),

    ("thunderstorms-rain", dict(
        zh="雷阵雨", codes="95 - 99",
        base=[(0.0, "#16112b"), (0.42, "#0c0a18"), (1.0, "#05060c")],
        lights=[(352, 44, 300, 240, "#7a5be0", 0.28, 1.4),
                (120, 60, 240, 200, "#463a92", 0.24, 1.6)],
        clouds=[cloud(101, 138, 56, 160, 42, 15, 46, 104, soft=0.44, blur=9,
                      color="#9a92c8", gain=1.20),
                cloud(102, 362, 112, 135, 30, 10, 42, 88, soft=0.26, blur=12,
                      color="#6a5f9c", gain=1.0)],
        motifs=[("bolt", dict(seed=103, x_top=330, y_top=66, y_bottom=306)),
                rain(230, 17, 50, 1.4, 0.28),
                rain(110, 17, 30, 1.0, 0.20, y0=235, blur=1.9)],
    )),

    # ---------------- 雪 ----------------
    ("snow", dict(
        zh="雪", codes="71 - 77 / 85 / 86",
        base=[(0.0, "#182634"), (0.42, "#111a23"), (1.0, "#080c12")],
        lights=[(240, 32, 340, 230, "#79b4e0", 0.28, 1.4),
                (118, 92, 220, 200, "#4a7fa8", 0.20, 1.6)],
        clouds=[cloud(111, 140, 74, 150, 36, 11, 42, 92, soft=0.32, blur=10,
                      color="#a9c6dd", gain=1.15),
                cloud(112, 356, 116, 130, 30, 9, 40, 86, soft=0.24, blur=12,
                      color="#88a8c0", gain=0.95)],
        motifs=[snow(170, 0.9, 2.2, 0.55), snow(18, 2.6, 4.2, 0.12)],
    )),

    ("sleet", dict(
        zh="雨夹雪", codes="66 / 67 / 56 / 57",
        base=[(0.0, "#0f1d28"), (0.42, "#0b141b"), (1.0, "#06090d")],
        lights=[(240, 36, 330, 225, "#3e8fb0", 0.28, 1.4),
                (380, 104, 210, 200, "#2f6f8a", 0.18, 1.6)],
        clouds=[cloud(121, 140, 72, 148, 34, 11, 42, 90, soft=0.34, blur=10,
                      color="#93b3c9", gain=1.10),
                cloud(122, 358, 116, 128, 30, 9, 40, 86, soft=0.24, blur=12,
                      color="#7796ac", gain=0.95)],
        motifs=[rain(130, 10, 34, 1.1, 0.24),
                snow(95, 1.0, 2.4, 0.50),
                snow(20, 2.6, 4.4, 0.12)],
    )),

    ("partly-cloudy-day-rain", dict(
        zh="阵雨", codes="80 / 81 / 82 (白天) / 51-55",
        base=[(0.0, "#5595c0"), (0.42, "#77b2d6"), (0.75, "#9dcce7"), (1.0, "#b4d9ee")],
        lights=[(372, 110, 200, 160, "#ffe9c0", 0.14, 1.4),
                (372, 110, 86, 86, "#fff0cc", 0.12, 1.9)],
        clouds=[cloud(131, 108, 156, 88, 30, 9, 60, 104, soft=1.00, blur=5,
                      color="#243747", gain=0.50, dark=True, texture=0.5, flat=1.4),
                cloud(132, 350, 300, 50, 20, 7, 48, 88, soft=0.70, blur=6,
                      color="#ffffff", gain=0.50, texture=0.45, flat=1.3)],
        motifs=[("sun", dict(cx=372, cy=110, r=18)),
                ("rays", 372, 110, dict(k=0.04, reach=180)),
                rain(120, 11, 30, 1.2, 0.26, y0=150)],
        vignette=0.20,
        city_scrim=0.0,
        palette="light",
    )),

    ("partly-cloudy-day-rain-dark", dict(
        zh="阵雨（深色备选）", codes="80 / 81 / 82 (白天) / 51-55",
        base=[(0.0, "#0f1e30"), (0.42, "#0b1420"), (1.0, "#070a11")],
        lights=[(400, 62, 270, 270, "#2e6be6", 0.30, 1.4),
                (400, 60, 90, 90, "#f7b32b", 0.22, 2.0)],
        clouds=[cloud(131, 292, 78, 115, 38, 12, 34, 84, soft=0.36, blur=8,
                      color="#a7bfdd", gain=1.25),
                cloud(132, 116, 210, 122, 34, 9, 40, 86, soft=0.18, blur=14,
                      color="#6e89ab", gain=0.9)],
        motifs=[("sun", dict(cx=400, cy=60, r=16)),
                ("rays", 400, 60, dict(k=0.05, reach=240)),
                rain(100, 11, 30, 1.1, 0.24, y0=120)],
        alt=True,
    )),
])
