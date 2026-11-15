# -*- coding: utf-8 -*-
"""
mochi · Bilibili 粉丝数背景 · 480x480 · ST7701S RGB565

按固件里真实存在的字体排版：
  font_digits_106 —— 每位宽 64px，数字高 77~80px，基线在 by=-79
  font_ui_29      —— 「粉丝」这类标签，最高 31px

所以数字槽按「7 位 = 448px」预留：x 16..464，居中 240，基线 y=300。

用法：
  python gen.py            # 生成干净版 + 无文字版
  python gen.py --only plain
"""
import argparse
import math
import os
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from bili_logo import mask as logo_mask

W = H = 480
S = 2                      # 矢量层 2 倍超采样，缩回去才没有锯齿
W2, H2 = W * S, H * S
YY, XX = np.mgrid[0:H2, 0:W2].astype(np.float32) / S   # 用 480 空间的坐标

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FONT = r"C:\Windows\Fonts\msyh.ttc"
FONT_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"

# ---- 版式常量（README 里必须和这里一致）----
NUM_BASELINE = 300         # 数字基线 y
NUM_CENTER_X = 240
DIGIT_ADV = 64             # 每位宽度
DIGIT_H = 80               # 数字高度
MAX_DIGITS = 7             # 16..464

# ---- 品牌色 ----
PINK = (251, 114, 153)
CYAN = (0, 161, 214)


def hx(s):
    s = s.lstrip("#")
    return np.array([int(s[i:i + 2], 16) for i in (0, 2, 4)], np.float32) / 255.0


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def vgrad(stops):
    stops = sorted(stops, key=lambda s: s[0])
    pos = np.array([s[0] for s in stops], np.float32)
    cols = np.array([hx(s[1]) for s in stops], np.float32)
    t = np.arange(H, dtype=np.float32) / (H - 1)
    chans = [np.interp(t, pos, cols[:, c]).astype(np.float32) for c in range(3)]
    lin = np.stack(chans, axis=1)[:, None, :]
    lin = np.repeat(lin, W, axis=1)                      # 480 空间
    return np.repeat(np.repeat(lin, S, axis=0), S, axis=1).copy()


def glow(cx, cy, rx, ry, power=1.5):
    d2 = ((XX - cx) / rx) ** 2 + ((YY - cy) / ry) ** 2
    return np.exp(-d2 * power)


def add_light(img, color, mask, k):
    m = np.clip(mask * k, 0.0, 1.0)[..., None]
    return 1.0 - (1.0 - img) * (1.0 - color * m)


def fbm(seed, octaves=4, base=3, aspect=1.0):
    rng = np.random.default_rng(seed)
    out = np.zeros((H2, W2), np.float32)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        gw = max(2, int(base * (2 ** o) * aspect))
        gh = max(2, int(base * (2 ** o)))
        g = rng.random((gh, gw)).astype(np.float32)
        layer = Image.fromarray((g * 255).astype(np.uint8), "L").resize((W2, H2), Image.BICUBIC)
        out += amp * (np.asarray(layer, np.float32) / 255.0)
        total += amp
        amp *= 0.5
    return out / total


# --------------------------------------------------------------------------
# 矢量层
# --------------------------------------------------------------------------
def sc(x):
    return x * S


def draw_ring(d, alpha=70):
    """外圈细环 + 刻度；刻度避开中间的数字带（|y-260|<70）。"""
    cx, cy, r = 240, 240, 214
    d.ellipse([sc(cx - r), sc(cy - r), sc(cx + r), sc(cy + r)],
              outline=(251, 114, 153, alpha), width=int(1.4 * S))
    for deg in range(0, 360, 10):
        a = math.radians(deg)
        ex, ey = cx + math.cos(a) * r, cy + math.sin(a) * r
        if abs(ey - 260) < 74 and abs(math.cos(a)) > 0.55:
            continue                      # 数字左右两侧留空
        major = (deg % 30 == 0)
        r0 = r - (7 if major else 3.5)
        d.line([sc(cx + math.cos(a) * r0), sc(cy + math.sin(a) * r0), sc(ex), sc(ey)],
               fill=(251, 114, 153, 120 if major else 60), width=int((1.3 if major else 1.0) * S))


def draw_logo(layer, cx=240, cy=112, size=104):
    """B 站官方 18x18 图标，等比放大后上色（矢量光栅化，边缘干净）。

    layer 是 2 倍超采样的画布，所以尺寸和落点都要乘 S。
    """
    px = int(size * S)
    m = logo_mask(px, ss=3)
    tint = Image.new("RGBA", (px, px), PINK + (255,))
    tint.putalpha(m.point(lambda v: int(v * 0.96)))
    layer.alpha_composite(tint, (int(sc(cx - size / 2.0)), int(sc(cy - size / 2.0))))


