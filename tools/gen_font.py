#!/usr/bin/env python3
"""
从 TTF 生成固件用的 8bit 灰度字体图集 -> firmware/main/fonts.c

- 每个字形存紧致包围盒 + 相对基线偏移，绘制时零裁剪
- 8bit alpha，C 端做一次 alpha 混合
- 字形按 codepoint 排序，C 端二分查找

用法：python tools/gen_font.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "firmware", "main", "fonts.c")

BOLD = r"C:\Windows\Fonts\HarmonyOS_Sans_SC_Bold.ttf"
REG = r"C:\Windows\Fonts\HarmonyOS_Sans_SC_Regular.ttf"

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
CJK = ("年月日周一二三四五六天星期时分秒上午下晚今明昨好呀别摇我啦摸头真棒气温暖冷晴云雨雪阴风"
       "￥谷峰余额度"
       "多阵雷雹雾小中大暴转局部"
       "等待网络同步错误加载"
       "℃℉"
       "深圳广州北京上海杭州成都重庆武汉西安南京天津苏州长沙厦门贵阳"
       "万亿段位资产收益流动非昵称总最近烽火哈夫币"
       "↑↓")

FONTS = [
    # (C 变量名, 字体文件, 字号 px, 字符集)
    ("font_digits_106", BOLD, 106, "0123456789:."),
    ("font_num_56", BOLD, 56, "0123456789.:"),
    ("font_ui_29", REG, 29, ASCII + CJK),
    # 三天预报用的小号温度：只要数字、℃、日期分隔符
    ("font_num_22", REG, 22, "0123456789.:-/℃℉"),
]


def build(font_path, size, chars):
    font = ImageFont.truetype(font_path, size)
    glyphs = []
    blob = bytearray()

    for ch in chars:
        try:
            bbox = font.getbbox(ch, anchor="ls")
        except Exception:
            continue
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        adv = int(round(font.getlength(ch)))
        if w <= 0 or h <= 0:
            glyphs.append((ord(ch), 0, 0, 0, 0, adv, len(blob)))
            continue
        img = Image.new("L", (w, h), 0)
        d = ImageDraw.Draw(img)
        d.text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        alpha = img.tobytes()
        # 纯空白字形（比如全角空格）不占图集
        if max(alpha) == 0:
            glyphs.append((ord(ch), 0, 0, 0, 0, adv, len(blob)))
            continue
        off = len(blob)
        blob += alpha
        glyphs.append((ord(ch), w, h, x0, y0, adv, off))

    glyphs.sort(key=lambda g: g[0])
    line_h = int(round(size * 1.2))
    return glyphs, blob, max(h for _, _, h, _, _, _, _ in glyphs), line_h


def emit(f, name, glyphs, blob, max_h, line_h):
    f.write(f"/* {name}: {len(glyphs)} glyphs, {len(blob)} bytes, max_h={max_h} */\n")
    f.write(f"static const uint8_t bm_{name}[] = {{\n")
    for i in range(0, len(blob), 24):
        f.write("    " + ",".join(str(b) for b in blob[i:i + 24]) + ",\n")
    f.write("};\n\n")

    f.write(f"static const mochi_glyph_t gl_{name}[] = {{\n")
    for cp, w, h, bx, by, adv, off in glyphs:
        f.write(f"    {{{cp}u,{w},{h},{bx},{by},{adv},{off}u}},\n")
    f.write("};\n\n")

    f.write(f"const mochi_font_t {name} = {{\n")
    f.write(f"    gl_{name}, {len(glyphs)}, {line_h}, bm_{name},\n")
    f.write("};\n\n")


def main():
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("/* 由 tools/gen_font.py 生成，不要手改 */\n#include \"gfx.h\"\n\n")
        total = 0
        for name, path, size, chars in FONTS:
            glyphs, blob, max_h, line_h = build(path, size, chars)
            total += len(blob)
            emit(f, name, glyphs, blob, max_h, line_h)
            print(f"{name}: {len(glyphs)} glyphs, {len(blob)/1024:.1f} KB bitmap")
        print(f"total: {total/1024:.1f} KB -> {OUT}")


if __name__ == "__main__":
    main()
