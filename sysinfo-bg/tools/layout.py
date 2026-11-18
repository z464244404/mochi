# -*- coding: utf-8 -*-
"""
设备信息页的版式常量 —— gen.py 和 preview.py 共用。

    SYSTEM      22px，基线 46
    CPU 环      圆心 (240,150)，中线半径 92，线宽 10（角度从 -90 起顺时针）
    CPU 数字    font_num_56（字高 43），基线 172，环内居中
    2×2 磁贴    列心 136 / 344
                A 行：标签基线 300、数值基线 334
                B 行：标签基线 380、数值基线 414

五个数据：CPU 使用率（环）、内存、网络、系统音量、开机时长。
"""
CAPTION_BASELINE = 46
CAPTION_SIZE = 22

RING_CX, RING_CY = 240, 150
RING_R = 92             # gfx_arc 的 r 是「中线半径」，环占 87..97
RING_THICK = 10
ARC_START = -90         # 12 点方向

PCT_BASELINE = 172
PCT_FONT_H = 43         # font_num_56 数字高

TILE_X = (136, 344)
ROW_A_LABEL = 300
ROW_A_VALUE = 334
ROW_B_LABEL = 380
ROW_B_VALUE = 414
TILE_LABEL_SIZE = 22
TILE_VALUE_SIZE = 29

ACCENT = "#4fd1c5"
ACCENT_DIM = "#1f5f5a"
TEXT_BRIGHT = "#f2f6f8"
TEXT_DIM = "#7f8b99"
TRACK = "#1b222c"
