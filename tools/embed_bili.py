#!/usr/bin/env python3
"""
把 tools/bili_plugin.lua 嵌进 firmware/main/builtin_plugins.c 的 BILI_LUA 字符串。

    python tools/embed_bili.py             # 正常嵌入
    python tools/embed_bili.py --test-uid 373992388   # 顺便写死个 UID 方便本机验证

注意：BILI_LUA 后面紧跟的是 CLOCK_FILES 表，结束锚点不能写错，
写错会把 CLOCK_FILES 一起删掉（编译时报 'CLOCK_FILES' undeclared）。
"""
import argparse
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "bili_plugin.lua")
DST = os.path.join(ROOT, "firmware", "main", "builtin_plugins.c")


def cstr(name, lines):
    out = ['static const char *%s =' % name]
    for ln in lines:
        esc = ln.replace('\\', '\\\\').replace('"', '\\"')
        out.append('    "%s\\n"' % esc)
    out.append('    ;')
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--test-uid", help="写死 UID（只用于本机验证）")
    args = ap.parse_args()

    lua = io.open(SRC, encoding="utf-8").read().split("\n")
    if lua and lua[-1] == "":
        lua = lua[:-1]

    if args.test_uid:
        out = []
        for ln in lua:
            out.append(ln)
            if "uid = uid:gsub" in ln:
                out.append('  if uid == "" then uid = "%s" end' % args.test_uid)
        lua = out

    s = io.open(DST, encoding="utf-8").read()
    start = s.index("static const char *BILI_LUA =")
    end = s.index("static const builtin_file_t CLOCK_FILES[] = {")
    s = s[:start] + cstr("BILI_LUA", lua) + "\n" + s[end:]
    io.open(DST, "w", encoding="utf-8", newline="").write(s)
    print("embedded bili lua: %d lines%s" % (len(lua), ", test uid " + args.test_uid if args.test_uid else ""))


if __name__ == "__main__":
    main()
