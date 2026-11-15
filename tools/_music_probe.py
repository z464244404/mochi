"""端到端测：给设备推一条假歌曲 + 一张封面，验证音乐副屏"""
import io
import sys
import time
import urllib.parse
import urllib.request

from PIL import Image, ImageDraw, ImageFont

BASE = "http://192.168.1.13"
SIDE = 240


def post(url, data=b"", timeout=20):
    req = urllib.request.Request(url, data=data, headers={"User-Agent": "mochi-test"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def make_cover():
    im = Image.new("RGB", (SIDE, SIDE), (24, 36, 62))
    d = ImageDraw.Draw(im)
    for i in range(SIDE):
        d.line([(0, i), (SIDE, i)], fill=(24 + i // 6, 36 + i // 8, 62 + i // 3))
    font = ImageFont.truetype(r"C:\Windows\Fonts\HarmonyOS_Sans_SC_Bold.ttf", 56)
    d.text((SIDE // 2, SIDE // 2), "起风了", font=font, fill=(250, 250, 255), anchor="mm")
    d.ellipse([70, 70, 170, 170], outline=(251, 114, 153), width=6)
    return im


def to_565(im):
    import numpy as np

    a = np.asarray(im.convert("RGB"), dtype=np.uint16)
    return (((a[:, :, 0] & 0xF8) << 8) | ((a[:, :, 1] & 0xFC) << 3) | (a[:, :, 2] >> 3)).astype("<u2").tobytes()


def main():
    meta = {
        "title": "起风了",
        "artist": "买辣椒也用券",
        "pos": "83.0",
        "dur": "325.0",
        "playing": "1",
        "ts": int(time.time()),
    }
    r = post(BASE + "/api/music?" + urllib.parse.urlencode(meta))
    print("meta ->", r)

    raw = to_565(make_cover())
    print("cover bytes:", len(raw))
    r = post(BASE + "/api/music/cover", raw, timeout=40)
    print("cover ->", r)


if __name__ == "__main__":
    main()
