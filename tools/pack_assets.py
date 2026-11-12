#!/usr/bin/env python3
"""
把固件里的素材打包成一个 assets.bin，烧到 assets 分区，设备开机自动同步到 TF 卡。

    python tools/pack_assets.py --out firmware/assets/assets.bin \
        --add pet.mjp=firmware/assets/pet.mjp \
        --add work.mjp=firmware/assets/work.mjp \
        --add sleep.mjp=firmware/assets/sleep.mjp

格式（小端）：
    0   magic  'AS1\0'
    4   u32    entries
    8   u32    reserved
    12  entries * { u32 off, u32 size, char name[40] }
    ... blobs

设备只会拷贝「大小不一致」的文件，所以重复烧录不会反复写卡。
"""
import argparse
import os
import struct

MAGIC = b"AS1\x00"
NAME_LEN = 40


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="firmware/assets/assets.bin")
    ap.add_argument("--add", action="append", default=[], help="name=path")
    args = ap.parse_args()

    items = []
    for spec in args.add:
        name, path = spec.split("=", 1)
        if len(name.encode()) >= NAME_LEN:
            raise SystemExit("name too long: " + name)
        data = open(path, "rb").read()
        items.append((name, data))
        print("  %-12s %7.2f MB" % (name, len(data) / 1048576.0))

    header = 12 + len(items) * (4 + 4 + NAME_LEN)
    off = header
    entries = []
    for name, data in items:
        entries.append((off, len(data), name))
        off += len(data)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<II", len(items), 0))
        for e_off, e_size, name in entries:
            f.write(struct.pack("<II", e_off, e_size))
            f.write(name.encode().ljust(NAME_LEN, b"\x00"))
        for _, data in items:
            f.write(data)

    print("%s: %d files, %.2f MB" % (args.out, len(items), os.path.getsize(args.out) / 1048576.0))


if __name__ == "__main__":
    main()
