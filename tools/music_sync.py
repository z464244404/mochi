#!/usr/bin/env python3
"""
音乐副屏 · 电脑端推送脚本

读 Windows 的系统媒体会话（网易云 / QQ 音乐 / Spotify / 浏览器都算），
把「歌名 / 歌手 / 进度 / 封面」推到 Mochi 摆件上。

    # 1. 装依赖（一次）
    python -m pip install winsdk pillow numpy

    # 2. 跑起来（设备 IP 看摆件「网络」屏）
    python tools/music_sync.py --host 10.215.198.102

    # 只看不改设备（调试用）
    python tools/music_sync.py --dry-run
"""
import argparse
import asyncio
import io
import sys
import time
from datetime import datetime, timezone
import urllib.parse
import urllib.request

from winsdk.windows.media.control import (
    GlobalSystemMediaTransportControlsSessionManager as SessionManager,
)
from winsdk.windows.storage.streams import Buffer, DataReader, InputStreamOptions

COVER_SIDE = 240


def http_post(url, data=b"", timeout=8):
    req = urllib.request.Request(url, data=data, headers={"User-Agent": "mochi-music"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def push_meta(host, title, artist, pos, dur, playing):
    q = urllib.parse.urlencode(
        {
            "title": title,
            "artist": artist,
            "pos": "%.1f" % pos,
            "dur": "%.1f" % dur,
            "playing": "1" if playing else "0",
            "ts": int(time.time()),
        }
    )
    return http_post("http://%s/api/music?%s" % (host, q))


def to_rgb565(im):
    """PIL 图 -> 240x240 RGB565 小端字节"""
    import numpy as np

    im = im.convert("RGB").resize((COVER_SIDE, COVER_SIDE))
    a = np.asarray(im, dtype=np.uint16)
    r = (a[:, :, 0] & 0xF8) << 8
    g = (a[:, :, 1] & 0xFC) << 3
    b = a[:, :, 2] >> 3
    return (r | g | b).astype("<u2").tobytes()


async def read_cover(session):
    from PIL import Image

    props = await session.try_get_media_properties_async()
    if not props or not props.thumbnail:
        return None
    stream = await props.thumbnail.open_read_async()
    size = stream.size
    if size <= 0 or size > 8 * 1024 * 1024:
        return None
    buf = Buffer(size)
    await stream.read_async(buf, size, InputStreamOptions.NONE)
    data = bytearray(size)
    DataReader.from_buffer(buf).read_bytes(data)
    try:
        return Image.open(io.BytesIO(bytes(data)))
    except Exception:
        return None


async def main_async(args):
    mgr = await SessionManager.request_async()
    print("开始监听系统媒体…（Ctrl+C 退出）")

    last_key = None
    last_push = 0.0
    last_playing = None
    last_pos = 0.0          # 同一首歌的单调基准，防止 SMTC 时间轴抖动导致回跳
    back_ref = None         # 上一次可疑的回退位置
    back_cnt = 0            # 连续回退次数

    while True:
        try:
            session = mgr.get_current_session()
            if not session:
                if last_key is not None:
                    if not args.dry_run:
                        push_meta(args.host, "", "", 0, 0, False)
                    print("  [空闲] 没有播放")
                    last_key = None
                await asyncio.sleep(args.interval)
                continue

            props = await session.try_get_media_properties_async()
            tl = session.get_timeline_properties()
            info = session.get_playback_info()
            playing = int(info.playback_status) == 4  # PLAYING

            title = (props.title or "").strip()
            artist = (props.artist or "").strip()

            # SMTC 的 position 只在暂停/拖动时更新，平时是冻结的；
            # 真正进度 = position + (现在 - last_updated_time)
            pos = tl.position.total_seconds()
            dur = tl.end_time.total_seconds()
            if playing and tl.last_updated_time is not None:
                try:
                    delta = (datetime.now(timezone.utc) - tl.last_updated_time).total_seconds()
                    if 0 <= delta < 3600:    # Chrome 播放期间不更新时间轴，delta 会一直增长
                        pos += delta
                except Exception:
                    pass
            if dur > 0 and pos > dur:
                pos = dur
            if pos < 0:
                pos = 0

            # 进度防抖：
            #  Chrome 偶尔报「非播放」状态，那时 last_updated 不参与计算，
            #  pos 会掉回 0.3 秒，屏上闪一下 0:00。
            #  规则：
            #    暂停        -> 冻结在上一帧位置
            #    回退 1 次   -> 当成抖动，钳住
            #    回退连续 2 次 -> 认为用户拖回前面，接受
            if not playing:
                pos = last_pos
            elif pos < last_pos - 2:
                if back_ref is not None and abs(pos - back_ref) < 3:
                    back_cnt += 1
                else:
                    back_ref = pos
                    back_cnt = 1
                if back_cnt >= 2:
                    last_pos = pos        # 真的拖回去了
                    back_cnt = 0
                else:
                    pos = last_pos        # 第一次先钳住
            else:
                back_cnt = 0
                if pos > last_pos:
                    last_pos = pos
            key = (title, artist)

            if key != last_key:
                last_key = key
                last_pos = 0.0
                back_ref = None
                back_cnt = 0
                cover = await read_cover(session)
                if cover is not None and not args.dry_run:
                    try:
                        push_cover(args.host, cover)
                    except Exception as e:
                        print("× 封面上传失败:", e)
                print("  [换歌] %s - %s  (%s)" % (title, artist, "封面已传" if cover is not None else "无封面"))
                last_push = 0

            # 位置每秒都在变，每轮都推（只写设备内存，不伤卡）
            if not args.dry_run:
                try:
                    push_meta(args.host, title, artist, pos, dur, playing)
                except Exception as e:
                    print("× 推送失败:", e)
            last_push = time.time()
            last_playing = playing
            if args.dry_run:
                print("  %s %d:%02d/%d:%02d" %
                      ("播放" if playing else "暂停", pos // 60, pos % 60, dur // 60, dur % 60))

        except KeyboardInterrupt:
            raise
        except Exception as e:
            print("× %s" % e)

        await asyncio.sleep(args.interval)


def push_cover(host, im):
    raw = to_rgb565(im)
    http_post("http://%s/api/music/cover" % host, raw, timeout=30)



# 单实例保护：两个 music_sync 同时在跑会互相覆盖进度，屏上会来回跳 0:00
_LOCK_SOCK = None


def acquire_single_instance(port=47890):
    """占用一个本地端口当作锁；已被占用说明已经有一个在跑了"""
    import socket

    global _LOCK_SOCK
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.bind(("127.0.0.1", port))
        sock.listen(1)
        _LOCK_SOCK = sock
        return True
    except OSError:
        sock.close()
        return False


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    ap = argparse.ArgumentParser(description="把正在播放的歌推到 Mochi 摆件")
    ap.add_argument("--host", help="摆件 IP（看设备『网络』屏）")
    ap.add_argument("--interval", type=float, default=0.5, help="轮询间隔秒，默认 0.5")
    ap.add_argument("--sync-every", type=float, default=0.0, help="(已废弃，现在每轮都推)")
    ap.add_argument("--dry-run", action="store_true", help="只打印，不推设备")
    args = ap.parse_args()

    if not args.dry_run and not args.host:
        sys.exit("需要 --host，例如 --host 10.215.198.102")

    if not args.dry_run and not acquire_single_instance():
        sys.exit("已经有一个 music_sync 在跑了，先把旧的关掉（否则进度会来回跳）")

    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        print("\n退出")


if __name__ == "__main__":
    main()
