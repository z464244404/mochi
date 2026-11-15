#!/usr/bin/env python3
"""
生成 TF 卡用的中文字体包 -> release/fontpack.bin

固件里只带了 221 个常用字（省 flash），其余汉字靠这个包补充：
设备绘制时先查内置字体，查不到就来卡上找。

包含：ASCII + GB2312 全部汉字（6763）+ 常用标点符号，29px 灰度。

用法：python tools/pack_font.py [--size 29]
"""
import argparse
import os
import struct

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_29 = os.path.join(ROOT, "release", "fontpack.bin")
OUT_22 = os.path.join(ROOT, "release", "fontpack22.bin")
REG = r"C:\Windows\Fonts\HarmonyOS_Sans_SC_Regular.ttf"

EXTRA = "℃℉°±×÷←→↑↓、。，！？：；「」『』（）【】…—·～《》“”‘’"


def gb2312_chars():
    out = []
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            out.append(ch)
    return out


def build(size):
    chars = "".join(chr(c) for c in range(0x20, 0x7F))
    chars += "".join(gb2312_chars())
    chars += EXTRA

    seen = set()
    uniq = []
    for ch in chars:
        cp = ord(ch)
        if cp in seen:
            continue
        seen.add(cp)
        uniq.append(ch)
    uniq.sort(key=ord)

    font = ImageFont.truetype(REG, size)
    entries = []
    blob = bytearray()

    for ch in uniq:
        cp = ord(ch)
        try:
            x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        except Exception:
            continue
        w, h = x1 - x0, y1 - y0
        adv = int(round(font.getlength(ch)))
        off = len(blob)
        if w > 0 and h > 0:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
            blob += img.tobytes()
        else:
            w = h = 0
        entries.append((cp, off, w, h, x0, y0, adv))

    header_len = 20
    index_off = header_len
    data_off = index_off + len(entries) * 20

    out = OUT_22 if size == 22 else OUT_29
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(b"FTP1")
        f.write(struct.pack("<HH", size, 0))
        f.write(struct.pack("<III", len(entries), index_off, data_off))
        for cp, off, w, h, bx, by, adv in entries:
            f.write(struct.pack("<IIhhhhH", cp, off, w, h, bx, by, adv) + b"\x00\x00")
        f.write(blob)

    kb = os.path.getsize(out) / 1024.0
    print("%s: %d glyphs @ %dpx, %.0f KB" % (out, len(entries), size, kb))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=29)
    build(ap.parse_args().size)
