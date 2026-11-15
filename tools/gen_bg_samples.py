#!/usr/bin/env python3
"""
生成天气背景样例，放进插件仓库给用户预览：

    plugins/weather/backgrounds/<name>.jpg     16 张 480x480（就是 bg.bin 里压的那份）
    plugins/weather/backgrounds/contact-sheet.jpg  4x4 总览图（带中文标签）

用法：python tools/gen_bg_samples.py
"""
import io
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "weather-bg-480", "png")
DST = os.path.join(ROOT, "plugins", "weather", "backgrounds")
FONT = r"C:\Windows\Fonts\HarmonyOS_Sans_SC_Regular.ttf"

LABELS = {
    "clear-day": "晴（白天）",
    "clear-day-dark": "晴（白天·深色）",
    "clear-night": "晴（夜间）",
    "partly-cloudy-day": "多云（白天）",
    "partly-cloudy-day-dark": "多云（白天·深色）",
    "partly-cloudy-night": "多云（夜间）",
    "cloudy": "阴",
    "fog": "雾",
    "drizzle": "毛毛雨",
    "rain": "小 / 中雨",
    "overcast-rain": "大雨 / 暴雨",
    "partly-cloudy-day-rain": "阵雨（白天）",
    "partly-cloudy-day-rain-dark": "阵雨（白天·深色）",
    "sleet": "冻雨",
    "snow": "雪",
    "thunderstorms-rain": "雷阵雨",
}

QUALITY = 88
THUMB = 240
COLS, ROWS = 4, 4
PAD = 10


def main():
    names = [n for n in LABELS if os.path.exists(os.path.join(SRC, n + ".png"))]
    os.makedirs(DST, exist_ok=True)

    total = 0
    for name in names:
        im = Image.open(os.path.join(SRC, name + ".png")).convert("RGB")
        b = io.BytesIO()
        im.save(b, "JPEG", quality=QUALITY, subsampling=2, optimize=True)
        with open(os.path.join(DST, name + ".jpg"), "wb") as f:
            f.write(b.getvalue())
        total += len(b.getvalue())

    label_h = 34
    cell = THUMB + label_h
    sheet = Image.new("RGB", (COLS * (cell + PAD) + PAD, ROWS * (cell + PAD) + PAD), (18, 20, 24))
    d = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(FONT, 18) if os.path.exists(FONT) else ImageFont.load_default()

    for i, name in enumerate(names):
        x = PAD + (i % COLS) * (cell + PAD)
        y = PAD + (i // COLS) * (cell + PAD)
        im = Image.open(os.path.join(SRC, name + ".png")).convert("RGB").resize((THUMB, THUMB), Image.LANCZOS)
        sheet.paste(im, (x, y))
        d.text((x + 4, y + THUMB + 6), LABELS[name], font=font, fill=(228, 236, 244))

    sheet_path = os.path.join(DST, "contact-sheet.jpg")
    sheet.save(sheet_path, "JPEG", quality=86, optimize=True)

    print("%d backgrounds, %.0f KB total + contact sheet %.0f KB"
          % (len(names), total / 1024, os.path.getsize(sheet_path) / 1024))


if __name__ == "__main__":
    main()
