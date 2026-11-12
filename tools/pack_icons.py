#!/usr/bin/env python3
"""
鎶?meteocons 鐨勫姩鐢婚�愬抚鎵撴垚鍥炬爣鍖咃紝瀛樺埌 TF 鍗★紙/sdcard/icons.bin锛夈�?
- 澶у浘鏍囷細Edge 娓叉煋鍑烘潵鐨?8 甯э紙96px锛夛紝淇濈暀鍘熷�� SMIL 鍔ㄧ敾鐨勮妭濂?- 灏忓浘鏍囷細64px 鍗曞抚

姣忎釜鍍忕礌 3 瀛楄妭锛歊GB565 灏忕�� + A8銆傝�惧�囩洿鎺ュ師濮嬭创鍥?+ alpha 娣峰悎銆?
    python tools/pack_icons.py
"""
import json
import os
import struct

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANIM = os.path.join(ROOT, "tools", "iconwork", "anim")
OUT = os.path.join(ROOT, "firmware", "assets", "icons.bin")

BIG = 120
SMALL = 64


def to_rgb565_alpha(im):
    im = im.convert("RGBA")
    a = np.asarray(im, dtype=np.uint16)
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    out = np.empty((im.size[1], im.size[0], 3), dtype=np.uint8)
    out[..., 0] = v & 0xFF
    out[..., 1] = (v >> 8) & 0xFF
    out[..., 2] = al
    return out.tobytes()


def main():
    meta = json.load(open(os.path.join(ANIM, "meta.json")))
    blobs = []

    for name in sorted(meta):
        dur = meta[name]["dur"]
        frames = sorted(os.listdir(os.path.join(ANIM, name)))
        frames = [f for f in frames if f.endswith(".png")]
        delay = max(40, int(dur * 1000 / len(frames)))

        data = b""
        for f in frames:
            im = Image.open(os.path.join(ANIM, name, f))
            tile = im if im.size == (BIG, BIG) else im.resize((BIG, BIG), Image.LANCZOS)
            data += to_rgb565_alpha(tile)
        blobs.append(("%s@%d" % (name, BIG), BIG, BIG, len(frames), delay, data))

        im = Image.open(os.path.join(ANIM, name, frames[0]))
        tile = im if im.size == (SMALL, SMALL) else im.resize((SMALL, SMALL), Image.LANCZOS)
        blobs.append(("%s@%d" % (name, SMALL), SMALL, SMALL, 1, 0, to_rgb565_alpha(tile)))

    header = 8 + len(blobs) * (28 + 24)
    off = header
    entries = []
    for name, w, h, nf, delay, data in blobs:
        entries.append((name, off, len(data), w, h, nf, delay))
        off += len(data)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"ICN1")
        f.write(struct.pack("<I", len(blobs)))
        for name, e_off, e_size, w, h, nf, delay in entries:
            f.write(name.encode().ljust(28, b"\x00"))
            f.write(struct.pack("<IIIIII", e_off, e_size, w, h, nf, delay))
        for item in blobs:
            f.write(item[-1])

    print("%s: %d icons, %.1f MB" % (OUT, len(meta), os.path.getsize(OUT) / 1048576.0))


if __name__ == "__main__":
    main()
