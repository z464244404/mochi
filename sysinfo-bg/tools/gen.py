# -*- coding: utf-8 -*-
"""
mochi · 设备信息页背景 · 480x480 · ST7701S RGB565

版式见 tools/layout.py。背景里「烘焙」了两样东西，固件只画弧和字：
  · CPU 环的底槽（r=92，宽 10）—— 弧用同一个 r / thickness 叠上去就严丝合缝
  · 环顶的起始刻度（12 点方向），外加 3 个 25%/50%/75% 的位置点

用法：
  python gen.py
  python gen.py --only sysinfo
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


def dot_grid(d, color, step=40, r=1.1, alpha=12):
    for y in range(22, 470, step):
        for x in range(22, 470, step):
            if (x - L.RING_CX) ** 2 + (y - L.RING_CY) ** 2 < 118 ** 2:
                continue
            d.ellipse([sc(x - r), sc(y - r), sc(x + r), sc(y + r)],
                      fill=color + (alpha,))


def draw_ring(d, accent):
    """底槽 + 起始刻度 + 三个分位点。"""
    cx, cy, r, th = L.RING_CX, L.RING_CY, L.RING_R, L.RING_THICK
    # 底槽（比背景亮一点点，弧压上去才看得出「进度」）
    d.ellipse([sc(cx - r), sc(cy - r), sc(cx + r), sc(cy + r)],
              outline=(150, 168, 190, 26), width=int(th * S))
    # 12 点起始刻度
    a = math.radians(L.ARC_START)
    for rr, aa, thk in ((r + th * 0.9, 150, 2.2), (r - th * 0.9, 120, 2.2)):
        x0, y0 = cx + math.cos(a) * rr, cy + math.sin(a) * rr
        x1, y1 = cx + math.cos(a) * (rr + 9), cy + math.sin(a) * (rr + 9)
        d.line([sc(x0), sc(y0), sc(x1), sc(y1)], fill=accent + (aa,), width=int(thk * S))
    # 25 / 50 / 75% 分位点
    for k in (1, 2, 3):
        a = math.radians(L.ARC_START + 90 * k)
        for rr in (r - th * 0.5, r + th * 0.5):
            x, y = cx + math.cos(a) * rr, cy + math.sin(a) * rr
            d.ellipse([sc(x) - 1.4 * S, sc(y) - 1.4 * S,
                       sc(x) + 1.4 * S, sc(y) + 1.4 * S], fill=(220, 232, 240, 90))


def render(name):
    img = vgrad([(0.0, "#080b10"), (0.45, "#0c1119"), (1.0, "#090d13")])
    # 环后面一团冷光，给「仪表」一点存在感
    img = add_light(img, hx("#4fd1c5"), glow(L.RING_CX, L.RING_CY, 250, 220, 1.5), 0.15)
    img = add_light(img, hx("#5b6ea8"), glow(410, 420, 240, 200, 1.5), 0.10)
    img = add_light(img, hx("#3aa7a0"), glow(78, 380, 200, 190, 1.6), 0.08)

    # 磁贴区压暗，数值和标签才立得住
    img *= (1.0 - 0.30 * smoothstep(262, 320, YY))[..., None]
    # 边缘暗角
    r = np.sqrt((XX - 240) ** 2 + (YY - 240) ** 2) / 240.0
    img *= (1.0 - 0.44 * smoothstep(0.68, 1.10, r))[..., None]

    base = Image.fromarray((np.clip(img, 0, 1) * 255).round().astype(np.uint8), "RGB").convert("RGBA")
    layer = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    accent = tuple(int(L.ACCENT[i:i + 2], 16) for i in (1, 3, 5))

    dot_grid(d, accent)
    draw_ring(d, accent)

    # 外圈细环，收边
    d.ellipse([sc(240 - 232), sc(240 - 232), sc(240 + 232), sc(240 + 232)],
              outline=(150, 168, 190, 14), width=int(1.0 * S))

    base.alpha_composite(layer)

    out = np.asarray(base.convert("RGB").resize((W, H), Image.LANCZOS), np.float32) / 255.0
    rng = np.random.default_rng(zlib.crc32(name.encode()) & 0x7FFFFFFF)
    out += (rng.random((H, W, 1)).astype(np.float32) - 0.5) * 0.006
    return Image.fromarray((np.clip(out, 0, 1) * 255).round().astype(np.uint8), "RGB")


def dither565(pim):
    a = np.asarray(pim, np.float32)
    rng = np.random.default_rng(13)
    org = rng.random((H, W, 3)).astype(np.float32) - 0.5
    r = np.clip(np.round((a[..., 0] + org[..., 0] * 8.0) / 8.0) * 8.0, 0, 255)
    g = np.clip(np.round((a[..., 1] + org[..., 1] * 4.0) / 4.0) * 4.0, 0, 255)
    b = np.clip(np.round((a[..., 2] + org[..., 2] * 8.0) / 8.0) * 8.0, 0, 255)
    return Image.fromarray(np.stack([r, g, b], -1).astype(np.uint8), "RGB")


def rgb565_bytes(pim):
    a = np.asarray(pim, np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    return v.astype("<u2").tobytes()


NAMES = ["sysinfo"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--no565", action="store_true")
    args = ap.parse_args()

    for dd in ("png", "jpg", "rgb565"):
        os.makedirs(os.path.join(ROOT, dd), exist_ok=True)

    for n in ([args.only] if args.only else NAMES):
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
