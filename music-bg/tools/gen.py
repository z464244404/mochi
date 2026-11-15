# -*- coding: utf-8 -*-
"""
mochi · 音乐播放插件背景 · 480x480 · ST7701S RGB565

五种风格，坐标全部一致（见 tools/layout.py）：
  vinyl  黑胶 · 暖金     —— 唱片沟槽，适合爵士/老歌
  neon   霓虹 · 紫青     —— 点阵 + 双向光晕，适合电子
  sunset 落日 · 橙粉     —— 地平线暖光
  mint   薄荷 · 青绿     —— 斜细线，干净
  ocean  深海 · 蓝青     —— 横向波纹

文字区（曲名/歌手/进度条/时间）统一压暗，保证白字和进度条压得住。

用法：
  python gen.py                 # 全部
  python gen.py --only neon
"""
import argparse
import math
import os
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

import layout as L

W = H = 480
S = 2
W2, H2 = W * S, H * S
YY, XX = np.mgrid[0:H2, 0:W2].astype(np.float32) / S

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


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
    lin = np.repeat(np.stack(chans, 1)[:, None, :], W, axis=1)
    return np.repeat(np.repeat(lin, S, axis=0), S, axis=1).copy()


def glow(cx, cy, rx, ry, power=1.4):
    d2 = ((XX - cx) / rx) ** 2 + ((YY - cy) / ry) ** 2
    return np.exp(-d2 * power)


def add_light(img, color, mask, k):
    m = np.clip(mask * k, 0.0, 1.0)[..., None]
    return 1.0 - (1.0 - img) * (1.0 - color * m)


def sc(v):
    return v * S


# --------------------------------------------------------------------------
# 主题
# --------------------------------------------------------------------------
THEMES = {
    "vinyl": dict(
        zh="黑胶 · 暖金",
        base=[(0.0, "#0b0b0e"), (0.45, "#141115"), (1.0, "#191418")],
        lights=[(306, 118, 270, 240, "#e8b06a", 0.16, 1.5),
                (96, 428, 260, 200, "#8a5a2a", 0.14, 1.6)],
        accent="#e8b06a", ring_k=0.22, motif="grooves",
    ),
    "neon": dict(
        zh="霓虹 · 紫青",
        base=[(0.0, "#08060f"), (0.45, "#140a22"), (1.0, "#0b0714")],
        lights=[(402, 88, 250, 230, "#ff4d9d", 0.26, 1.5),
                (66, 386, 240, 220, "#35e0ff", 0.20, 1.6)],
        accent="#ff6fb5", ring_k=0.30, motif="dots",
    ),
    "sunset": dict(
        zh="落日 · 橙粉",
        base=[(0.0, "#150c12"), (0.45, "#23101a"), (1.0, "#120a10")],
        lights=[(240, 470, 300, 220, "#ff8a4c", 0.26, 1.5),
                (404, 236, 210, 200, "#c94a72", 0.16, 1.6)],
        accent="#ff9a5c", ring_k=0.26, motif="haze",
    ),
    "mint": dict(
        zh="薄荷 · 青绿",
        base=[(0.0, "#061110"), (0.45, "#0a1a17"), (1.0, "#07110f")],
        lights=[(366, 138, 260, 240, "#5be0bd", 0.20, 1.5),
                (86, 404, 240, 200, "#2a8f7c", 0.16, 1.6)],
        accent="#5be0bd", ring_k=0.24, motif="diag",
    ),
    "ocean": dict(
        zh="深海 · 蓝青",
        base=[(0.0, "#071019"), (0.45, "#0b1a26"), (1.0, "#070f16")],
        lights=[(240, 54, 290, 220, "#3a8fd6", 0.22, 1.5),
                (420, 386, 220, 200, "#2ba3c8", 0.16, 1.6)],
        accent="#54b8ff", ring_k=0.26, motif="waves",
    ),
}


# --------------------------------------------------------------------------
# 装饰
# --------------------------------------------------------------------------
def motif_grooves(d, accent):
    cx, cy = L.COVER_CX, L.COVER_CY
    for i, r in enumerate(range(L.COVER_R + 8, 232, 8)):
        a = int(26 * (1.0 - i / 16.0)) + 6
        d.ellipse([sc(cx - r), sc(cy - r), sc(cx + r), sc(cy + r)],
                  outline=accent + (a,), width=int(1.2 * S))


def motif_dots(d, accent):
    for y in range(26, 456, 34):
        for x in range(26, 456, 34):
            if (x - L.COVER_CX) ** 2 + (y - L.COVER_CY) ** 2 < (L.COVER_R + 16) ** 2:
                continue
            d.ellipse([sc(x) - 1.1 * S, sc(y) - 1.1 * S, sc(x) + 1.1 * S, sc(y) + 1.1 * S],
                      fill=accent + (52,))


def motif_haze(d, accent):
    """落日：横向的暖雾带，压在下沿，不抢中间。"""
    for y0, rx, ry, a in ((438, 320, 28, 16), (462, 345, 26, 14), (488, 365, 30, 11)):
        d.ellipse([sc(240 - rx), sc(y0 - ry), sc(240 + rx), sc(y0 + ry)],
                  fill=accent + (a,))


