# 截图

## 实拍

`docs/photos/` 下三张是手机实拍（裁到设备区域、压到 720 宽）：

| 文件 | 屏幕 |
|---|---|
| `pet.jpg` | 桌宠（打字动画）+ 余额 10.21 / 今日 1.17 / 谷 |
| `weather.jpg` | 天气（贵阳 12℃ 阴 + 三天预报） |
| `clock.jpg` | 时钟 minimal 样式 |

拍法：关掉房间顶灯只留一点环境光，手机对着屏幕拍，别开闪光灯。
拍完用 `PIL` 按暗区投影裁一下就能去掉桌面背景。

## 渲染预览

`docs/preview-*.png` 是 `tools/preview.py` 用同一套图元和字体在 PC 上重画的，用于排版设计。
颜色和位置和真机基本一致，但没有背光、圆角玻璃和像素级抗锯齿的差异。

```bash
python tools/preview.py            # 生成 docs/preview-*.png
python tools/preview.py --pet      # 只出桌宠那一张
```

桌宠那张读的是 `firmware/assets/*.mjp` 里的实际帧，所以基本就是真机像素。

## 想要「无相机」的真截图

给固件加一个 `GET /api/shot.jpg`（还没做）：把当前帧缓冲用 `esp_jpeg_enc` 编码成 JPEG 直接吐出去，
浏览器打开就能另存。设备在局域网里时（热点 `192.168.4.1` 或它显示的 IP）都能用。
