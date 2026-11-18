#!/usr/bin/env python3
"""把 tools/pc_plugin.lua 嵌进 firmware/main/builtin_plugins.c（PC_LUA / PC_FILES / BUILTINS）。

    python tools/embed_pc.py
"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "pc_plugin.lua")
DST = os.path.join(ROOT, "firmware", "main", "builtin_plugins.c")

MANIFEST = """{
  "id": "pc",
  "name": "电脑",
  "version": "1.0.0",
  "api": 1,
  "author": "mochi",
  "settings": [
    {"key": "hint", "label": "数据来源", "type": "text",
     "default": "电脑上运行 python tools/pc_monitor.py"}
  ]
}
"""


def cstr(name, lines):
    out = ['static const char *%s =' % name]
    for ln in lines:
        esc = ln.replace('\\', '\\\\').replace('"', '\\"')
        out.append('    "%s\\n"' % esc)
    out.append('    ;')
    return '\n'.join(out) + '\n'


def main():
    lua = io.open(SRC, encoding="utf-8").read().split("\n")
    if lua and lua[-1] == "":
        lua = lua[:-1]

    s = io.open(DST, encoding="utf-8").read()
    block = cstr("PC_MANIFEST", MANIFEST.rstrip("\n").split("\n")) + "\n" + cstr("PC_LUA", lua)

    if "static const char *PC_LUA =" in s:
        start = s.index("static const char *PC_MANIFEST =")
        end = s.index("static const builtin_file_t CLOCK_FILES[] = {")
        s = s[:start] + block + "\n" + s[end:]
    else:
        anchor = "static const builtin_file_t CLOCK_FILES[] = {"
        s = s.replace(anchor, block + "\n" + anchor, 1)
        s = s.replace(
            "static const builtin_file_t BILI_FILES[] = {",
            "static const builtin_file_t PC_FILES[] = {\n"
            '    {"manifest.json", NULL},\n'
            '    {"main.lua", NULL},\n'
            "};\n\n"
            "static const builtin_file_t BILI_FILES[] = {",
            1,
        )
        s = s.replace(
            '    if (!strcmp(p->id, "bili")) {',
            '    if (!strcmp(p->id, "pc")) {\n'
            '        if (!strcmp(name, "manifest.json")) return PC_MANIFEST;\n'
            '        if (!strcmp(name, "main.lua")) return PC_LUA;\n'
            "    }\n"
            '    if (!strcmp(p->id, "bili")) {',
            1,
        )
        s = s.replace(
            '    {"bili", "哔哩哔哩", "1.3.0", BILI_FILES},',
            '    {"pc", "电脑", "1.0.0", PC_FILES},\n'
            '    {"bili", "哔哩哔哩", "1.3.0", BILI_FILES},',
            1,
        )

    io.open(DST, "w", encoding="utf-8", newline="").write(s)
    print("embedded pc plugin: %d lines" % len(lua))


if __name__ == "__main__":
    main()
