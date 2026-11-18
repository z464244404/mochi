#!/usr/bin/env python3
# 探测：只用标准库 + psutil（不装任何额外组件）能拿到什么
import ctypes
import ctypes.wintypes as wt
import os
import platform
import shutil
import socket
import subprocess
import time

import psutil

OUT = []


def add(name, ok, value=""):
    OUT.append((name, "OK" if ok else "--", str(value)[:70]))


# ---------------- CPU / 内存 ----------------
add("CPU 使用率", True, "%.1f%%" % psutil.cpu_percent(interval=0.4))
add("CPU 核心数", True, "%d 物理 / %d 逻辑" % (psutil.cpu_count(False), psutil.cpu_count(True)))
try:
    f = psutil.cpu_freq()
    add("CPU 频率", f is not None, "%d MHz (最大 %d)" % (f.current, f.max) if f else "")
except Exception as e:
    add("CPU 频率", False, e)

vm = psutil.virtual_memory()
add("内存", True, "%.1f%%  %.1f/%.1f GB" % (vm.percent, vm.used / 2**30, vm.total / 2**30))
sw = psutil.swap_memory()
add("交换分区", True, "%.1f%%  %.1f GB" % (sw.percent, sw.used / 2**30))

# ---------------- 磁盘 ----------------
parts = []
for p in psutil.disk_partitions(all=False):
    try:
        u = psutil.disk_usage(p.mountpoint)
        parts.append("%s %.0f%%" % (p.mountpoint.rstrip("\\"), u.percent))
    except Exception:
        pass
add("磁盘占用", bool(parts), "  ".join(parts))
try:
    io = psutil.disk_io_counters()
    add("磁盘读写", True, "读 %.0f MB 写 %.0f MB (累计)" % (io.read_bytes / 2**20, io.write_bytes / 2**20))
except Exception as e:
    add("磁盘读写", False, e)

# ---------------- 网络 ----------------
try:
    io = psutil.net_io_counters()
    add("网络累计", True, "↑%.1f GB ↓%.1f GB" % (io.bytes_sent / 2**30, io.bytes_recv / 2**30))
    nics = [n for n in psutil.net_if_addrs() if n != "Loopback Pseudo-Interface 1"]
    add("网卡列表", True, ", ".join(nics[:4]))
except Exception as e:
    add("网络", False, e)

# 公网 IP
try:
    import urllib.request
    ip = urllib.request.urlopen("https://api.ipify.org", timeout=3).read().decode()
    add("公网 IP", True, ip)
except Exception as e:
    add("公网 IP", False, e)

# ---------------- 温度 / 风扇 / 电池 ----------------
try:
    t = psutil.sensors_temperatures()
    add("温度传感器", bool(t), t if t else "空（Windows 拿不到）")
except Exception as e:
    add("温度传感器", False, e)
try:
    fa = psutil.sensors_fans()
    add("风扇转速", bool(fa), fa if fa else "空")
except Exception as e:
    add("风扇转速", False, e)
try:
    b = psutil.sensors_battery()
    add("电池", b is not None, ("%.0f%% %s" % (b.percent, "充电中" if b.power_plugged else "放电")) if b else "无")
except Exception as e:
    add("电池", False, e)

# ---------------- 系统 / 进程 ----------------
add("开机时长", True, "%.1f 小时" % ((time.time() - psutil.boot_time()) / 3600))
add("进程数", True, "%d 进程 / %d 线程" % (len(psutil.pids()), sum(p.num_threads() for p in psutil.process_iter() if p.is_running())))
procs = []
for p in psutil.process_iter(["name", "cpu_percent", "memory_info"]):
    try:
        procs.append((p.info["name"], p.info["cpu_percent"] or 0, (p.info["memory_info"].rss / 2**20) if p.info["memory_info"] else 0))
    except Exception:
        pass
procs.sort(key=lambda x: -x[1])
add("Top 进程(CPU)", True, "  ".join("%s %.0f%%" % (n, c) for n, c, _ in procs[:3]))

# ---------------- Windows 特有（ctypes，无需第三方） ----------------
try:
    u32 = ctypes.windll.user32
    hwnd = u32.GetForegroundWindow()
    n = u32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(hwnd, buf, n + 1)
    add("当前窗口标题", bool(buf.value), buf.value)
except Exception as e:
    add("当前窗口标题", False, e)

try:
    class LASTINPUTINFO(ctypes.Structure):
        _fields_ = [("cbSize", wt.UINT), ("dwTime", wt.DWORD)]

    li = LASTINPUTINFO()
    li.cbSize = ctypes.sizeof(li)
    ctypes.windll.user32.GetLastInputInfo(ctypes.byref(li))
    idle = (ctypes.windll.kernel32.GetTickCount() - li.dwTime) / 1000.0
    add("键鼠空闲", True, "%.0f 秒" % idle)
except Exception as e:
    add("键鼠空闲", False, e)

try:
    out = subprocess.run(["nvidia-smi", "--query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu",
                          "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=4)
    add("GPU (nvidia-smi)", out.returncode == 0, out.stdout.strip() or out.stderr.strip())
except Exception as e:
    add("GPU (nvidia-smi)", False, "没装/无 N 卡")

try:
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "(Get-CimInstance Win32_VideoController | Select-Object -First 1 -ExpandProperty Name)"],
                         capture_output=True, text=True, timeout=6)
    add("显卡型号(WMI)", out.returncode == 0 and bool(out.stdout.strip()), out.stdout.strip())
except Exception as e:
    add("显卡型号(WMI)", False, e)

try:
    import pycaw  # noqa
    add("系统音量(pycaw)", True, "已安装")
except Exception:
    add("系统音量(pycaw)", False, "未安装 pycaw")

# ---------------- 输出 ----------------
print("%-16s %-4s %s" % ("项目", "状态", "实际值"))
print("-" * 78)
for n, s, v in OUT:
    print("%-16s %-4s %s" % (n, s, v))
