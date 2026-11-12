param(
    [string]$Port = "COM10",
    [switch]$Monitor
)

# 一键烧录：导出 IDF 环境 -> idf.py flash
$ErrorActionPreference = "Stop"
$env:IDF_PATH = 'D:\esp\esp-idf'
$env:IDF_TOOLS_PATH = 'D:\esp\.espressif'

if (-not (Test-Path $env:IDF_PATH)) {
    Write-Error "找不到 ESP-IDF：$env:IDF_PATH（改脚本顶部的路径）"
}

& (Join-Path $env:IDF_PATH 'export.ps1') | Out-Null
Push-Location (Join-Path $PSScriptRoot '..\firmware')
try {
    idf.py -p $Port flash
    if ($Monitor) { idf.py -p $Port monitor }
} finally {
    Pop-Location
}
