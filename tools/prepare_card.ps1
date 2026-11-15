# 把一张空卡准备好：素材放根目录，字库放 fonts/
# 用法： .\tools\prepare_card.ps1 -Drive D
param(
    [Parameter(Mandatory = $true)][string]$Drive
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$letter = $Drive.TrimEnd(':', '\') + ':'
$dest = "$letter\"

if (-not (Test-Path $dest)) { throw "找不到 $dest" }

$vol = Get-Volume -DriveLetter ($letter.TrimEnd(':')) -ErrorAction Stop
Write-Host "目标卡: $letter  $($vol.FileSystem)  $([math]::Round($vol.Size/1GB,1)) GB"
if ($vol.FileSystem -ne 'FAT32') {
    Write-Warning "当前是 $($vol.FileSystem)，设备只认 FAT32 —— 请重新格式化为 FAT32 再跑一次"
}

$files = @(
    @{ src = 'firmware\assets\pet.mjp';   dst = 'pet.mjp' },
    @{ src = 'firmware\assets\work.mjp';  dst = 'work.mjp' },
    @{ src = 'firmware\assets\sleep.mjp'; dst = 'sleep.mjp' },
    @{ src = 'firmware\assets\icons.bin'; dst = 'icons.bin' },
    @{ src = 'firmware\assets\bg.bin';    dst = 'bg.bin' },
    @{ src = 'release\fontpack.bin';      dst = 'fonts\fontpack.bin' },
    @{ src = 'release\fontpack22.bin';    dst = 'fonts\fontpack22.bin' }
)

New-Item -ItemType Directory -Force -Path "$dest fonts" | Out-Null

$total = 0
foreach ($f in $files) {
    $src = Join-Path $root $f.src
    if (-not (Test-Path $src)) { Write-Warning "缺文件: $src"; continue }
    $out = Join-Path $dest $f.dst
    Copy-Item $src $out -Force
    $size = (Get-Item $out).Length
    $total += $size
    Write-Host ("  {0,-20} {1,8:N0} KB" -f $f.dst, ($size / 1KB))
}
Write-Host ("完成：{0} 个文件，共 {1:N0} MB" -f $files.Count, ($total / 1MB))
Write-Host "现在把卡插回设备，上电即可（插件会自动装回）。"
