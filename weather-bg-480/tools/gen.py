# -*- coding: utf-8 -*-
"""
mochi 天气背景生成器 · 480x480 · ST7701S RGB565
把 recipes.py 里的配方渲染成 PNG / JPEG / RGB565 裸数据。

设计约定（和现有 weather 插件对齐）：
  * 底子深色（0x0b0d12 一脉）—— 插件用 0xf4f7f8 / 0x5be0bd / 0x7e8a99 写字，
    亮底会把字压没。
  * 天气情绪放在边缘：上沿云层、四周光晕、下沿降水纹理；
    城市名(y46)、大图标(120,150)、温度(318,148)、底部三列预报(y250-368)
    这些区域低亮度低对比。
  * 配色取自 meteocons 图标（琥珀 #f7b32b、宝蓝 #2e6be6、云白 #dfebf7），
    背景和图标天然一套。
  * 输出按 RGB565 量化 + 抖动，避免 5-6-5 屏上的色带。

用法：
  python gen.py                    # 全部生成到 ../（png/ jpg/ rgb565/）
  python gen.py --only rain        # 只做一张
  python gen.py --no565            # 不量化（拿去做二次编辑）
"""
import argparse
import json
import math
import os
import sys
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipes import RECIPES  # noqa: E402

W = H = 480
YY, XX = np.mgrid[0:H, 0:W].astype(np.float32)
R_SAFE = 240.0

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))


# --------------------------------------------------------------------------
# 基础工具
# --------------------------------------------------------------------------
def hx(s):
    s = s.lstrip("#")
    return np.array([int(s[i:i + 2], 16) for i in (0, 2, 4)], np.float32) / 255.0


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def blur(a, r):
    if r <= 0:
        return a
    im = Image.fromarray((np.clip(a, 0, 1) * 255).round().astype(np.uint8), "L")
    return np.asarray(im.filter(ImageFilter.GaussianBlur(r)), np.float32) / 255.0


def fbm(seed, octaves=5, base=3, aspect=1.0):
    rng = np.random.default_rng(seed)
    out = np.zeros((H, W), np.float32)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        gw = max(2, int(base * (2 ** o) * aspect))
        gh = max(2, int(base * (2 ** o)))
        g = rng.random((gh, gw)).astype(np.float32)
        layer = Image.fromarray((g * 255).astype(np.uint8), "L").resize((W, H), Image.BICUBIC)
        out += amp * (np.asarray(layer, np.float32) / 255.0)
        total += amp
        amp *= 0.5
    return out / total


def vgrad(stops):
    """[(pos, '#rrggbb')] -> (H,W,3)"""
    stops = sorted(stops, key=lambda s: s[0])
    pos = np.array([s[0] for s in stops], np.float32)
    cols = np.array([hx(s[1]) for s in stops], np.float32)
    t = np.arange(H, dtype=np.float32) / (H - 1)
    chans = [np.interp(t, pos, cols[:, c]).astype(np.float32) for c in range(3)]
    return np.repeat(np.stack(chans, axis=1)[:, None, :], W, axis=1).copy()


def glow(cx, cy, rx, ry, power=2.0):
    """高斯衰减的环境光。用 (1-d)^p 的话边界会形成圆盘，屏幕上像散景光斑。"""
    d2 = ((XX - cx) / rx) ** 2 + ((YY - cy) / ry) ** 2
    return np.exp(-d2 * power)


def add_light(img, color, mask, k):
    m = np.clip(mask * k, 0.0, 1.0)[..., None]
    return 1.0 - (1.0 - img) * (1.0 - color * m)


# --------------------------------------------------------------------------
# 天气元素
# --------------------------------------------------------------------------
def metaballs(seed, cx, cy, sx, sy, n, rmin, rmax, flat=1.9):
    rng = np.random.default_rng(seed)
    f = np.zeros((H, W), np.float32)
    for _ in range(n):
        px = cx + rng.normal(0, sx)
        py = cy + rng.normal(0, sy) * 0.55
        r = rng.uniform(rmin, rmax)
        d2 = ((XX - px) / flat) ** 2 + (YY - py) ** 2
        f += np.exp(-d2 / (2.0 * r * r))
    m = f.max()
    return f / m if m > 0 else f


