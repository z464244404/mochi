# -*- coding: utf-8 -*-
"""
预览：把封面、曲名、歌手、进度条、时间按 layout.py 的坐标画上去。
外加一张标注尺寸的版式图。

用法：python preview.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

import layout as L
from gen import THEMES

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PNG = os.path.join(ROOT, "png")
OUT = os.path.join(ROOT, "preview")
FONT = r"C:\Windows\Fonts\msyh.ttc"
FONT_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"
W = 480


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, size)


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


# --------------------------------------------------------------------------
# 封面占位
# --------------------------------------------------------------------------
COVER_ART = {
    "vinyl": ("#f0b96a", "#8a4a1e", "#2a1408"),
    "neon": ("#ff7ab8", "#7a2fd0", "#160a28"),
    "sunset": ("#ffb08a", "#c2452f", "#2a0e12"),
    "mint": ("#8ef0d2", "#1f8f78", "#08201b"),
    "ocean": ("#7fc8ff", "#1f5f9e", "#08131f"),
}


def draw_cover(img, theme, angle=0.0):
    """240 圆的封面占位：渐变 + 两圈同心环 + 中心孔 + 一道「旋转」反光。"""
    c0, c1, c2 = COVER_ART[theme]
    cx, cy, r = L.COVER_CX, L.COVER_CY, L.COVER_R
    ss = 4
    size = L.COVER_D * ss
    art = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(art)
    a0, a1 = tuple(int(c0[i:i + 2], 16) for i in (1, 3, 5)), \
             tuple(int(c1[i:i + 2], 16) for i in (1, 3, 5))
    a2 = tuple(int(c2[i:i + 2], 16) for i in (1, 3, 5))
    # 渐变底
    for y in range(size):
        t = y / (size - 1)
        if t < 0.55:
            k = t / 0.55
            col = tuple(int(a0[i] + (a1[i] - a0[i]) * k) for i in range(3))
        else:
            k = (t - 0.55) / 0.45
            col = tuple(int(a1[i] + (a2[i] - a1[i]) * k) for i in range(3))
        d.line([(0, y), (size, y)], fill=col + (255,))
    # 同心环
    for rr in (int(size * 0.40), int(size * 0.28)):
        d.ellipse([size // 2 - rr, size // 2 - rr, size // 2 + rr, size // 2 + rr],
                  outline=(255, 255, 255, 40), width=2 * ss)
    # 中心孔
    hr = int(size * 0.055)
    d.ellipse([size // 2 - hr, size // 2 - hr, size // 2 + hr, size // 2 + hr],
              fill=(12, 12, 14, 235))
    # 旋转反光
    ang = math.radians(angle)
    gx = size // 2 + math.cos(ang) * size * 0.30
    gy = size // 2 + math.sin(ang) * size * 0.30
    gl = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(gl).ellipse([gx - size * 0.22, gy - size * 0.22,
                                gx + size * 0.22, gy + size * 0.22],
                               fill=(255, 255, 255, 26))
    gl = gl.filter(ImageFilter.GaussianBlur(size * 0.06))
    art.alpha_composite(gl)

    mask = Image.new("L", (size * 2, size * 2), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, size * 2 - 1, size * 2 - 1], fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)
    art.putalpha(mask)
    art = art.resize((L.COVER_D, L.COVER_D), Image.LANCZOS)
    img.alpha_composite(art, (cx - r, cy - r))


# --------------------------------------------------------------------------
# 播放控件
# --------------------------------------------------------------------------
def draw_player(img, theme, title="起风了", artist="买辣椒也用券",
                pos=0.32, playing=True, elapsed="1:23", total="4:45"):
    d = ImageDraw.Draw(img)

    draw_cover(img, theme, angle=35)

    # 曲名 29px
    f_title = font(L.TITLE_SIZE, True)
    tw = d.textlength(title, font=f_title)
    if tw > L.TEXT_MAX_W:                     # 截断加 ..
        while tw > L.TEXT_MAX_W and len(title) > 1:
            title = title[:-1]
            tw = d.textlength(title + "..", font=f_title)
        title += ".."
    d.text((L.COVER_CX, L.TITLE_BASELINE), title, font=f_title,
           fill=(244, 247, 248), anchor="ms")

    # 歌手 22px
    f_art = font(L.ARTIST_SIZE)
    d.text((L.COVER_CX, L.ARTIST_BASELINE), artist, font=f_art,
           fill=(150, 160, 172), anchor="ms")

    # 进度条
    knob = rgb(L.GREEN_PLAY) if playing else rgb(L.GREY_PAUSE)
    x0, x1, y = L.BAR_X0, L.BAR_X1, L.BAR_Y
    d.rounded_rectangle([x0, y - L.BAR_H // 2, x1, y + L.BAR_H // 2],
                        radius=L.BAR_H // 2, fill=(62, 68, 78))
    kx = x0 + (x1 - x0) * pos
    d.rounded_rectangle([x0, y - L.BAR_H // 2, kx, y + L.BAR_H // 2],
                        radius=L.BAR_H // 2, fill=knob)
    d.ellipse([kx - L.KNOB_R, y - L.KNOB_R, kx + L.KNOB_R, y + L.KNOB_R], fill=knob)

    # 时间
    f_time = font(L.TIME_SIZE)
    d.text((L.TIME_LEFT_X, L.TIME_BASELINE), elapsed, font=f_time,
           fill=(139, 147, 160), anchor="ls")
    d.text((L.TIME_RIGHT_X, L.TIME_BASELINE), total, font=f_time,
           fill=(139, 147, 160), anchor="rs")


def mock(theme, out, playing=True, pos=0.32, title="起风了", artist="买辣椒也用券"):
    base = Image.open(os.path.join(PNG, theme + ".png")).convert("RGBA")
    draw_player(base, theme, title=title, artist=artist, pos=pos, playing=playing)
    mask = Image.new("L", (W * 4, W * 4), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, W * 4 - 1, W * 4 - 1], fill=255)
    mask = mask.resize((W, W), Image.LANCZOS)
    canvas = Image.new("RGB", (W, W), (10, 11, 14))
    canvas.paste(base.convert("RGB"), (0, 0), mask)
    os.makedirs(OUT, exist_ok=True)
    canvas.save(out, optimize=True)
    print("mock ->", out)


# --------------------------------------------------------------------------
# 版式标注
# --------------------------------------------------------------------------
def dashed_line(d, p0, p1, color, width=1, dash=6, gap=5):
    import math as m
    length = m.hypot(p1[0] - p0[0], p1[1] - p0[1])
    n = int(length)
    p = 0
    while p < n:
        e = min(p + dash, n)
        x0 = p0[0] + (p1[0] - p0[0]) * p / n
        y0 = p0[1] + (p1[1] - p0[1]) * p / n
        x1 = p0[0] + (p1[0] - p0[0]) * e / n
        y1 = p0[1] + (p1[1] - p0[1]) * e / n
        d.line([x0, y0, x1, y1], fill=color, width=width)
        p = e + gap


def arrow(d, p0, p1, color, width=2, head=7):
    import math as m
    d.line([p0[0], p0[1], p1[0], p1[1]], fill=color, width=width)
    ang = m.atan2(p1[1] - p0[1], p1[0] - p0[0])
    for da in (2.6, -2.6):
        d.line([p1[0], p1[1],
                p1[0] + m.cos(ang + da) * head, p1[1] + m.sin(ang + da) * head],
               fill=color, width=width)


def guide(out):
    M, TOP = 40, 60
    CW, CH = 960, 780
    canvas = Image.new("RGB", (CW, CH), (18, 20, 25))
    d = ImageDraw.Draw(canvas)

    bg = Image.open(os.path.join(PNG, "vinyl.png")).convert("RGB")
    canvas.paste(bg, (M, TOP))

    def X(x):
        return M + x

    def Y(y):
        return TOP + y

    pink = (255, 138, 92)
    cyan = (84, 184, 255)
    dim = (140, 150, 165)
    line = (86, 94, 108)

    # 封面：虚线圆 + 十字
    cx, cy, r = L.COVER_CX, L.COVER_CY, L.COVER_R
    d.ellipse([X(cx - r), Y(cy - r), X(cx + r), Y(cy + r)], outline=pink, width=1)
    dashed_line(d, (X(cx - r - 14), Y(cy)), (X(cx + r + 14), Y(cy)), pink, dash=7, gap=6)
    dashed_line(d, (X(cx), Y(cy - r - 14)), (X(cx), Y(cy + r + 14)), pink, dash=7, gap=6)

    # 元素基线 / 进度条
    d.line([X(96), Y(L.TITLE_BASELINE), X(384), Y(L.TITLE_BASELINE)], fill=pink, width=1)
    d.line([X(96), Y(L.ARTIST_BASELINE), X(384), Y(L.ARTIST_BASELINE)], fill=cyan, width=1)
    d.line([X(L.BAR_X0), Y(L.BAR_Y), X(L.BAR_X1), Y(L.BAR_Y)], fill=(255, 255, 255), width=1)
    d.ellipse([X(L.BAR_X0) - 3, Y(L.BAR_Y) - 3, X(L.BAR_X0) + 3, Y(L.BAR_Y) + 3],
              outline=(255, 255, 255), width=1)
    d.ellipse([X(L.BAR_X1) - 3, Y(L.BAR_Y) - 3, X(L.BAR_X1) + 3, Y(L.BAR_Y) + 3],
              outline=(255, 255, 255), width=1)
    d.line([X(L.TIME_LEFT_X), Y(L.TIME_BASELINE), X(L.TIME_RIGHT_X), Y(L.TIME_BASELINE)],
           fill=(120, 128, 140), width=1)

    # 右侧图例：每条对齐到对应元素的 y
    LX = X(480) + 34
    legend = [
        (150, "封面：直径 240，圆心 (240,150)，旋转", pink),
        (310, "曲名：29px，基线 310，居中", pink),
        (344, "歌手：22px，基线 344，居中", cyan),
        (378, "进度条：y=378，x 100→380", cyan),
        (404, "时间：22px，基线 404，两端", dim),
    ]
    for y, text, col in legend:
        d.line([X(480) + 8, Y(y), LX - 8, Y(y)], fill=line, width=1)
        d.ellipse([LX - 13, Y(y) - 2, LX - 9, Y(y) + 2], fill=col)
        d.text((LX, Y(y)), text, font=font(14), fill=col, anchor="lm")

    # 底部：进度条宽度
    ay = Y(556)
    d.line([X(L.BAR_X0), Y(480), X(L.BAR_X0), ay - 6], fill=line, width=1)
    d.line([X(L.BAR_X1), Y(480), X(L.BAR_X1), ay - 6], fill=line, width=1)
    arrow(d, (X(L.BAR_X0), ay), (X(L.BAR_X1), ay), cyan)
    d.text((X(L.BAR_X0), ay - 18), "100", font=font(12), fill=cyan, anchor="lt")
    d.text((X(L.BAR_X1), ay - 18), "380", font=font(12), fill=cyan, anchor="rt")
    d.text((X(240), ay + 10), "280px", font=font(15, True), fill=cyan, anchor="mt")

    # 标题与说明
    d.text((M, 30), "mochi · 音乐播放背景 · 480×480", font=font(24, True),
           fill=(236, 240, 246), anchor="ls")
    y0 = CH - 84
    d.text((M, y0), "· 超宽曲名按 TEXT_MAX_W=360 截断，末尾补 ..（歌手同理）",
           font=font(14), fill=dim, anchor="ls")
    d.text((M, y0 + 26), "· 播放/暂停只换旋钮颜色：绿 0x5be0bd / 灰 0x5a6270，背景不用换",
           font=font(14), fill=dim, anchor="ls")
    d.text((M, y0 + 52), "· 封面 240 时，标题顶端 y≈279，与封面底部留 9px",
           font=font(14), fill=dim, anchor="ls")

    canvas.save(out, optimize=True)
    print("guide ->", out)


def sheet(out, cell=236, gap=22):
    themes = list(THEMES)
    Wd = gap + len(themes) * (cell + gap)
    Hd = 108 + cell + 40
    canvas = Image.new("RGB", (Wd, Hd), (16, 18, 22))
    d = ImageDraw.Draw(canvas)
    d.text((gap + 2, 34), "mochi · 音乐播放背景", font=font(24, True), fill=(236, 240, 246))
    d.text((gap + 2, 66), "五种配色，版式坐标完全一致：封面 240 · 曲名 310 · 歌手 344 · 进度条 378 · 时间 404",
           font=font(13), fill=(126, 138, 153))

    mask = Image.new("L", (cell * 4, cell * 4), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, cell * 4 - 1, cell * 4 - 1], fill=255)
    mask = mask.resize((cell, cell), Image.LANCZOS)

    for i, t in enumerate(themes):
        x = gap + i * (cell + gap)
        y = 100
        im = Image.open(os.path.join(PNG, t + ".png")).convert("RGB").resize((cell, cell),
                                                                            Image.LANCZOS)
        canvas.paste(im, (x, y), mask)
        d.ellipse([x, y, x + cell - 1, y + cell - 1], outline=(48, 54, 62), width=1)
        label = THEMES_ZH[t]
        d.text((x + cell // 2, y + cell + 10), label, font=font(15), fill=(200, 208, 218),
               anchor="mt")
    canvas.save(out, optimize=True)
    print("sheet ->", out)


THEMES_ZH = {
    "vinyl": "黑胶 · 暖金",
    "neon": "霓虹 · 紫青",
    "sunset": "落日 · 橙粉",
    "mint": "薄荷 · 青绿",
    "ocean": "深海 · 蓝青",
}


if __name__ == "__main__":
    sheet(os.path.join(OUT, "00-overview.png"))
    guide(os.path.join(OUT, "01-guide.png"))
    mock("vinyl", os.path.join(OUT, "02-mock-vinyl.png"))
    mock("neon", os.path.join(OUT, "03-mock-neon.png"))
    mock("sunset", os.path.join(OUT, "04-mock-sunset.png"))
    mock("mint", os.path.join(OUT, "05-mock-mint.png"))
    mock("ocean", os.path.join(OUT, "06-mock-ocean.png"))
    mock("neon", os.path.join(OUT, "07-mock-paused.png"), playing=False, pos=0.32)
    mock("vinyl", os.path.join(OUT, "08-mock-longtitle.png"),
         title="這是一首名字特别特别长的歌曲用来测试截断", artist="群星")
