#!/usr/bin/env python3
import ctypes
import time

import psutil


def try_msacpi():
    try:
        out = ctypes.windll.ole32
    except Exception:
        pass
    import subprocess
    ps = ("$t = Get-CimInstance -Namespace root/WMI -ClassName MSAcpi_ThermalZoneTemperature "
          "-ErrorAction SilentlyContinue; if ($t) { $t | ForEach-Object { '{0} {1:N1}C' "
          "-f $_.InstanceName, ($_.CurrentTemperature/10-273.15) } } else { 'NO' }")
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                       capture_output=True, text=True, timeout=12)
    return (r.stdout or "").strip() or (r.stderr or "").strip()


def gpus():
    import subprocess
    ps = ("Get-CimInstance Win32_VideoController | ForEach-Object { "
          "'{0} | {1:N0} MB | {2}' -f $_.Name, ($_.AdapterRAM/1MB), $_.DriverVersion }")
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                       capture_output=True, text=True, timeout=12)
    return (r.stdout or "").strip()


def top_procs():
    psutil.cpu_percent(interval=None)
    for p in psutil.process_iter():
        try:
            p.cpu_percent(None)
        except Exception:
            pass
    time.sleep(1.0)
    rows = []
    for p in psutil.process_iter(["name", "memory_info"]):
        try:
            c = p.cpu_percent(None)
            m = p.info["memory_info"].rss / 2**20 if p.info["memory_info"] else 0
            rows.append((p.info["name"], c, m))
        except Exception:
            pass
    rows.sort(key=lambda x: -x[1])
    return ["%s %.0f%% %.0fMB" % r for r in rows[:5]]


def volume():
    try:
        from pycaw.pycaw import AudioUtilities
        from ctypes import cast, POINTER
        from comtypes import CLSCTX_ALL
        from pycaw.pycaw import IAudioEndpointVolume
        dev = AudioUtilities.GetSpeakers()
        iface = dev.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
        vol = cast(iface, POINTER(IAudioEndpointVolume))
        return "%.0f%% %s" % (vol.GetMasterVolumeLevelScalar() * 100,
                              "静音" if vol.GetMute() else "有声")
    except Exception as e:
        return "失败: %s" % e


print("=== WMI 温度（MSAcpi）===")
print(try_msacpi()[:300])
print("\n=== 显卡列表（WMI）===")
print(gpus()[:400])
print("\n=== Top 5 进程（CPU）===")
print(" | ".join(top_procs()))
print("\n=== 系统音量（pycaw）===")
print(volume())
print("\n=== 电池 ===")
b = psutil.sensors_battery()
print("%.0f%% 插电=%s 剩余=%s" % (b.percent, b.power_plugged,
                                  "%d 分" % (b.secsleft // 60) if b.secsleft and b.secsleft > 0 else "计算中") if b else "无")
