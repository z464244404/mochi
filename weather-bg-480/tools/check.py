# -*- coding: utf-8 -*-
"""
量每张背景在文字区的对比度（WCAG），两套文字配色都算：
  dark  —— 插件现在的配色（深色背景）
  light —— 亮蓝天白天用的配色

用法：python check.py
输出：控制台表格 + contrast.md（给 README 用）
"""
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipes import RECIPES  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PNG = os.path.join(ROOT, "png")

PALETTES = {
    "dark": dict(city=0xA8B4C0, temp=0xF4F7F8, unit=0x5BE0BD, cond=0x7E8A99,
                 date=0x6B7785, hi=0xFF9F43, lo=0x54B8FF),
    "light": dict(city=0x06233A, temp=0x041420, unit=0x0A5C4A, cond=0x123A56,
                  date=0x123A56, hi=0x7A3C04, lo=0x0A3C66),
}

ZONES = {
    "city": (188, 32, 292, 62, "city"),
    "temp": (248, 96, 404, 206, "temp"),
    "cond": (268, 202, 368, 232, "cond"),
    "date": (76, 236, 404, 260, "date"),
    "hi": (76, 322, 404, 350, "hi"),
    "lo": (76, 354, 404, 382, "lo"),
}


def rgb(v):
    return np.array([(v >> 16) & 255, (v >> 8) & 255, v & 255], np.float64)


def lum_arr(rgb255):
    c = np.asarray(rgb255, np.float64) / 255.0
    c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def contrast(fg, bg_lum):
    a = lum_arr(fg)
    hi = np.maximum(a, bg_lum)
    lo = np.minimum(a, bg_lum)
    return (hi + 0.05) / (lo + 0.05)


def main():
    rows = []
    for name, rec in RECIPES.items():
        p = os.path.join(PNG, name + ".png")
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("RGB")
        row = {"name": name, "zh": rec["zh"], "alt": bool(rec.get("alt"))}
        for pal_name, pal in PALETTES.items():
            crs = {}
            for z, (x0, y0, x1, y1, key) in ZONES.items():
                a = np.asarray(im.crop((x0, y0, x1, y1)), np.float64).reshape(-1, 3)
                l = lum_arr(a)
                bg = np.percentile(l, 75)  # 略偏亮：文字叠在亮处时也能读
                crs[z] = round(float(contrast(rgb(pal[key]), bg)), 2)
            row[pal_name] = crs
            row[pal_name + "_worst"] = min(crs.values())
        rows.append(row)

    hdr = "%-30s | %-6s | %-8s %-8s" % ("背景", "最佳配色", "dark最差", "light最差")
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        best = "light" if r["light_worst"] > r["dark_worst"] else "dark"
        print("%-30s | %-6s | %8.2f %8.2f" %
              (r["name"], best, r["dark_worst"], r["light_worst"]))

    # markdown
    lines = ["| 背景 | 场景 | dark 最差 | light 最差 | 建议 |", "|---|---|---|---|---|"]
    for r in rows:
        best = "light" if r["light_worst"] > r["dark_worst"] else "dark"
        tag = {"dark": "原配色", "light": "白天配色"}[best]
        lines.append("| `%s.png` | %s | %.1f | %.1f | %s |" %
                     (r["name"], r["zh"], r["dark_worst"], r["light_worst"], tag))
    path = os.path.join(ROOT, "contrast.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("<!-- 由 tools/check.py 生成 -->\n")
        f.write("\n".join(lines) + "\n")

    with open(os.path.join(ROOT, "contrast.json"), "w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False, indent=1)
    print("\n-> contrast.md / contrast.json")


if __name__ == "__main__":
    main()
