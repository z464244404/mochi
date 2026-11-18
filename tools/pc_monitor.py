#!/usr/bin/env python3
"""把电脑状态（CPU / 内存 / 网速 / 音量 / 开机时长）推给设备。

    python tools/pc_monitor.py --host 192.168.1.29

只写设备内存，不碰 SD 卡，所以 1 秒推一次也没关系。
"""
import argparse
import socket
import time
import urllib.parse
import urllib.request

try:
    import psutil
except ImportError:
    raise SystemExit("需要 psutil：pip install psutil")


def read_volume():
    """系统音量（pycaw）。没装或失败就返回 (-1, False)"""
    try:
        from pycaw.pycaw import AudioUtilities

        dev = AudioUtilities.GetSpeakers()
        vol = dev.EndpointVolume
        return float(vol.GetMasterVolumeLevelScalar()) * 100.0, bool(vol.GetMute())
    except Exception:
        return -1.0, False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True, help="设备 IP，例如 192.168.1.29")
    ap.add_argument("--interval", type=float, default=1.0, help="推送间隔秒")
    ap.add_argument("--dry-run", action="store_true", help="只打印不推送")
    args = ap.parse_args()

    hostname = socket.gethostname()
    boot = psutil.boot_time()
    psutil.cpu_percent(interval=None)      # 第一次调用总是 0，先喂一次

    prev_io = None
    prev_t = time.time()
    miss = 0

    print("推送电脑状态到 %s（Ctrl+C 退出）" % args.host)
    while True:
        cpu = psutil.cpu_percent(interval=None)
        mem = psutil.virtual_memory().percent
        now_io = psutil.net_io_counters()
        t = time.time()
        dt = t - prev_t
        if prev_io is not None and dt > 0:
            up = max((now_io.bytes_sent - prev_io.bytes_sent) / dt / 1048576.0, 0.0)
            down = max((now_io.bytes_recv - prev_io.bytes_recv) / dt / 1048576.0, 0.0)
        else:
            up = down = 0.0
        prev_io, prev_t = now_io, t

        vol, mute = read_volume()

        fields = {
            "host": hostname,
            "cpu": "%.1f" % cpu,
            "mem": "%.1f" % mem,
            "up": "%.2f" % up,
            "down": "%.2f" % down,
            "vol": "%.0f" % (vol if vol >= 0 else 0),
            "mute": "1" if mute else "0",
            "up_sec": int(t - boot),
        }

        if args.dry_run:
            print("CPU %5.1f%%  内存 %5.1f%%  音量 %3.0f%%%s  ↑%.2f ↓%.2f M/s  开机 %.1f 时" % (
                cpu, mem, vol, " (静音)" if mute else "", up, down, (t - boot) / 3600))
        else:
            try:
                q = urllib.parse.urlencode(fields)
                req = urllib.request.Request("http://%s/api/pc?%s" % (args.host, q),
                                             data=b"", method="POST")
                urllib.request.urlopen(req, timeout=4).read()
                if miss:
                    print("✓ 已恢复推送")
                    miss = 0
            except Exception as e:
                miss += 1
                if miss == 1 or miss % 30 == 0:
                    print("× 推送失败(%d): %s" % (miss, e))

        time.sleep(max(0.2, args.interval))


if __name__ == "__main__":
    main()
