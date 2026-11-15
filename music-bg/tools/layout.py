# -*- coding: utf-8 -*-
"""
音乐播放页的版式常量 —— gen.py 和 preview.py 都从这里取，保证「画图的」和「标尺的」一致。

    封面圆   直径 240，圆心 (240, 150)
    曲名     29px，基线 y=310，居中，超宽截断
    歌手     22px，基线 y=344，居中
    进度条   x 100..380，y=378，轨高 4，旋钮 r=7
    时间     22px，基线 y=404，左边 left@100，右边 right@380
"""

# 封面
COVER_CX, COVER_CY = 240, 150
COVER_D = 240
COVER_R = COVER_D // 2

# 曲名 / 歌手
TITLE_BASELINE = 310
TITLE_SIZE = 29
ARTIST_BASELINE = 344
ARTIST_SIZE = 22
TEXT_MAX_W = 360          # 超过就截断加 ..

# 进度条
BAR_X0, BAR_X1 = 100, 380
BAR_Y = 378
BAR_H = 4
KNOB_R = 7

# 时间
TIME_BASELINE = 404
TIME_SIZE = 22
TIME_LEFT_X = BAR_X0
TIME_RIGHT_X = BAR_X1

# 播放/暂停时旋钮的颜色（固件切换，背景里不要画）
GREEN_PLAY = 0x5BE0BD
GREY_PAUSE = 0x5A6270
