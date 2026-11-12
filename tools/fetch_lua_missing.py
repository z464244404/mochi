#!/usr/bin/env python3
"""补齐 Lua 源码里被 include 但还没下载的头文件。"""
import glob
import os
import re
import sys
import urllib.request

BASE = "https://raw.githubusercontent.com/lua/lua/v5.4.7/"
D = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "firmware", "components", "lua", "src")

have = set(os.listdir(D))
need = set()
for f in glob.glob(os.path.join(D, "*.c")) + glob.glob(os.path.join(D, "*.h")):
    with open(f, encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            m = re.match(r'\s*#include\s+"([\w.]+)"', line)
            if m and m.group(1) not in have:
                need.add(m.group(1))

print("missing:", sorted(need))
for name in sorted(need):
    try:
        data = urllib.request.urlopen(BASE + name, timeout=30).read()
        with open(os.path.join(D, name), "wb") as fh:
            fh.write(data)
        print("got", name, len(data))
    except Exception as e:  # noqa: BLE001
        print("FAIL", name, e)
        sys.exit(1)
