#!/usr/bin/env python3
"""
把一整个目录的 PNG 序列打成 MJP1（和 pack_clip.py 同样的容器，只是不用 zip）。

    python tools/pack_dir.py --dir "C:\\...\\frames_alpha" --out firmware/assets/work.mjp --fps 24
"""
import argparse
import io
import os

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--quality", type=int, default=82)
    ap.add_argument("--fps", type=int, default=24)
    args = ap.parse_args()

    files = sorted(
        n for n in os.listdir(args.dir)
        if n.lower().endswith((".png", ".webp", ".jpg", ".jpeg"))
    )
    if not files:
        raise SystemExit("no frames in " + args.dir)

    blobs, w, h = [], 0, 0
    for i, name in enumerate(files):
        im = Image.open(os.path.join(args.dir, name))
        if i == 0:
            w, h = im.size
        if im.mode == "RGBA":
            bg = Image.new("RGB", im.size, (0, 0, 0))
            bg.paste(im, (0, 0), im)
            im = bg
        else:
            im = im.convert("RGB")
        b = io.BytesIO()
        im.save(b, "JPEG", quality=args.quality, subsampling=2, optimize=True)
        blobs.append(b.getvalue())
        if (i + 1) % 20 == 0:
            print("  encoded %d/%d" % (i + 1, len(files)))

    n = len(blobs)
    table_off = 32
    data_off = table_off + n * 8
    total = data_off + sum(len(b) for b in blobs)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(b"MJP1")
        f.write(np.array([w, h, n, args.fps], dtype="<u2").tobytes())
        f.write(np.array([total, table_off], dtype="<u4").tobytes())
        f.write(b"\x00" * 12)
        off = data_off
        for b in blobs:
            f.write(np.array([off, len(b)], dtype="<u4").tobytes())
            off += len(b)
        for b in blobs:
            f.write(b)

    print("%s: %dx%d, %d frames @ %dfps, %.2f MB (avg %.1f KB/frame)" % (
        args.out, w, h, n, args.fps, total / 1048576.0, sum(len(b) for b in blobs) / n / 1024.0))


if __name__ == "__main__":
    main()
