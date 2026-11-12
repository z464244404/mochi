# 截图

仓库里 `docs/preview-*.png` 是 **`tools/preview.py` 渲染的预览图**，不是真机照片：
它用同一套图元和字体在 PC 上重画一遍界面，用来排版设计，颜色和位置和真机基本一致，
但没有面板的背光、圆角玻璃和像素级的抗锯齿差异。

想要真机截图，目前有两条路：

## 方法一：手机拍（最简单）

设备放在桌上，关掉房间顶灯只留一点环境光，手机对着屏幕拍。
圆屏本身没有边框反光的话效果很好。拍完丢进 `docs/` 就行。

## 方法二：给固件加一个截图接口（还没做，TODO）

思路：在 `web.c` 里加一个 `GET /api/shot.jpg`——把当前帧缓冲用 `esp_jpeg_enc` 编码成 JPEG 直接吐出去，
浏览器打开这个地址就能另存为图片。

```c
/* 伪代码 */
static esp_err_t shot_get(httpd_req_t *r) {
    const uint16_t *fb = /* 当前帧缓冲 */;
    /* RGB565 -> RGB888 -> jpeg_enc -> send */
    httpd_resp_set_type(r, "image/jpeg");
    return httpd_resp_send(r, jpg, jpg_len);
}
```

注意设备 IP 要在你自己的局域网里（热点 192.168.4.1 或它显示的局域网 IP 都能访问）。

## 预览图怎么生成

```bash
python tools/preview.py            # 生成 docs/preview-*.png
python tools/preview.py --pet      # 只出桌宠那一张
```

它读的是 `firmware/assets/*.mjp` 里的实际帧 + 同一份字体数据，所以桌宠那张基本就是真机像素。
