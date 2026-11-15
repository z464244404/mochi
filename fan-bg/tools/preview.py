# -*- coding: utf-8 -*-
"""
预览：把粉丝数按真实字体度量画上去，外加一张带尺寸标注的版式图。

数字度量来自固件 fonts.c（font_digits_106）：
  每位 64px，字高 77~80px（0/3/8 是 80），基线 y=300，整体居中 x=240。

用法：python preview.py
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PNG = os.path.join(ROOT, "png")
OUT = os.path.join(ROOT, "preview")
FONT = r"C:\Windows\Fonts\msyh.ttc"
FONT_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"

W = 480
NUM_BASELINE = 300
NUM_CENTER_X = 240
DIGIT_ADV = 64
MAX_DIGITS = 7

# fonts.c 里每个数字的实际高度
GLYPH_H = {0: 80, 1: 77, 2: 79, 3: 80, 4: 78, 5: 79, 6: 79, 7: 77, 8: 80, 9: 79}

COL_NUM = (255, 255, 255, 255)


def font(size, bold=True):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, size)


def digit_ink(ch, ah=200):
    """把单个数字渲染成大图，返回裁掉空白后的墨迹（RGBA）。"""
    f = font(ah)
    tmp = Image.new("RGBA", (ah * 2, ah * 2), (0, 0, 0, 0))
    ImageDraw.Draw(tmp).text((ah // 2, ah // 2), ch, font=f, fill=(255, 255, 255, 255))
    bbox = tmp.getbbox()
    return tmp.crop(bbox)


def draw_number(img, text):
    """按固件度量画数字：每位 64px，字高取 GLYPH_H，底边贴基线 300。"""
    n = len(text)
    x0 = NUM_CENTER_X - n * DIGIT_ADV / 2.0
    for i, ch in enumerate(text):
        ink = digit_ink(ch)
        th = GLYPH_H[int(ch)]
        scale = th / ink.height
        w = max(1, int(round(ink.width * scale)))
        ink = ink.resize((w, th), Image.LANCZOS)
        cx = x0 + i * DIGIT_ADV + DIGIT_ADV / 2.0
        img.alpha_composite(ink, (int(round(cx - w / 2.0)), int(NUM_BASELINE - th)))


def mockup(text, name, out):
    base = Image.open(os.path.join(PNG, name + ".png")).convert("RGBA")
    draw_number(base, text)
    # 圆屏裁切
    mask = Image.new("L", (W * 4, W * 4), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, W * 4 - 1, W * 4 - 1], fill=255)
    mask = mask.resize((W, W), Image.LANCZOS)
    out_img = Image.new("RGB", (W, W), (10, 11, 14))
    out_img.paste(base.convert("RGB"), (0, 0), mask)
    os.makedirs(OUT, exist_ok=True)
    out_img.save(out, optimize=True)
    print("mock ->", out)


# --------------------------------------------------------------------------
# 版式标注图
# --------------------------------------------------------------------------
def dashed_rect(d, box, color, width=2, dash=9, gap=7):
    x0, y0, x1, y1 = box
    def seg(x, y, dx, dy, length):
        n = int(length)
        p = 0
        while p < n:
            e = min(p + dash, n)
            d.line([x + dx * p, y + dy * p, x + dx * e, y + dy * e], fill=color, width=width)
            p = e + gap
    seg(x0, y0, 1, 0, x1 - x0)
    seg(x1, y0, 0, 1, y1 - y0)
    seg(x1, y1, -1, 0, x1 - x0)
    seg(x0, y1, 0, -1, y1 - y0)


def arrow(d, p0, p1, color, width=2, head=7):
    d.line([p0[0], p0[1], p1[0], p1[1]], fill=color, width=width)
    ang = __import__("math").atan2(p1[1] - p0[1], p1[0] - p0[0])
    import math as _m
    for da in (2.6, -2.6):
        d.line([p1[0], p1[1],
                p1[0] + _m.cos(ang + da) * head, p1[1] + _m.sin(ang + da) * head],
               fill=color, width=width)


def guide(out):
    M = 60                       # 图片在画布上的位置
    CW, CH = 760, 780
    canvas = Image.new("RGB", (CW, CH), (18, 20, 25))
    d = ImageDraw.Draw(canvas)

    bg = Image.open(os.path.join(PNG, "fan-bg.png")).convert("RGB")
    canvas.paste(bg, (M, M))

    def X(x):
        return M + x

    def Y(y):
        return M + y

    pink = (251, 114, 153)
    cyan = (0, 161, 214)
    dim = (140, 150, 165)

    # 可视圆
    d.ellipse([M, M, M + 480, M + 480], outline=(70, 78, 92), width=1)

    # 数字槽（7 位）+ 基线 + 中心线
    dashed_rect(d, (X(16), Y(220), X(464), Y(300)), pink, width=2)
    d.line([X(0), Y(NUM_BASELINE), X(480), Y(NUM_BASELINE)], fill=(255, 255, 255), width=1)
    d.line([X(NUM_CENTER_X), Y(210), X(NUM_CENTER_X), Y(310)], fill=(255, 255, 255), width=1)

    # ---- 右侧：竖直尺寸 ----
    ax = X(500)
    arrow(d, (ax, Y(220)), (ax, Y(300)), cyan)
    d.line([X(464), Y(220), ax + 6, Y(220)], fill=(90, 100, 115), width=1)
    d.line([X(464), Y(300), ax + 6, Y(300)], fill=(90, 100, 115), width=1)
    d.text((ax + 12, Y(214)), "字高 80px", font=font(15, False), fill=cyan, anchor="lt")
    d.text((ax + 12, Y(234)), "（77~80，", font=font(13, False), fill=dim, anchor="lt")
    d.text((ax + 12, Y(252)), "随数字而定）", font=font(13, False), fill=dim, anchor="lt")
    d.text((ax + 12, Y(296)), "基线 y=300", font=font(15, True), fill=pink, anchor="lt")
    d.text((ax + 12, Y(318)), "即 gfx_text 的 y", font=font(13, False), fill=dim, anchor="lt")
    d.text((ax + 12, Y(336)), "居中 x=240", font=font(13, False), fill=dim, anchor="lt")

    # ---- 下方：水平尺寸 ----
    ay = Y(520)
    d.line([X(16), Y(480), X(16), ay + 6], fill=(90, 100, 115), width=1)
    d.line([X(464), Y(480), X(464), ay + 6], fill=(90, 100, 115), width=1)
    arrow(d, (X(16), ay), (X(464), ay), cyan)
    d.text((X(240), ay + 12), "数字槽 448px = 7 位 × 64px", font=font(16, False),
           fill=cyan, anchor="mt")
    d.text((X(16), ay - 22), "x=16", font=font(13, False), fill=cyan, anchor="lt")
    d.text((X(464), ay - 22), "x=464", font=font(13, False), fill=cyan, anchor="rt")

    # ---- 标题与说明 ----
    d.text((M, 22), "mochi · Bilibili 粉丝数背景 · 480×480", font=font(24, True),
           fill=(236, 240, 246), anchor="ls")
    d.text((M, CH - 108), "· 白色数字水平居中在 x=240，底边压在基线 y=300 上",
           font=font(14, False), fill=dim, anchor="ls")
    d.text((M, CH - 82), "· 最多 7 位：x 16~464，都在 r=240 的圆屏安全区内",
           font=font(14, False), fill=dim, anchor="ls")
    d.text((M, CH - 56), "· 「粉丝」标签已画在背景里，基线 y=366，固件只需画数字",
           font=font(14, False), fill=dim, anchor="ls")

    canvas.save(out, optimize=True)
    print("guide ->", out)


if __name__ == "__main__":
    mockup("25839", "fan-bg", os.path.join(OUT, "fan-bg-mock-5.png"))
    mockup("1234567", "fan-bg", os.path.join(OUT, "fan-bg-mock-7.png"))
    mockup("999", "fan-bg", os.path.join(OUT, "fan-bg-mock-3.png"))
    mockup("25839", "fan-bg-plain", os.path.join(OUT, "fan-bg-mock-plain.png"))
    guide(os.path.join(OUT, "fan-bg-guide.png"))