def cloud_mask(seed, cx, cy, sx, sy, n, rmin, rmax, soft, blur_r, texture=0.30,
               lumps=3.0, shrink=0.48, flat=1.9):
    """云团轮廓：多而小的球叠出起伏，再叠一层 fbm 打碎边缘。

    按 99.5 分位归一化（而不是最大值），否则只有球堆的峰值能过阈值，
    云会被切成一坨坨孤立光斑。
    """
    f = metaballs(seed, cx, cy, sx, sy, int(n * lumps), rmin * shrink, rmax * shrink,
                  flat=flat)
    lo = float(np.percentile(f, 1.0))
    hi = float(np.percentile(f, 99.5))
    f = (f - lo) / max(1e-6, hi - lo)
    f = f + texture * (fbm(seed + 7, base=4, aspect=2.0) - 0.5)
    m = smoothstep(0.26, 0.52, f)
    return blur(m, blur_r * 0.5) * soft


def add_cloud(img, c):
    """云：上缘受光、下缘压暗，再补一层错位的投影，才有体积。

    dark=True 时反过来压暗（亮蓝天上的乌云），加光做不到这件事。
    """
    m = cloud_mask(c["seed"], c["cx"], c["cy"], c["sx"], c["sy"], c["n"],
                   c["rmin"], c["rmax"], c["soft"], c["blur"],
                   texture=c.get("texture", 0.30), flat=c.get("flat", 1.9))
    top = 1.0 - 0.55 * smoothstep(c["cy"] - 6, c["cy"] + c["sy"] * 1.8, YY)
    if c.get("dark"):
        return img * (1.0 - np.clip(m * top, 0.0, 1.0) * c["gain"])[..., None]
    dy = max(6, int(c["sy"] * 0.45))
    sh = np.zeros_like(m)
    sh[dy:] = m[:-dy]
    img = img * (1.0 - 0.22 * blur(sh, c["blur"] * 0.8))[..., None]
    return add_light(img, hx(c["color"]), m * top, c["gain"])


def rain(seed, n, angle, length, width, alpha, y0=-60, y1=H + 60, blur_r=0.6):
    S = 2
    canvas = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(canvas)
    rng = np.random.default_rng(seed)
    rad = math.radians(angle)
    for _ in range(n):
        x = rng.uniform(-80, W + 80)
        y = rng.uniform(y0, y1)
        L = length * rng.uniform(0.55, 1.35)
        dx, dy = math.sin(rad) * L, math.cos(rad) * L
        a = int(255 * alpha * rng.uniform(0.25, 1.0))
        if a <= 2:
            continue
        d.line([(x * S, y * S), ((x + dx) * S, (y + dy) * S)], fill=min(255, a),
               width=max(1, int(round(width * S))))
    m = np.asarray(canvas.resize((W, H), Image.LANCZOS), np.float32) / 255.0
    return blur(m, blur_r)


def snow(seed, n, rmin, rmax, alpha):
    S = 2
    canvas = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(canvas)
    rng = np.random.default_rng(seed)
    for _ in range(n):
        x, y = rng.uniform(-20, W + 20), rng.uniform(-20, H + 20)
        r = rng.uniform(rmin, rmax)
        a = int(255 * alpha * rng.uniform(0.35, 1.0))
        if r < 2.2:
            d.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=a)
        else:
            g = Image.new("L", (64, 64), 0)
            ImageDraw.Draw(g).ellipse([4, 4, 60, 60], fill=a)
            g = g.filter(ImageFilter.GaussianBlur(6))
            canvas.paste(g, (int((x - r) * S), int((y - r) * S)), g)
    m = np.asarray(canvas.resize((W, H), Image.LANCZOS), np.float32) / 255.0
    return blur(m, 0.5)


def stars(seed, n, bright=0.55):
    S = 2
    canvas = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(canvas)
    rng = np.random.default_rng(seed)
    for _ in range(n):
        x, y = rng.uniform(0, W), rng.uniform(0, H * 0.85)
        r = abs(rng.normal(0, 0.45)) + 0.5
        a = int(255 * min(1.0, bright * rng.uniform(0.25, 1.0)))
        d.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=a)
    base = np.asarray(canvas.resize((W, H), Image.LANCZOS), np.float32) / 255.0
    return np.clip(base + blur(base, 2.2) * 0.55, 0, 1)


def lightning(seed, x_top, y_top, y_bottom, core_w=3.0):
    S = 2
    rng = np.random.default_rng(seed)
    pts = []
    n = 7
    for i in range(n + 1):
        t = i / n
        x = x_top + rng.normal(0, 16) * math.sin(t * math.pi) + rng.normal(0, 7)
        pts.append((x, y_top + (y_bottom - y_top) * t))
    canvas = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(canvas)
    d.line([(x * S, y * S) for x, y in pts], fill=255, width=int(core_w * S), joint="curve")
    bx, by = pts[4]
    d.line([(bx * S, by * S), ((bx + rng.uniform(28, 52)) * S, (by + rng.uniform(30, 60)) * S)],
           fill=170, width=int(core_w * S * 0.7))
    core = np.asarray(canvas.resize((W, H), Image.LANCZOS), np.float32) / 255.0
    halo = np.clip(blur(core, 3.0) * 0.55 + blur(core, 13.0) * 0.5, 0, 1)
    return core, halo


