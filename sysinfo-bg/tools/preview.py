# -*- coding: utf-8 -*-
"""
预览：把弧、百分比、四块磁贴按 layout.py 的坐标画上去；再出一张标注图。

用法：python preview.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

import layout as L

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PNG = os.path.join(ROOT, "png")
OUT = os.path.join(ROOT, "preview")
FONT = r"C:\Windows\Fonts\msyh.ttc"
FONT_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"
W = 480


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, size)


def hxrgb(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


def digit_ink(ch, ah=220):
    f = font(ah, True)
    tmp = Image.new("RGBA", (ah * 3, ah * 3), (0, 0, 0, 0))
    ImageDraw.Draw(tmp).text((ah, ah), ch, font=f, fill=(255, 255, 255, 255))
    return tmp.crop(tmp.getbbox())


def draw_pct(img, pct, baseline=L.PCT_BASELINE, h=43, adv=34, gap=10):
    """把「42%」作为一组居中到 (240, 环心)。"""
    txt = str(pct)
    d = ImageDraw.Draw(img)
    f_pct = font(29)
    pw = d.textlength("%", font=f_pct)
    num_w = adv * len(txt)
    total = num_w + gap + pw
    x0 = L.RING_CX - total / 2.0

    for i, ch in enumerate(txt):
        ink = digit_ink(ch)
        scale = h / ink.height
        w = max(1, int(round(ink.width * scale)))
        ink = ink.resize((w, h), Image.LANCZOS)
        cx = x0 + i * adv + adv / 2.0
        img.alpha_composite(ink, (int(round(cx - w / 2.0)), int(baseline - h)))

    d.text((x0 + num_w + gap, baseline), "%", font=f_pct,
           fill=hxrgb(L.ACCENT) + (255,), anchor="ls")


def draw_arc(img, pct, color):
    d = ImageDraw.Draw(img)
    cx, cy, r, th = L.RING_CX, L.RING_CY, L.RING_R, L.RING_THICK
    a0 = L.ARC_START
    a1 = a0 + 360.0 * pct / 100.0
    box = [cx - r, cy - r, cx + r, cy + r]
    d.arc(box, start=a0, end=a1, fill=color + (255,), width=th)
    # 圆头
    for a in (a0, a1):
        x = cx + math.cos(math.radians(a)) * r
        y = cy + math.sin(math.radians(a)) * r
        d.ellipse([x - th / 2, y - th / 2, x + th / 2, y + th / 2], fill=color + (255,))


def draw_screen(pct=42, mem="3.2/5.6G", net="12M/s", vol="60%", up="2天03:41",
                labels=("内存", "网络", "音量", "开机"), caption="SYSTEM"):
    base = Image.open(os.path.join(PNG, "sysinfo.png")).convert("RGBA")
    d = ImageDraw.Draw(base)

    # 顶部小标题（逐字加字距）
    f_cap = font(L.CAPTION_SIZE)
    span = 4
    widths = [d.textlength(c, font=f_cap) for c in caption]
    total = sum(widths) + span * (len(caption) - 1)
    x = 240 - total / 2
    for c, wd in zip(caption, widths):
        d.text((x, L.CAPTION_BASELINE), c, font=f_cap, fill=hxrgb(L.TEXT_DIM) + (255,), anchor="ls")
        x += wd + span

    draw_arc(base, pct, hxrgb(L.ACCENT))
    draw_pct(base, pct)

    f_lab = font(L.TILE_LABEL_SIZE)
    f_val = font(L.TILE_VALUE_SIZE)
    cells = [(labels[0], mem), (labels[1], net), (labels[2], vol), (labels[3], up)]
    for i, (lab, val) in enumerate(cells):
        cx = L.TILE_X[i % 2]
        ly = L.ROW_A_LABEL if i < 2 else L.ROW_B_LABEL
        vy = L.ROW_A_VALUE if i < 2 else L.ROW_B_VALUE
        d.text((cx, ly), lab, font=f_lab, fill=hxrgb(L.TEXT_DIM) + (255,), anchor="ms")
        d.text((cx, vy), val, font=f_val, fill=hxrgb(L.TEXT_BRIGHT) + (255,), anchor="ms")
    return base


def mock(out, **kw):
    base = draw_screen(**kw)
    mask = Image.new("L", (W * 4, W * 4), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, W * 4 - 1, W * 4 - 1], fill=255)
    mask = mask.resize((W, W), Image.LANCZOS)
    canvas = Image.new("RGB", (W, W), (10, 11, 14))
    canvas.paste(base.convert("RGB"), (0, 0), mask)
    os.makedirs(OUT, exist_ok=True)
    canvas.save(out, optimize=True)
    print("mock ->", out)


def dashed_arc(d, cx, cy, r, color, step=10, dash=6):
    a = 0
    while a < 360:
        d.arc([cx - r, cy - r, cx + r, cy + r], start=a, end=min(a + dash, 360),
              fill=color, width=1)
        a += step


def guide(out):
    M, TOP = 44, 64
    CW, CH = 980, 780
    canvas = Image.new("RGB", (CW, CH), (18, 20, 25))
    d = ImageDraw.Draw(canvas)
    canvas.paste(Image.open(os.path.join(PNG, "sysinfo.png")).convert("RGB"), (M, TOP))

    def X(x):
        return M + x

    def Y(y):
        return TOP + y

    accent = hxrgb(L.ACCENT)
    pink = (255, 138, 92)
    dim = (140, 150, 165)
    line = (86, 94, 108)

    # 环
    cx, cy, r = L.RING_CX, L.RING_CY, L.RING_R
    for rr in (r - L.RING_THICK / 2, r + L.RING_THICK / 2):
        dashed_arc(d, X(cx), Y(cy), rr, accent, step=12, dash=6)
    d.line([X(cx - r - 14), Y(cy), X(cx + r + 14), Y(cy)], fill=(120, 130, 145), width=1)
    d.line([X(cx), Y(cy - r - 14), X(cx), Y(cy + r + 14)], fill=(120, 130, 145), width=1)
    d.ellipse([X(cx) - 2, Y(cy) - 2, X(cx) + 2, Y(cy) + 2], fill=accent)

    # 基线
    d.line([X(150), Y(L.CAPTION_BASELINE), X(330), Y(L.CAPTION_BASELINE)], fill=dim, width=1)
    d.line([X(cx - 52), Y(L.PCT_BASELINE), X(cx + 52), Y(L.PCT_BASELINE)], fill=pink, width=1)

    # 磁贴
    for cx_, ly, vy in ((L.TILE_X[0], L.ROW_A_LABEL, L.ROW_A_VALUE),
                        (L.TILE_X[1], L.ROW_A_LABEL, L.ROW_A_VALUE),
                        (L.TILE_X[0], L.ROW_B_LABEL, L.ROW_B_VALUE),
                        (L.TILE_X[1], L.ROW_B_LABEL, L.ROW_B_VALUE)):
        d.line([X(cx_ - 46), Y(ly), X(cx_ + 46), Y(ly)], fill=(120, 128, 140), width=1)
        d.line([X(cx_ - 46), Y(vy), X(cx_ + 46), Y(vy)], fill=(210, 220, 230), width=1)
    # 列中线
    for cx_ in L.TILE_X:
        d.line([X(cx_), Y(286), X(cx_), Y(424)], fill=(70, 78, 92), width=1)

    # 右侧图例
    LX = X(492)
    legend = [
        (46, "SYSTEM：22px，基线 46", dim),
        (150, "CPU 环：圆心 (240,150)，r=92，线宽 10", accent),
        (172, "百分比：font_num_56，基线 172", pink),
        (300, "A 行标签：22px，基线 300", dim),
        (334, "A 行数值：29px，基线 334", (235, 240, 246)),
        (380, "B 行标签：22px，基线 380", dim),
        (414, "B 行数值：29px，基线 414", (235, 240, 246)),
    ]
    for y, text, col in legend:
        d.line([X(478) + 6, Y(y), LX - 10, Y(y)], fill=line, width=1)
        d.text((LX, Y(y)), text, font=font(14), fill=col, anchor="lm")

    d.text((M, 34), "mochi · 设备信息页 · 480×480", font=font(24, True),
           fill=(236, 240, 246), anchor="ls")
    y0 = CH - 88
    d.text((M, y0), "· 两列中心 x = 136 / 344；磁贴标签居中，数值 29px 同轴",
           font=font(14), fill=dim, anchor="ls")
    d.text((M, y0 + 26), "· 弧用 gfx_arc(240,150,92,10,-90,-90+3.6*pct,色)，和底槽严丝合缝",
           font=font(14), fill=dim, anchor="ls")
    d.text((M, y0 + 52), "· 百分比 = 0~100，三位数（100%）在环内仍有余量（组宽 128 < 内径 174）",
           font=font(14), fill=dim, anchor="ls")

    canvas.save(out, optimize=True)
    print("guide ->", out)


if __name__ == "__main__":
    mock(os.path.join(OUT, "01-mock.png"))
    mock(os.path.join(OUT, "02-mock-high.png"), pct=100, mem="6.9/8.0G",
         net="310K/s", vol="92%", up="2d03:41")
    mock(os.path.join(OUT, "03-mock-ascii.png"), labels=("MEM", "NET", "VOL", "UP"),
         caption="DEVICE", pct=7, mem="1.1/5.6G", net="36K/s", vol="18%", up="0d 04:12")
    guide(os.path.join(OUT, "00-guide.png"))
