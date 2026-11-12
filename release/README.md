# 发布产物

这个目录只放**打好的固件包**（不进 git，见 `.gitignore`）。重新生成：

```powershell
# 1. 构建
cd firmware
idf.py build

# 2. 素材包（如果 assets/assets.bin 不存在）
cd ..
python tools/pack_assets.py --out firmware/assets/assets.bin `
    --add pet.mjp=firmware/assets/pet.mjp `
    --add work.mjp=firmware/assets/work.mjp `
    --add sleep.mjp=firmware/assets/sleep.mjp `
    --add icons.bin=firmware/assets/icons.bin `
    --add bg.bin=firmware/assets/bg.bin

# 3. 打包
cd firmware
python -m esptool --chip esp32s3 merge_bin -o ..\release\mochi-<版本>-full.bin `
  0x0 build\bootloader\bootloader.bin `
  0x8000 build\partition_table\partition-table.bin `
  0x20000 build\mochi.bin `
  0x620000 ..\firmware\assets\assets.bin
Copy-Item build\mochi.bin ..\release\mochi-<版本>-ota.bin
```

## 两个文件怎么用

| 文件 | 用途 | 命令 |
|---|---|---|
| `mochi-x.y.z-full.bin` | **全新烧录/救砖**，含 bootloader、分区表、固件、素材 | `esptool --chip esp32s3 -p COM10 write_flash 0x0 mochi-x.y.z-full.bin` |
| `mochi-x.y.z-ota.bin` | **网页 OTA 升级**，只换固件不动素材和 NVS | 网页「固件」上传，或 `curl --data-binary @... /api/ota` |

刷 full 之后第一次开机会把素材从 assets 分区同步到 TF 卡，然后自动擦除该分区。

## 分区表

```
nvs        0x009000   24KB
otadata    0x00f000    8KB
phy_init   0x011000    4KB
ota_0      0x020000    3MB
ota_1      0x320000    3MB
assets     0x620000  9.875MB   （素材分发用，拷完自动擦除）
```
