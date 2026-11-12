#!/usr/bin/env python3
"""
Mochi 视觉预览器（设计工具，不参与固件构建）

用 Pillow 以 3x 超采样把「表情」和「时钟」画出来，输出 PNG。
目的：在没有接板子的情况下先把「可爱」调准，再把几何参数逐行翻译成 C
（firmware/main/app_face.c / app_clock.c）。

用法：
    python tools/preview.py            # 生成 preview/*.png + contact-sheet.png
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W = H = 480
SS = 3                 # 超采样倍数
CW, CH = W * SS, H * SS

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "preview")
os.makedirs(OUT, exist_ok=True)

FONT_BOLD = r"C:\Windows\Fonts\HarmonyOS_Sans_SC_Bold.ttf"
FONT_REG = r"C:\Windows\Fonts\HarmonyOS_Sans_SC_Regular.ttf"


def K(v):
    return v * SS


def rgb(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def rgba(s, a=255):
    return rgb(s) + (a,)


# --------------------------------------------------------------------------
# 背景 / 渐变
# --------------------------------------------------------------------------

def background(inner="#1b2030", outer="#08090d", cx=240, cy=225, power=1.7):
    yy, xx = np.mgrid[0:CH, 0:CW]
    dx = (xx - cx * SS) / (CW * 0.62)
    dy = (yy - cy * SS) / (CH * 0.62)
    t = np.clip(np.sqrt(dx * dx + dy * dy), 0, 1) ** power
    c0 = np.array(rgb(inner), float)
    c1 = np.array(rgb(outer), float)
    arr = c0[None, None, :] * (1 - t[..., None]) + c1[None, None, :] * t[..., None]
    return Image.fromarray(arr.astype(np.uint8), "RGB").convert("RGBA")


def linear_gradient(size, c0, c1, angle_deg=60):
    w, h = size
    yy, xx = np.mgrid[0:h, 0:w]
    a = math.radians(angle_deg)
    t = xx * math.cos(a) + yy * math.sin(a)
    t = (t - t.min()) / max(1e-6, (t.max() - t.min()))
    arr = np.array(rgb(c0))[None, None, :] * (1 - t[..., None]) + \
          np.array(rgb(c1))[None, None, :] * t[..., None]
    return Image.fromarray(arr.astype(np.uint8), "RGB").convert("RGBA")


def capsule_mask(w, h):
    m = Image.new("L", (int(w), int(h)), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=min(w, h) / 2, fill=255)
    return m


# --------------------------------------------------------------------------
# 表情
# --------------------------------------------------------------------------

BODY_W, BODY_H, BODY_R = 300.0, 288.0, 114.0
EYE_X, EYE_Y = 57.0, 222.0
EYE_W, EYE_H = 31.0, 72.0
MOUTH_Y = 296.0

MINT_TOP, MINT_BOT = "#d2f9ec", "#2aa887"
INK = "#0e1f1b"
BLUSH = "#ff93a8"


class Face:
    def __init__(self):
        self.scale = 1.0
        self.bob = 0.0
        self.blink = 0.0          # 0 = 睁开, 1 = 闭合
        self.happy = 0.0          # 0..1
        self.dizzy = 0.0          # 0..1
        self.gaze_x = 0.0
        self.gaze_y = 0.0
        self.mouth_open = 0.0
        self.wobble = 0.0
        self.sparkle_t = 0.0

    def draw(self):
        img = background()
        d = ImageDraw.Draw(img, "RGBA")

        s = self.scale
        cx = 240.0 + self.wobble
        cy = 248.0 + self.bob

        bw, bh = BODY_W * s, BODY_H * s
        br = BODY_R * s

        # --- 外发光：和 gfx_glow_round_rect 同一套 SDF + 平方衰减 ----------
        spread = 38.0
        gx0 = max(0, int(K(cx - bw / 2 - spread)) - 2)
        gy0 = max(0, int(K(cy + 2 - bh / 2 - spread)) - 2)
        gx1 = min(CW, int(K(cx + bw / 2 + spread)) + 2)
        gy1 = min(CH, int(K(cy + 2 + bh / 2 + spread)) + 2)
        yy, xx = np.mgrid[gy0:gy1, gx0:gx1]
        px = xx + 0.5 - K(cx)
        py = yy + 0.5 - K(cy + 2)
        qx = np.abs(px) - K(bw) / 2 + K(br)
        qy = np.abs(py) - K(bh) / 2 + K(br)
        dist = np.where((qx > 0) & (qy > 0), np.sqrt(qx * qx + qy * qy) - K(br),
                        np.maximum(qx, qy) - K(br))
        t = np.clip(1.0 - dist / K(spread), 0.0, 1.0)
        ga = (t * t * 42.0).astype(np.uint8)
        layer = np.zeros((gy1 - gy0, gx1 - gx0, 4), np.uint8)
        layer[..., 0] = 0x46
        layer[..., 1] = 0xD8
        layer[..., 2] = 0xB0
        layer[..., 3] = ga
        img.alpha_composite(Image.fromarray(layer), (gx0, gy0))

        # --- 体块 ------------------------------------------------------
        pad = int(K(40))
        mw, mh = int(K(bw)) + pad * 2, int(K(bh)) + pad * 2
        mask = Image.new("L", (mw, mh), 0)
        ImageDraw.Draw(mask).rounded_rectangle([pad, pad, pad + K(bw), pad + K(bh)],
                                               radius=K(br), fill=255)
        body = Image.new("RGBA", mask.size, (0, 0, 0, 0))
        grad = linear_gradient(mask.size, MINT_TOP, MINT_BOT, 90)
        body.paste(grad, (0, 0), mask)
        img.alpha_composite(body, (int(K(cx) - mw / 2), int(K(cy) - mh / 2)))

        # 顶部柔光/底部阴影都交给渐变，不再叠半透明块（会露出硬边）

        # --- 五官 ------------------------------------------------------
        ex = EYE_X * s
        ey = EYE_Y * s + self.gaze_y
        eye_w = EYE_W * s
        eye_h = EYE_H * s * (1.0 - 0.94 * self.blink)
        blink_shift = EYE_H * s * 0.30 * self.blink

        for sx in (-1, 1):
            x = cx + sx * ex + self.gaze_x
            y = cy + (ey - 248.0) * s
            if self.dizzy > 0.5:
                self._draw_x_eye(d, x, y, s)
            elif self.happy > 0.5:
                self._draw_happy_eye(d, x, y, s)
            else:
                self._draw_open_eye(d, x, y, eye_w, eye_h + blink_shift, s)

        # 腮红
        blush_a = int(120 * max(self.happy, 0.25))
        if blush_a > 8:
            for sx in (-1, 1):
                bx = cx + sx * 104 * s
                by = cy + (286 - 248) * s
                d.ellipse([K(bx - 23 * s), K(by - 15 * s), K(bx + 23 * s), K(by + 15 * s)],
                          fill=rgba(BLUSH, blush_a))

        self._draw_mouth(d, cx, cy, s)

        # 晕眩星星
        if self.dizzy > 0.2:
            for i in range(3):
                a = self.sparkle_t * 2.4 + i * 2.094
                r = (168 + 10 * math.sin(self.sparkle_t * 3 + i)) * s
                px = cx + math.cos(a) * r
                py = cy + 250 * s - 248 + math.sin(a) * r * 0.55
                self._star(d, px, py, 11 * s, rgba("#ffe98a", int(230 * self.dizzy)))

        return img.resize((W, H), Image.LANCZOS).convert("RGB")

    # -- 眼睛 ------------------------------------------------------------
    def _draw_open_eye(self, d, x, y, w, h, s):
        d.rounded_rectangle([K(x - w / 2), K(y - h / 2), K(x + w / 2), K(y + h / 2)],
                            radius=K(w / 2), fill=rgba(INK))
        # 高光
        d.ellipse([K(x - w * 0.26), K(y - h * 0.30), K(x - w * 0.02), K(y - h * 0.10)],
                  fill=(255, 255, 255, 205))

    def _draw_happy_eye(self, d, x, y, s):
        d.arc([K(x - 21), K(y - 14), K(x + 21), K(y + 24)], 195, 345,
              fill=rgba(INK), width=int(K(9)))

    def _draw_x_eye(self, d, x, y, s):
        r = 18 * s
        d.line([K(x - r), K(y - r), K(x + r), K(y + r)], fill=rgba(INK), width=int(K(9)))
        d.line([K(x - r), K(y + r), K(x + r), K(y - r)], fill=rgba(INK), width=int(K(9)))

    # -- 嘴巴 ------------------------------------------------------------
    def _draw_mouth(self, d, cx, cy, s):
        base_y = cy + (MOUTH_Y - 248) * s
        w = 40 * s
        if self.dizzy > 0.5:
            pts = []
            for i in range(17):
                t = i / 16
                px = cx - w / 2 + w * t
                py = base_y + math.sin(t * math.pi * 3) * 5 * s
                pts.append((K(px), K(py)))
            d.line(pts, fill=rgba(INK), width=int(K(8)), joint="curve")
            return
        if self.happy > 0.5:
            mw, mh = 52 * s, 38 * s
            d.ellipse([K(cx - mw / 2), K(base_y - mh * 0.35), K(cx + mw / 2), K(base_y + mh * 0.75)],
                      fill=rgba(INK))
            d.ellipse([K(cx - 14), K(base_y + 12 * s), K(cx + 14), K(base_y + 28 * s)],
                      fill=rgba("#ff7f93"))
            return
        # 二次贝塞尔微笑
        smile = 20 * s
        p0 = (cx - w / 2, base_y - 2 * s)
        p1 = (cx, base_y + smile)
        p2 = (cx + w / 2, base_y - 2 * s)
        pts = []
        for i in range(25):
            t = i / 24
            px = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t ** 2 * p2[0]
            py = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t ** 2 * p2[1]
            pts.append((K(px), K(py)))
        d.line(pts, fill=rgba(INK), width=int(K(9)), joint="curve")
        for p in (pts[0], pts[-1]):
            d.ellipse([p[0] - K(4.5), p[1] - K(4.5), p[0] + K(4.5), p[1] + K(4.5)], fill=rgba(INK))

    def _star(self, d, x, y, r, color):
        pts = []
        for i in range(8):
            a = -math.pi / 2 + i * math.pi / 4
            rr = r if i % 2 == 0 else r * 0.34
            pts.append((K(x + math.cos(a) * rr), K(y + math.sin(a) * rr)))
        d.polygon(pts, fill=color)


# --------------------------------------------------------------------------
# 时钟
# --------------------------------------------------------------------------

def render_clock(hh=12, mm=34, ss=56, date="11月12日 周三"):
    img = background("#161b2a", "#07080c", power=1.9)
    d = ImageDraw.Draw(img, "RGBA")

    cx, cy = 240.0, 232.0

    # 刻度环
    for i in range(60):
        a = math.radians(i * 6 - 90)
        major = (i % 5 == 0)
        r0 = 197 if major else 202
        r1 = 208
        if i == ss:
            col = rgba("#5be0bd", 255)
            wid = K(4.6)
        else:
            col = rgba("#ffffff", 92 if major else 34)
            wid = K(3.4 if major else 2.0)
        d.line([K(cx + math.cos(a) * r0), K(cy + math.sin(a) * r0),
                K(cx + math.cos(a) * r1), K(cy + math.sin(a) * r1)], fill=col, width=int(wid))

    # 秒进度弧
    ar = 216
    d.arc([K(cx - ar), K(cy - ar), K(cx + ar), K(cy + ar)], -90, -90 + 360 * ss / 60,
          fill=rgba("#5be0bd", 120), width=int(K(3)))

    f_big = ImageFont.truetype(FONT_BOLD, int(K(106)))
    f_date = ImageFont.truetype(FONT_REG, int(K(29)))

    tstr = f"{hh:02d}:{mm:02d}"
    d.text((K(cx), K(cy - 10)), tstr, font=f_big, fill=rgba("#f4f7f8"), anchor="mm")

    # 秒：环上的一颗游标点（比数字更干净）
    a = math.radians(ss * 6 - 90)
    dx, dy = cx + math.cos(a) * 203, cy + math.sin(a) * 203
    d.ellipse([K(dx - 6), K(dy - 6), K(dx + 6), K(dy + 6)], fill=rgba("#5be0bd"))
    d.ellipse([K(dx - 11), K(dy - 11), K(dx + 11), K(dy + 11)], fill=rgba("#5be0bd", 40))

    d.text((K(cx), K(330)), date, font=f_date, fill=rgba("#7e8a99"), anchor="mm")
    return img.resize((W, H), Image.LANCZOS).convert("RGB")


# --------------------------------------------------------------------------
# 场景
# --------------------------------------------------------------------------

def scene_idle():
    f = Face()
    return f.draw()


def scene_blink():
    f = Face()
    f.blink = 0.5
    return f.draw()


def scene_happy():
    f = Face()
    f.happy = 1.0
    return f.draw()


def scene_dizzy():
    f = Face()
    f.dizzy = 1.0
    f.sparkle_t = 0.6
    f.wobble = 4
    return f.draw()


def scene_look():
    f = Face()
    f.gaze_x = 13
    f.gaze_y = -8
    return f.draw()


def main():
    shots = {
        "face-idle": scene_idle(),
        "face-blink": scene_blink(),
        "face-happy": scene_happy(),
        "face-dizzy": scene_dizzy(),
        "face-look": scene_look(),
        "clock": render_clock(),
    }
    for name, img in shots.items():
        img.save(os.path.join(OUT, f"{name}.png"))
        print("wrote", name)

    # 拼一张 contact sheet
    cols, rows = 3, 2
    pad = 12
    sheet = Image.new("RGB", (cols * W + pad * (cols + 1), rows * H + pad * (rows + 1)), (18, 18, 20))
    for i, (name, img) in enumerate(shots.items()):
        r, c = divmod(i, cols)
        sheet.paste(img, (pad + c * (W + pad), pad + r * (H + pad)))
    sheet = sheet.resize((sheet.width // 2, sheet.height // 2), Image.LANCZOS)
    sheet.save(os.path.join(OUT, "contact-sheet.png"))
    print("wrote contact-sheet.png")


if __name__ == "__main__":
    main()