def motif_diag(d, accent):
    for k in range(-14, 16):
        x0 = k * 34
        d.line([sc(x0), sc(-40), sc(x0 + 620), sc(620)],
               fill=accent + (16,), width=int(1.0 * S))


def motif_waves(d, accent):
    for i in range(5):
        y0 = 296 + i * 34
        pts = []
        for x in range(0, 481, 8):
            pts.append((sc(x), sc(y0 + math.sin((x / 240.0) * math.pi * 2 + i) * 11)))
        d.line(pts, fill=accent + (22,), width=int(1.2 * S))


MOTIFS = dict(grooves=motif_grooves, dots=motif_dots, haze=motif_haze,
              diag=motif_diag, waves=motif_waves)


def draw_overlay(theme):
    accent = tuple(int(v, 16) for v in (theme["accent"][1:3], theme["accent"][3:5],
                                        theme["accent"][5:7]))
    layer = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    MOTIFS[theme["motif"]](d, accent)

    # 封面「凹槽」：外圈一道细环 + 内侧一圈暗边，让 240 的封面落在一个坑里
    cx, cy = L.COVER_CX, L.COVER_CY
    r0 = L.COVER_R + 9
    d.ellipse([sc(cx - r0), sc(cy - r0), sc(cx + r0), sc(cy + r0)],
              outline=accent + (66,), width=int(1.6 * S))
    d.ellipse([sc(cx - L.COVER_R), sc(cy - L.COVER_R),
               sc(cx + L.COVER_R), sc(cy + L.COVER_R)],
              outline=(0, 0, 0, 90), width=int(3.0 * S))
    return layer


# --------------------------------------------------------------------------
# 渲染
# --------------------------------------------------------------------------
def render(name):
    t = THEMES[name]
    img = vgrad(t["base"])
    for (cx, cy, rx, ry, col, k, power) in t["lights"]:
        img = add_light(img, hx(col), glow(cx, cy, rx, ry, power), k)

    # 封面后面垫一层柔光，封面缺席时也不空
    img = add_light(img, hx(t["accent"]), glow(L.COVER_CX, L.COVER_CY, 168, 168, 1.6), 0.10)

    # 文字/进度条区压暗
    img *= (1.0 - 0.34 * smoothstep(258, 306, YY))[..., None]
    img *= (1.0 - 0.22 * smoothstep(352, 424, YY))[..., None]

    # 边缘暗角
    r = np.sqrt((XX - 240) ** 2 + (YY - 240) ** 2) / 240.0
    img *= (1.0 - 0.44 * smoothstep(0.68, 1.10, r))[..., None]

    base = Image.fromarray((np.clip(img, 0, 1) * 255).round().astype(np.uint8), "RGB").convert("RGBA")
    base.alpha_composite(draw_overlay(t))

    # 颗粒：压 RGB565 色带
    out = np.asarray(base.convert("RGB").resize((W, H), Image.LANCZOS), np.float32) / 255.0
    rng = np.random.default_rng(zlib.crc32(name.encode()) & 0x7FFFFFFF)
    out += (rng.random((H, W, 1)).astype(np.float32) - 0.5) * 0.006
    return Image.fromarray((np.clip(out, 0, 1) * 255).round().astype(np.uint8), "RGB")


def dither565(pim):
    a = np.asarray(pim, np.float32)
    rng = np.random.default_rng(11)
    org = rng.random((H, W, 3)).astype(np.float32) - 0.5
    r = np.clip(np.round((a[..., 0] + org[..., 0] * 8.0) / 8.0) * 8.0, 0, 255)
    g = np.clip(np.round((a[..., 1] + org[..., 1] * 4.0) / 4.0) * 4.0, 0, 255)
    b = np.clip(np.round((a[..., 2] + org[..., 2] * 8.0) / 8.0) * 8.0, 0, 255)
    return Image.fromarray(np.stack([r, g, b], -1).astype(np.uint8), "RGB")


def rgb565_bytes(pim):
    a = np.asarray(pim, np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    return v.astype("<u2").tobytes()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--no565", action="store_true")
    args = ap.parse_args()

    for dd in ("png", "jpg", "rgb565"):
        os.makedirs(os.path.join(ROOT, dd), exist_ok=True)

    names = [args.only] if args.only else list(THEMES)
    for n in names:
        pim = render(n)
        if not args.no565:
            pim = dither565(pim)
        pim.save(os.path.join(ROOT, "png", n + ".png"), optimize=True)
        pim.save(os.path.join(ROOT, "jpg", n + ".jpg"), quality=90, subsampling=0)
        with open(os.path.join(ROOT, "rgb565", n + ".bin"), "wb") as f:
            f.write(rgb565_bytes(pim))
        a = np.asarray(pim, np.float32)
        lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
        print("ok %-8s mean=%5.1f p95=%5.1f" % (n, lum.mean(), np.percentile(lum, 95)))


if __name__ == "__main__":
    main()
