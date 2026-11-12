#!/usr/bin/env python3
"""
把 PNG 序列打成固件能直接播的 MJP1 容器（连续 JPEG 帧 + 帧表）。

    python tools/pack_clip.py --zip "C:\\...\\pack.zip" --out firmware/assets/pet.mjp

容器（小端）：
    0   magic  'MJP1'
    4   u16    width
    6   u16    height
    8   u16    nframes
    10  u16    fps
    12  u32    total_size
    16  u32    table_off (= 32)
    20  u32    reserved[3]
    帧表: nframes * { u32 off, u32 size }
    数据: 各帧 JPEG

PNG 是 RGBA，按包里的说明合成到 #000000 上（本来就是黑底设计），
省掉设备端 alpha，也让 JPEG 压缩率高。
"""
import argparse
import io
import os
import sys
import zipfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", required=True)
    ap.add_argument("--out", default=os.path.join(ROOT, "firmware", "assets", "pet.mjp"))
    ap.add_argument("--quality", type=int, default=82)
    ap.add_argument("--fps", type=int, default=24)
    args = ap.parse_args()

    z = zipfile.ZipFile(args.zip)
    rgb = sorted(n for n in z.namelist() if "frames_rgb" in n and n.lower().endswith(".png"))
    alpha = sorted(n for n in z.namelist() if "frames_alpha" in n and n.lower().endswith(".png"))
    frames = rgb or alpha
    if not frames:
        sys.exit("zip 里没有 frames_rgb / frames_alpha 的 PNG")
    use_rgb = bool(rgb)

    blobs = []
    w = h = 0
    for i, name in enumerate(frames):
        im = Image.open(io.BytesIO(z.read(name)))
        if i == 0:
            w, h = im.size
        if use_rgb:
            im = im.convert("RGB")
        else:
            im = im.convert("RGBA")
            bg = Image.new("RGB", im.size, (0, 0, 0))
            bg.paste(im, (0, 0), im)
            im = bg
        b = io.BytesIO()
        im.save(b, "JPEG", quality=args.quality, subsampling=2, optimize=True)
        blobs.append(b.getvalue())
        if (i + 1) % 20 == 0:
            print(f"  encoded {i + 1}/{len(frames)}")

    n = len(blobs)
    table_off = 32
    data_off = table_off + n * 8
    total = data_off + sum(len(b) for b in blobs)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
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

    print(f"{args.out}: {w}x{h}, {n} frames @ {args.fps}fps, "
          f"{total / 1048576:.2f} MB (avg {sum(len(b) for b in blobs) / n / 1024:.1f} KB/frame)")


if __name__ == "__main__":
    main()