def draw_text_layer(layer, with_text=True):
    d = ImageDraw.Draw(layer)

    # 机身外的一圈柔光，让 logo 有「亮着」的感觉
    glow_l = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
    ImageDraw.Draw(glow_l).ellipse([sc(240 - 84), sc(116 - 76), sc(240 + 84), sc(116 + 76)],
                                   fill=(251, 114, 153, 42))
    glow_l = glow_l.filter(ImageFilter.GaussianBlur(20 * S))
    layer.alpha_composite(glow_l)

    draw_ring(d)
    draw_logo(layer)

    if not with_text:
        return

    # ---- BILIBILI 字标（逐字摆放做字距）----
    f_brand = ImageFont.truetype(FONT_BOLD, int(21 * S))
    label = "BILIBILI"
    spacing = 5.0
    widths = [d.textlength(ch, font=f_brand) / S for ch in label]
    total = sum(widths) + spacing * (len(label) - 1)
    x = 240 - total / 2
    for ch, wd in zip(label, widths):
        d.text((sc(x), sc(186)), ch, font=f_brand, fill=(251, 114, 153, 205), anchor="ls")
        x += wd + spacing

    # 字标两侧的小短线
    for dx in (-1, 1):
        x0 = 240 + dx * (total / 2 + 16)
        d.line([sc(x0), sc(178), sc(x0 + dx * 26), sc(178)],
               fill=(251, 114, 153, 120), width=int(1.6 * S))

    # ---- 「粉丝」标签 ----
    f_lab = ImageFont.truetype(FONT, int(33 * S))
    d.text((sc(240), sc(366)), "粉丝", font=f_lab, fill=(236, 240, 246, 235), anchor="ms")

    # 标签两侧的粉色点
    for dx in (-1, 1):
        cx = 240 + dx * 58
        d.ellipse([sc(cx) - 3 * S, sc(360) - 3 * S, sc(cx) + 3 * S, sc(360) + 3 * S],
                  fill=(251, 114, 153, 210))

    # ---- FANS 小字 ----
    f_small = ImageFont.truetype(FONT, int(15 * S))
    txt = "F A N S"
    d.text((sc(240), sc(396)), txt, font=f_small, fill=(0, 161, 214, 190), anchor="ms")


# --------------------------------------------------------------------------
# 背景
# --------------------------------------------------------------------------
def render(with_text=True):
    img = vgrad([(0.0, "#0a0a12"), (0.42, "#121020"), (0.78, "#1a1120"), (1.0, "#120c16")])

    # 左上冷光、右下粉光：B 站的蓝 + 粉
    img = add_light(img, hx("#00a1d6"), glow(86, 74, 250, 230, 1.6), 0.30)
    img = add_light(img, hx("#fb7299"), glow(410, 402, 300, 250, 1.5), 0.24)
    img = add_light(img, hx("#fb7299"), glow(240, 470, 300, 110, 1.8), 0.10)

    # 数字带（y 220..300）压一层很淡的暗，保证白字干净
    band = smoothstep(184, 232, YY) * (1.0 - smoothstep(292, 340, YY))
    img *= (1.0 - 0.20 * band)[..., None]

    # 上沿和边缘收暗
    img *= (1.0 - 0.30 * smoothstep(78, 0, YY))[..., None]
    r = np.sqrt((XX - 240) ** 2 + (YY - 240) ** 2) / 240.0
    img *= (1.0 - 0.46 * smoothstep(0.70, 1.10, r))[..., None]

    # 细颗粒，压 RGB565 的色带
    rng = np.random.default_rng(zlib.crc32(b"fan" + bytes([with_text])) & 0x7FFFFFFF)
    img += (rng.random((H2, W2, 1)).astype(np.float32) - 0.5) * 0.006

    base = Image.fromarray((np.clip(img, 0, 1) * 255).round().astype(np.uint8), "RGB").convert("RGBA")

    layer = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
    draw_text_layer(layer, with_text)
    base.alpha_composite(layer)

    out = base.convert("RGB").resize((W, H), Image.LANCZOS)
    return out


def dither565(pim):
    a = np.asarray(pim, np.float32)
    rng = np.random.default_rng(7)
    org = rng.random((H, W, 3)).astype(np.float32) - 0.5
    r = np.clip(np.round((a[..., 0] + org[..., 0] * 8.0) / 8.0) * 8.0, 0, 255)
    g = np.clip(np.round((a[..., 1] + org[..., 1] * 4.0) / 4.0) * 4.0, 0, 255)
    b = np.clip(np.round((a[..., 2] + org[..., 2] * 8.0) / 8.0) * 8.0, 0, 255)
    return Image.fromarray(np.stack([r, g, b], -1).astype(np.uint8), "RGB")


def rgb565_bytes(pim):
    a = np.asarray(pim, np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    return v.astype("<u2").tobytes()


VARIANTS = {"fan-bg": True, "fan-bg-plain": False}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--no565", action="store_true")
    args = ap.parse_args()

    for d in ("png", "jpg", "rgb565"):
        os.makedirs(os.path.join(ROOT, d), exist_ok=True)

    for name, with_text in VARIANTS.items():
        if args.only and args.only not in name:
            continue
        pim = render(with_text)
        if not args.no565:
            pim = dither565(pim)
        pim.save(os.path.join(ROOT, "png", name + ".png"), optimize=True)
        pim.save(os.path.join(ROOT, "jpg", name + ".jpg"), quality=90, subsampling=0)
        with open(os.path.join(ROOT, "rgb565", name + ".bin"), "wb") as f:
            f.write(rgb565_bytes(pim))
        print("ok", name)


if __name__ == "__main__":
    main()
