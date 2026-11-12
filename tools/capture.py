#!/usr/bin/env python3
"""读取串口 N 秒原始输出（去掉 ANSI 颜色），用来抓 boot 日志 / ASCII 截图。"""
import re
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM10"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0
out = sys.argv[3] if len(sys.argv) > 3 else None

ser = serial.Serial(port, 115200, timeout=0.2)
end = time.time() + secs
buf = bytearray()
try:
    while time.time() < end:
        data = ser.read(8192)
        if data:
            buf += data
finally:
    ser.close()

text = buf.decode("utf-8", "replace")
text = re.sub(r"\x1b\[[0-9;]*m", "", text)
text = text.replace("\r", "")
if out:
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
print(text)