def fog_layers(seed, layers, alpha=0.5):
    """横向雾丝：一条条被拉长的细线，比噪声更清楚是「雾」。"""
    S = 2
    canvas = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(canvas)
    rng = np.random.default_rng(seed)
    for i in range(46):
        y = rng.uniform(H * 0.10, H * 1.02)
        # 越靠下越密
        if y < H * 0.45 and rng.random() < 0.45:
            continue
        thick = rng.uniform(1.5, 7.0)
        x0 = rng.uniform(-120, W * 0.7)
        x1 = x0 + rng.uniform(W * 0.25, W * 0.95)
        a = int(255 * rng.uniform(0.25, 0.85))
        d.line([(x0 * S, y * S), (x1 * S, y * S)], fill=a,
               width=max(1, int(thick * S)))
    m = np.asarray(canvas.resize((W, H), Image.LANCZOS), np.float32) / 255.0
    m = blur(m, 3.5)
    # 叠加纵向浓度：给每条雾带一个 y 权重
    prof = np.zeros((H, W), np.float32)
    for (cy, th, a) in layers:
        prof += np.exp(-((YY - cy) ** 2) / (2.0 * th * th)) * a
    prof /= max(1e-6, prof.max())
    m = m * (0.45 + 0.55 * prof)
    m *= 0.55 + 0.45 * smoothstep(H * 0.20, H, YY)
    return blur(m, 2.0) * alpha


# --------------------------------------------------------------------------
# 渲染
# --------------------------------------------------------------------------
def render(name):
    s = RECIPES[name]
    img = vgrad(s["base"])

    for (cx, cy, rx, ry, col, k, power) in s["lights"]:
        img = add_light(img, hx(col), glow(cx, cy, rx, ry, power), k)

    for c in s["clouds"]:
        img = add_cloud(img, c)

    for item in s["motifs"]:
        kind = item[0]
        p = item[1] if len(item) > 1 and isinstance(item[1], dict) else {}
        if kind == "rain":
            m = rain(1000 + p["n"], p["n"], p["angle"], p["length"], p["width"],
                     p["alpha"], y0=p["y0"], blur_r=p["blur"])
            img = add_light(img, hx("#bcd8ef"), m, 1.0)
        elif kind == "snow":
            m = snow(2000 + p["n"], p["n"], p["rmin"], p["rmax"], p["alpha"])
            img = add_light(img, hx("#e8f4ff"), m, 1.0)
        elif kind == "stars":
            img = add_light(img, hx("#dfe6ff"), stars(3001, p["n"]), 0.85)
        elif kind == "moon":
            cx, cy, r = p["cx"], p["cy"], p["r"]
            d = np.sqrt((XX - cx) ** 2 + (YY - cy) ** 2)
            disc = np.clip((r - d) / 1.6 + 0.5, 0, 1)
            # 边缘压暗一点，别像一个贴上去的纯白圆
            disc *= 1.0 - 0.30 * smoothstep(0.55, 1.0, d / r)
            img = add_light(img, hx("#f2f5ff"), disc, 0.60)
            img = add_light(img, hx("#8fa6e0"), glow(cx, cy, r * 6.0, r * 6.0, 2.6), 0.10)
        elif kind == "sun":
            cx, cy, r = p["cx"], p["cy"], p["r"]
            d = np.sqrt((XX - cx) ** 2 + (YY - cy) ** 2)
            # 不画硬边圆盘 —— 小圆盘在平滑天空上永远像贴纸/月亮。
            # 用「高斯核心 + 更大光晕」：亮到发光，边界自然消失。
            core = np.exp(-((d / (r * 0.78)) ** 2) * 2.2)
            bloom = np.exp(-((d / (r * 3.2)) ** 2) * 2.0)
            img = add_light(img, hx("#fffdf6"), core, 0.95)
            img = add_light(img, hx("#ffe9c0"), bloom, 0.34)
        elif kind == "rays":
            cx, cy = item[1], item[2]
            ang = np.arctan2(YY - cy, XX - cx)
            rr = np.sqrt((XX - cx) ** 2 + (YY - cy) ** 2)
            spokes = 0.5 + 0.5 * np.cos(ang * 7.0)
            m = (spokes ** 6) * np.clip(1.0 - rr / p.get("reach", 300.0), 0, 1) \
                * smoothstep(16, 56, rr)
            img = add_light(img, hx("#ffd98a"), m, p.get("k", 0.06))
        elif kind == "fog":
            img = add_light(img, hx("#c2cfd8"), fog_layers(4001, p["layers"]), p.get("k", 0.18))
        elif kind == "bolt":
            core, halo = lightning(p["seed"], p["x_top"], p["y_top"], p["y_bottom"])
            img = add_light(img, hx("#8f7bff"), halo, 0.60)
            img = add_light(img, hx("#f2ecff"), core, 1.0)

    # 柔性暗角：圆屏边缘有外壳，过渡看不出来
    r = np.sqrt((XX - W / 2) ** 2 + (YY - H / 2) ** 2) / R_SAFE
    img *= (1.0 - s.get("vignette", 0.42) * smoothstep(0.70, 1.12, r))[..., None]

    # 文字区压暗：亮底图必须做，否则城市名/预报读不出来
    sc = s.get("scrim") or {}
    img *= (1.0 - sc.get("top", 0.0) * smoothstep(96, 0, YY))[..., None]
    img *= (1.0 - sc.get("bottom", 0.0) * smoothstep(262, H + 24, YY))[..., None]
    img *= (1.0 - 0.22 * smoothstep(70, 6, YY))[..., None]
    # 城市名正后方再柔一点：上沿那圈云最亮的地方正好压着字
    # （亮底图配深色字，这里就不能再压了）
    img *= (1.0 - s.get("city_scrim", 0.20) * glow(240, 40, 260, 78, 1.5))[..., None]
    # 三列预报后面（雾这类浅色图会盖住日期的灰字）
    img *= (1.0 - s.get("days_scrim", 0.0) * smoothstep(205, 262, YY))[..., None]

    rng = np.random.default_rng(zlib.crc32(name.encode()) & 0x7FFFFFFF)
    img += (rng.random((H, W, 1)).astype(np.float32) - 0.5) * s.get("grain", 0.006)
    return np.clip(img, 0, 1)


