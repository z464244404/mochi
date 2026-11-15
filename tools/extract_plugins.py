#!/usr/bin/env python3
"""
把固化在 firmware/main/builtin_plugins.c 里的插件源码拆回独立目录：

    plugins/<id>/manifest.json
    plugins/<id>/main.lua

这样每个插件都能单独发一个仓库，也方便别人只拷贝自己需要的。
"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "firmware", "main", "builtin_plugins.c")
OUT = os.path.join(ROOT, "plugins")

PLUGINS = [
    ("clock", "CLOCK_MANIFEST", "CLOCK_LUA"),
    ("weather", "WEATHER_MANIFEST", "WEATHER_LUA"),
    ("dfm", "DFM_MANIFEST", "DFM_LUA"),
    ("bili", "BILI_MANIFEST", "BILI_LUA"),
]

# 内容行形如：    "...."      或   "....";
LINE_RE = re.compile(r'^    "(.*)"(;?)$')


def read_c_string(text, name):
    """取出 static const char *NAME = "..." "..." [;] 的内容并反转义。

    C 里两种结尾写法都有：分号独占一行，或者跟在最后一个字符串后面
    （`"...\n";`），所以按行读到「该行以分号结尾」为止。
    Lua 里出现的 `;` 不会误伤。
    """
    start = text.index("static const char *%s =" % name)
    out = []
    for line in text[start:].split("\n")[1:]:
        m = LINE_RE.match(line)
        if not m:
            break
        s = m.group(1)
        s = s.replace('\\"', '"')
        if s.endswith("\\n"):
            s = s[:-2]
        out.append(s)
        if m.group(2) == ";":
            break
    return "\n".join(out) + "\n"


def main():
    src = io.open(SRC, encoding="utf-8").read()
    for pid, man_name, lua_name in PLUGINS:
        manifest = read_c_string(src, man_name)
        lua = read_c_string(src, lua_name)
        d = os.path.join(OUT, pid)
        os.makedirs(d, exist_ok=True)
        io.open(os.path.join(d, "manifest.json"), "w", encoding="utf-8", newline="").write(manifest)
        io.open(os.path.join(d, "main.lua"), "w", encoding="utf-8", newline="").write(lua)
        print("%-8s manifest %4d B   main.lua %5d B (%d lines)" %
              (pid, len(manifest.encode()), len(lua.encode()), lua.count("\n")))


if __name__ == "__main__":
    main()
