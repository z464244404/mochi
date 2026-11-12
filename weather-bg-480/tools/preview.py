# -*- coding: utf-8 -*-
"""
生成预览图：总览九宫格 + 带天气 UI 的实机模拟 + 圆屏外观。

用法：python preview.py
输出在 ../preview/
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipes import RECIPES  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
PNG = os.path.join(ROOT, "png")
OUT = os.path.join(ROOT, "preview")
ICONS = os.path.abspath(os.path.join(ROOT, "..", "tools", "iconwork", "png"))

W = 480
FONT_REG = r"C:\Windows\Fonts\msyh.ttc"
FONT_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"

# 天气插件里用到的颜色
# dark：原插件配色（深色背景用）
# light：亮蓝天背景用，整体压深，保证对比度
PALETTES = {
    "dark": dict(city=0xA8B4C0, temp=0xF4F7F8, unit=0x5BE0BD, cond=0x7E8A99,
                 date=0x6B7785, hi=0xFF9F43, lo=0x54B8FF),
    "light": dict(city=0x06233A, temp=0x041420, unit=0x0A5C4A, cond=0x123A56,
                  date=0x123A56, hi=0x7A3C04, lo=0x0A3C66),
}
PAL = PALETTES["dark"]


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def use_palette(name):
    global PAL
    PAL = PALETTES[name]


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT_REG, size)


def circle_mask(size=W, ss=4):
    m = Image.new("L", (size * ss, size * ss), 0)
    ImageDraw.Draw(m).ellipse([0, 0, size * ss - 1, size * ss - 1], fill=255)
    return m.resize((size, size), Image.LANCZOS)


def load_bg(name):
    return Image.open(os.path.join(PNG, name + ".png")).convert("RGB")


def load_icon(icon_name, size):
    p = os.path.join(ICONS, icon_name + ".png")
    im = Image.open(p).convert("RGBA")
    return im.resize((size, size), Image.LANCZOS)


# --------------------------------------------------------------------------
# 1) 总览九宫格
# --------------------------------------------------------------------------
def sheet(names, out, cols=5, cell=232, pad=18, label_h=30):
    rows = (len(names) + cols - 1) // cols
    Wd = cols * (cell + pad) + pad
    Hd = 86 + rows * (cell + label_h + pad) + pad
    canvas = Image.new("RGB", (Wd, Hd), (16, 18, 22))
    d = ImageDraw.Draw(canvas)
    d.text((pad + 4, 26), "mochi 天气背景 · 480×480 圆屏", font=font(26, True), fill=(236, 240, 245))
    d.text((pad + 4, 58), "同一套深色调 + 边缘氛围光，底色贴近插件原背景 0x0b0d12",
           font=font(15), fill=(126, 138, 153))

    mask = circle_mask(cell)
    for i, n in enumerate(names):
        r, c = divmod(i, cols)
        x = pad + c * (cell + pad)
        y = 86 + r * (cell + label_h + pad)
        bg = load_bg(n).resize((cell, cell), Image.LANCZOS)
        canvas.paste(bg, (x, y), mask)
        ring = Image.new("RGB", (cell, cell), (16, 18, 22))
        ImageDraw.Draw(ring).ellipse([0, 0, cell - 1, cell - 1],
                                     outline=(52, 58, 66), width=1)
        canvas.paste(ring, (x, y), Image.new("L", (cell, cell)).point(lambda v: 0))
        # 细描边
        ImageDraw.Draw(canvas).ellipse([x, y, x + cell - 1, y + cell - 1],
                                       outline=(48, 54, 62), width=1)
        rec = RECIPES[n]
        d.text((x + 2, y + cell + 6), rec["zh"], font=font(16, True), fill=(226, 232, 238))
        d.text((x + 2 + d.textlength(rec["zh"], font=font(16, True)) + 8, y + cell + 7),
               rec["codes"], font=font(13), fill=(110, 120, 132))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    canvas.save(out, optimize=True)
    print("sheet ->", out)


# --------------------------------------------------------------------------
# 2) 实机模拟：把 weather 插件的内容画在背景上
# --------------------------------------------------------------------------
def mockup(name, icon_name, temp="26", cond="晴", city="深圳", out=None,
           days=(("11/13", "partly-cloudy-day", "27", "19"),
                 ("11/14", "rain", "24", "18"),
                 ("11/15", "clear-day", "28", "20")),
           size=480, ss=2):
    """按 builtin_plugins.c 的坐标复刻天气屏。ss 为超采样倍数。"""
    S = size // 480
    Wp = size
    bg = load_bg(name).resize((Wp, Wp), Image.LANCZOS)
    im = bg.copy()
    d = ImageDraw.Draw(im)

    def txt(s, x, y, px, col, bold=False):
        f = font(int(px), bold)
        d.text((x, y), s, font=f, fill=col, anchor="mm")

    scale = Wp / 480.0
    txt(city, 240 * scale, 46 * scale, 29 * scale, rgb(PAL["city"]))

    ic = load_icon(icon_name, int(158 * scale))
    im.paste(ic, (int((120 - 79) * scale), int((150 - 79) * scale)), ic)

    fbig = font(int(106 * scale), True)
    tw = d.textlength(temp, font=fbig)
    d.text((318 * scale, 148 * scale), temp, font=fbig, fill=rgb(PAL["temp"]), anchor="mm")
    txt("℃", 318 * scale + tw * 0.5 + 20 * scale, 172 * scale, 29 * scale, rgb(PAL["unit"]))
    txt(cond, 318 * scale, 216 * scale, 29 * scale, rgb(PAL["cond"]))

    for i, (dt, dni, hi, lo) in enumerate(days):
        x = (120 + i * 120) * scale
        txt(dt, x, 250 * scale, 22 * scale, rgb(PAL["date"]))
        di = load_icon(dni, int(64 * scale))
        im.paste(di, (int(x - 32 * scale), int((292 - 32) * scale)), di)
        txt(hi + "℃", x, 336 * scale, 22 * scale, rgb(PAL["hi"]))
        txt(lo + "℃", x, 368 * scale, 22 * scale, rgb(PAL["lo"]))

    # 圆屏裁切
    im = im.resize((Wp, Wp), Image.LANCZOS)
    mask = Image.new("L", (Wp * ss, Wp * ss), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, Wp * ss - 1, Wp * ss - 1], fill=255)
    mask = mask.resize((Wp, Wp), Image.LANCZOS)
    out_img = Image.new("RGB", (Wp, Wp), (10, 11, 14))
    out_img.paste(im, (0, 0), mask)

    if out:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        out_img.save(out, optimize=True)
        print("mockup ->", out)
    return out_img


# --------------------------------------------------------------------------
# 3) 实机外观：带机身的一排
# --------------------------------------------------------------------------
def bezel(names, out, size=300, gap=40):
    n = len(names)
    Wd = gap + n * (size + gap)
    Hd = size + 2 * gap + 26
    canvas = Image.new("RGB", (Wd, Hd), (16, 18, 22))
    d = ImageDraw.Draw(canvas)
    for i, name in enumerate(names):
        im = load_bg(name).resize((size, size), Image.LANCZOS)
        mask = circle_mask(size, 4)
        x = gap + i * (size + gap)
        y = gap
        # 机身
        d.ellipse([x - 10, y - 10, x + size + 9, y + size + 9], fill=(28, 31, 36))
        d.ellipse([x - 10, y - 10, x + size + 9, y + size + 9],
                  outline=(46, 50, 57), width=2)
        canvas.paste(im, (x, y), mask)
        # 玻璃高光
        gl = Image.new("L", (size, size), 0)
        ImageDraw.Draw(gl).ellipse([0, 0, size - 1, size - 1], fill=64)
        gl = gl.filter(ImageFilter.GaussianBlur(size * 0.35))
        canvas.paste(Image.new("RGB", (size, size), (255, 255, 255)), (x, y),
                     gl.point(lambda v: int(v * 0.10)))
        rec = RECIPES[name]
        d.text((x + size // 2, y + size + 14), rec["zh"], font=font(15),
               fill=(140, 150, 162), anchor="mt")
    canvas.save(out, optimize=True)
    print("bezel ->", out)


if __name__ == "__main__":
    order = [k for k, v in RECIPES.items() if not v.get("alt")]
    order += [k for k, v in RECIPES.items() if v.get("alt")]
    sheet(order, os.path.join(OUT, "00-overview.png"), cols=4)

    # 亮底：用白天文字配色
    use_palette("light")
    mockup("clear-day", "clear-day", "26", "晴",
           out=os.path.join(OUT, "01-mock-clear-day.png"))
    mockup("partly-cloudy-day", "partly-cloudy-day", "24", "多云",
           out=os.path.join(OUT, "02-mock-partly-cloudy.png"))
    mockup("partly-cloudy-day-rain", "partly-cloudy-day-rain", "22", "阵雨",
           out=os.path.join(OUT, "03-mock-shower.png"))

    # 深底：沿用原配色
    use_palette("dark")
    mockup("rain", "rain", "21", "中雨",
           out=os.path.join(OUT, "04-mock-rain.png"))
    mockup("thunderstorms-rain", "thunderstorms-rain", "23", "雷阵雨",
           out=os.path.join(OUT, "05-mock-thunder.png"))
    mockup("clear-night", "clear-night", "18", "晴",
           out=os.path.join(OUT, "06-mock-night.png"))
    mockup("snow", "snow", "-3", "小雪",
           out=os.path.join(OUT, "07-mock-snow.png"))

    bezel(["clear-day", "rain", "clear-night", "snow"],
          os.path.join(OUT, "08-bezel.png"))
