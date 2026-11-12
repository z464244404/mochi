#!/usr/bin/env python3
"""
天气背景：把 480x480 的 PNG 压成 JPEG 打成包，存到 TF 卡（/sdcard/bg.bin）。

    python tools/pack_bg.py

包格式（小端）：
    0   magic 'BG01'
    4   u32 count
    8   entries * { char name[28]; u32 off; u32 size; u32 w; u32 h; }
    ... JPEG blobs
"""
import io
import os
import struct

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "weather-bg-480", "png")
OUT = os.path.join(ROOT, "firmware", "assets", "bg.bin")

QUALITY = 88


def main():
    names = sorted(f[:-4] for f in os.listdir(SRC) if f.lower().endswith(".png"))
    blobs = []
    for name in names:
        im = Image.open(os.path.join(SRC, name + ".png")).convert("RGB")
        b = io.BytesIO()
        im.save(b, "JPEG", quality=QUALITY, subsampling=2, optimize=True)
        blobs.append((name, im.size[0], im.size[1], b.getvalue()))

    header = 8 + len(blobs) * (28 + 16)
    off = header
    entries = []
    for name, w, h, data in blobs:
        entries.append((name, off, len(data), w, h))
        off += len(data)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"BG01")
        f.write(struct.pack("<I", len(blobs)))
        for name, e_off, e_size, w, h in entries:
            f.write(name.encode().ljust(28, b"\x00"))
            f.write(struct.pack("<IIII", e_off, e_size, w, h))
        for _, _, _, data in blobs:
            f.write(data)

    print("%s: %d backgrounds, %.0f KB" % (OUT, len(blobs), os.path.getsize(OUT) / 1024.0))


if __name__ == "__main__":
    main()
