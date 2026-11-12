#!/usr/bin/env python3
"""
用「和 gfx.c 完全相同的排版数学」把文字画出来，检查基线/居中是否对。

gfx_text 的规则：
    pen   = x - width/2           (center)
    gx    = round(pen) + glyph.bx
    gy    = baseline + glyph.by
    baseline_at(font, '0', cy) = cy - (glyph.by + glyph.h/2)

生成的 PNG 如果和 preview 里位置一致，说明字体管线没问题。
"""
import os

import numpy as np
from PIL import Image

import gen_font

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "preview", "text-check.png")


def find(glyphs, cp):
    for g in glyphs:
        if g[0] == cp:
            return g
    return None


def baseline_at(glyphs, cp, cy):
    cp0, w, h, bx, by, adv, off = find(glyphs, cp)
    return cy - (by + h / 2.0)


def draw_text(canvas, glyphs, blob, text, x, cy, rgb, ref=ord("0")):
    w = 0
    for ch in text:
        g = find(glyphs, ord(ch))
        w += g[5] if g else 15
    pen = x - w / 2.0
    base = baseline_at(glyphs, ref, cy)
    col = np.array(rgb, dtype=np.float32)

    for ch in text:
        g = find(glyphs, ord(ch))
        if not g:
            continue
        cp, gw, gh, bx, by, adv, off = g
        if gw and gh:
            alpha = np.frombuffer(bytes(blob[off:off + gw * gh]), dtype=np.uint8).reshape(gh, gw)
            gx, gy = int(round(pen)) + bx, int(round(base)) + by
            sub = canvas[gy:gy + gh, gx:gx + gw].astype(np.float32)
            a = (alpha.astype(np.float32) / 255.0)[..., None]
            canvas[gy:gy + gh, gx:gx + gw] = (sub * (1 - a) + col * a).astype(np.uint8)
        pen += adv


def main():
    gd, bd, _, _ = gen_font.build(gen_font.BOLD, 106, "0123456789:")
    gu, bu, _, _ = gen_font.build(gen_font.REG, 29, gen_font.ASCII + gen_font.CJK)

    canvas = np.zeros((480, 480, 3), np.uint8)
    canvas[:, :] = (10, 12, 18)

    draw_text(canvas, gd, bd, "12:34", 240, 222, (244, 247, 248))
    draw_text(canvas, gu, bu, "11月12日 周三", 240, 332, (126, 138, 153), ref=0x65E5)

    Image.fromarray(canvas).save(OUT)

    lum = canvas.sum(axis=2)
    ys, xs = np.where(lum > 120)
    print(f"all ink  x=[{xs.min()},{xs.max()}] y=[{ys.min()},{ys.max()}]")

    m = ys < 300
    print(f"digits   x=[{xs[m].min()},{xs[m].max()}] center_x={(xs[m].min()+xs[m].max())/2:.1f} "
          f"y=[{ys[m].min()},{ys[m].max()}] center_y={(ys[m].min()+ys[m].max())/2:.1f}")
    m = ys >= 300
    print(f"date     x=[{xs[m].min()},{xs[m].max()}] center_x={(xs[m].min()+xs[m].max())/2:.1f} "
          f"y=[{ys[m].min()},{ys[m].max()}] center_y={(ys[m].min()+ys[m].max())/2:.1f}")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