# --------------------------------------------------------------------------
# 输出
# --------------------------------------------------------------------------
def dither565(img):
    """加 1 LSB 抖动后按 5-6-5 量化，再还原 8bit —— 屏上就是这个样子。"""
    a = np.clip(img, 0, 1) * 255.0
    rng = np.random.default_rng(7)
    org = rng.random((H, W, 3)).astype(np.float32) - 0.5
    r = np.clip(np.round((a[..., 0] + org[..., 0] * 8.0) / 8.0) * 8.0, 0, 255)
    g = np.clip(np.round((a[..., 1] + org[..., 1] * 4.0) / 4.0) * 4.0, 0, 255)
    b = np.clip(np.round((a[..., 2] + org[..., 2] * 8.0) / 8.0) * 8.0, 0, 255)
    return np.stack([r, g, b], axis=-1).astype(np.uint8)


def rgb565_bytes(img888):
    a = img888.astype(np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    return v.astype("<u2").tobytes()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--no565", action="store_true")
    ap.add_argument("--no-bin", action="store_true")
    ap.add_argument("--no-jpg", action="store_true")
    args = ap.parse_args()

    names = [args.only] if args.only else [k for k, v in RECIPES.items() if v.get("enabled", True)]
    for d in ("png", "jpg", "rgb565"):
        os.makedirs(os.path.join(ROOT, d), exist_ok=True)

    index = {}
    for n in names:
        img = render(n)
        px = dither565(img) if not args.no565 else np.clip(img * 255.0, 0, 255).round().astype(np.uint8)
        pim = Image.fromarray(px, "RGB")
        pim.save(os.path.join(ROOT, "png", n + ".png"), optimize=True)
        if not args.no_jpg:
            pim.save(os.path.join(ROOT, "jpg", n + ".jpg"), quality=88, optimize=True,
                     subsampling=0)
        if not args.no_bin:
            with open(os.path.join(ROOT, "rgb565", n + ".bin"), "wb") as f:
                f.write(rgb565_bytes(px))
        lum = 0.2126 * px[..., 0] + 0.7152 * px[..., 1] + 0.0722 * px[..., 2]
        index[n] = dict(zh=RECIPES[n]["zh"], codes=RECIPES[n]["codes"],
                        palette=RECIPES[n].get("palette", "dark"),
                        alt=bool(RECIPES[n].get("alt")),
                        mean=round(float(lum.mean()), 1),
                        p95=round(float(np.percentile(lum, 95)), 1))
        print("ok %-24s mean=%5.1f p50=%5.1f p95=%5.1f max=%5.1f"
              % (n, lum.mean(), np.percentile(lum, 50), np.percentile(lum, 95), lum.max()))

    with open(os.path.join(ROOT, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
