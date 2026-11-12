# 发布产物

`mochi.bin` 是 2026-11-12 的构建结果（ESP-IDF v5.5.2，esp32s3，444 KB）。

整片烧录（16MB Flash，会清掉 NVS 之外的原有内容）：

```powershell
python -m esptool --chip esp32s3 -p COM10 -b 460800 `
  --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_size 16MB --flash_freq 80m `
  0x0     bootloader.bin `
  0x8000  partition-table.bin `
  0xf000  ota_data_initial.bin `
  0x20000 mochi.bin
```

或者直接用 `..\tools\flash.ps1 -Port COM10`（会重新编译一遍再烧）。

分区表：

```
nvs        0x009000   24KB
otadata    0x00f000    8KB
phy_init   0x011000    4KB
ota_0      0x020000    3MB
ota_1      0x320000    3MB
assets     0x620000  9.875MB  (littlefs，留给 M2 的插件)
```
