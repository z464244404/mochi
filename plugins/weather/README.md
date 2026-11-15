# 天气 · Mochi 插件

给 [Mochi](https://github.com/z464244404/mochi) 桌面摆件的天气屏：实时温度 + 未来三天。

![weather](https://raw.githubusercontent.com/z464244404/mochi/main/docs/photos/weather.jpg)

## 背景

`backgrounds/` 里是 16 张 480×480 的背景样例，就是打包进 `bg.bin` 的那一份（JPEG q88，平均 18 KB）：

![背景总览](backgrounds/contact-sheet.jpg)

| 文件 | 用于 |
|---|---|
| `clear-day.jpg` / `clear-day-dark.jpg` | 晴（白天）—— 白色文字要用 `-dark` 版，浅底看不清 |
| `clear-night.jpg` | 晴（夜间） |
| `partly-cloudy-day.jpg` / `-dark.jpg` | 多云（白天） |
| `partly-cloudy-night.jpg` | 多云（夜间） |
| `cloudy.jpg` | 阴 |
| `fog.jpg` | 雾 |
| `drizzle.jpg` | 毛毛雨 |
| `rain.jpg` | 小 / 中雨 |
| `overcast-rain.jpg` | 大雨 / 暴雨 |
| `partly-cloudy-day-rain.jpg` / `-dark.jpg` | 阵雨（白天） |
| `sleet.jpg` | 冻雨 |
| `snow.jpg` | 雪 |
| `thunderstorms-rain.jpg` | 雷阵雨 |

换背景：把同名文件替换掉，重新跑 `python tools/pack_bg.py` 生成 `bg.bin`，
再 `python tools/pack_assets.py` 打包烧进设备即可（详见主仓库）。

## 特性

- 数据源 **Open-Meteo**，免费、免注册、无需 API key
- 图标是 [Meteocons](https://meteocons.com/) 的原始动画帧（存在卡上 `icons.bin`）
- 背景随天气变化（`bg.bin` 里 16 张背景）
- 中文天气词（晴/多云/阴/小雨/中雨/大雨/暴雨/雷阵雨/雪/雾）
- 每 5 分钟刷新，数据缓存到卡上，断网也不报错
- 未来三天横排三列：日期 / 图标 / 高温（橙）/ 低温（蓝）

## 安装

网页后台「安装 / 更新插件」→ 选中 `manifest.json` + `main.lua` → 上传。

天气图标和背景在固件的素材包里（`icons.bin` / `bg.bin`），不用单独装。

## 设置

| 项 | 说明 | 默认 |
|---|---|---|
| 地名 | 显示在顶部 | 深圳 |
| 纬度 / 经度 | Open-Meteo 查询用 | 22.54 / 114.06 |
| 单位 | `c` 摄氏 / `f` 华氏 | `c` |

## 换城市

只改「地名」不会换数据源，**纬度和经度也要改**。查坐标：在 [openstreetmap.org](https://www.openstreetmap.org/) 搜城市，右键「显示地址」就能看到。

## 说明

- 接口：`https://api.open-meteo.com/v1/forecast?...&forecast_days=4`
- 只支持 2.4G WiFi
- 缺字会显示空白，装 `fontpack.bin` 后任意中文可显示
