# -*- coding: utf-8 -*-
"""
把 bilibili.com 的那个 18x18 SVG 图标光栅化成位图。

原始标签（trigger-icon）：
  <svg width="18" height="18" viewBox="0 0 18 18" fill="none">
    <path fill-rule="evenodd" d="M3.73252 2.67094C..." fill="currentColor"/>
  </svg>

环境里没有 cairosvg 之类的库，所以这里自己解析 path：
只用到 M / C / L / H / V / Z 六个绝对指令，三次贝塞尔按 N 段折线近似。
fill-rule=evenodd 用「各子路径分别填充后逐像素异或」实现，
正好把机身挖空、再让眼睛填回来。
"""
import re

import numpy as np
from PIL import Image, ImageDraw

# 官方 path，原样照搬
PATH = (
    "M3.73252 2.67094C3.33229 2.28484 3.33229 1.64373 3.73252 1.25764C4.11291 0.890684 "
    "4.71552 0.890684 5.09591 1.25764L7.21723 3.30403C7.27749 3.36218 7.32869 3.4261 "
    "7.37081 3.49407H10.5789C10.6211 3.4261 10.6723 3.36218 10.7325 3.30403L12.8538 "
    "1.25764C13.2342 0.890684 13.8368 0.890684 14.2172 1.25764C14.6175 1.64373 "
    "14.6175 2.28484 14.2172 2.67094L13.364 3.49407H14C16.2091 3.49407 18 5.28493 "
    "18 7.49407V12.9996C18 15.2087 16.2091 16.9996 14 16.9996H4C1.79086 16.9996 0 "
    "15.2087 0 12.9996V7.49406C0 5.28492 1.79086 3.49407 4 3.49407H4.58579L3.73252 "
    "2.67094ZM4 5.42343C2.89543 5.42343 2 6.31886 2 7.42343V13.0702C2 14.1748 "
    "2.89543 15.0702 4 15.0702H14C15.1046 15.0702 16 14.1748 16 13.0702V7.42343C16 "
    "6.31886 15.1046 5.42343 14 5.42343H4ZM5 9.31747C5 8.76519 5.44772 8.31747 6 "
    "8.31747C6.55228 8.31747 7 8.76519 7 9.31747V10.2115C7 10.7638 6.55228 11.2115 6 "
    "11.2115C5.44772 11.2115 5 10.7638 5 10.2115V9.31747ZM12 8.31747C11.4477 8.31747 "
    "11 8.76519 11 9.31747V10.2115C11 10.7638 11.4477 11.2115 12 11.2115C12.5523 "
    "11.2115 13 10.7638 13 10.2115V9.31747C13 8.76519 12.5523 8.31747 12 8.31747Z"
)

VB = 18.0          # viewBox 边长
BEZIER_STEPS = 24


def _bezier(p0, p1, p2, p3, n=BEZIER_STEPS):
    pts = []
    for i in range(1, n + 1):
        t = i / n
        u = 1.0 - t
        x = u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0]
        y = u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]
        pts.append((x, y))
    return pts


def parse(path=PATH):
    """-> 子路径列表，每条是一串 (x, y) 折线点（已按贝塞尔展开）。"""
    toks = re.findall(r"[MCLHVZ]|-?\d*\.?\d+", path)
    i = 0
    subs, cur_sub = [], []
    cur, start = (0.0, 0.0), (0.0, 0.0)

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    while i < len(toks):
        cmd = toks[i]
        i += 1
        if cmd == "M":
            if len(cur_sub) > 2:
                subs.append(cur_sub)
            cur = (num(), num())
            start = cur
            cur_sub = [cur]
        elif cmd == "C":
            pts = _bezier(cur, (num(), num()), (num(), num()), (num(), num()))
            cur_sub += pts
            cur = pts[-1]
        elif cmd == "L":
            cur = (num(), num())
            cur_sub.append(cur)
        elif cmd == "H":
            cur = (num(), cur[1])
            cur_sub.append(cur)
        elif cmd == "V":
            cur = (cur[0], num())
            cur_sub.append(cur)
        elif cmd == "Z":
            cur_sub.append(start)
            subs.append(cur_sub)
            cur_sub = []
            cur = start

    if len(cur_sub) > 2:
        subs.append(cur_sub)
    return subs


def mask(size, ss=8, path=PATH):
    """返回 size×size 的 8bit 遮罩（255 = 实心）。ss 是超采样倍数。"""
    px = int(size * ss)
    k = px / VB
    acc = np.zeros((px, px), bool)
    for sub in parse(path):
        m = Image.new("L", (px, px), 0)
        ImageDraw.Draw(m).polygon([(x * k, y * k) for (x, y) in sub], fill=255)
        acc ^= (np.asarray(m) > 0)
    out = Image.fromarray((acc * 255).astype(np.uint8), "L")
    return out.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    import os
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "logo.png"
    m = mask(288, ss=8)
    img = Image.new("RGBA", (288, 288), (0, 0, 0, 0))
    img.putalpha(m)
    img.paste((251, 114, 153, 255), (0, 0), m)
    img.save(out)
    print("wrote", os.path.abspath(out))
