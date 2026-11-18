import time

import psutil

print("采样 CPU 频率 / 使用率（每秒一次，共 6 次）")
psutil.cpu_percent(interval=None)
for i in range(6):
    f = psutil.cpu_freq()
    c = psutil.cpu_percent(interval=None)
    v = psutil.virtual_memory()
    print("  %d) 频率 %s MHz   CPU %4.1f%%   内存 %4.1f%%" %
          (i + 1, ("%.0f" % f.current) if f else "-", c, v.percent))
    if i == 2:
        # 制造一点负载，看频率会不会动
        t = time.time()
        while time.time() - t < 1.2:
            sum(i * i for i in range(20000))
    time.sleep(1.0)

print("\n音量读取 3 次（pycaw）")
from pycaw.pycaw import AudioUtilities
for i in range(3):
    sp = AudioUtilities.GetSpeakers()
    v = sp.EndpointVolume
    print("  %.0f%%  静音=%s" % (v.GetMasterVolumeLevelScalar() * 100, bool(v.GetMute())))
    time.sleep(0.6)
