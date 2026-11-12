# Lua 5.4.7（从 github.com/lua/lua v5.4.7 拉取）
# 只编核心 + 安全的标准库：base/string/table/math/utf8/coroutine
# 不注册 io/os/package/debug，插件拿不到文件和系统调用

file(GLOB LUA_SRC "src/*.c")

idf_component_register(
    SRCS ${LUA_SRC}
    INCLUDE_DIRS "src"
)

# 32 位数值：lua_Integer=int32, lua_Number=float（改的是 luaconf.h 里的 LUA_32BITS=1）。
# S3 的 FPU 是单精度，double 会走软浮点，慢好几倍。
# 不要再从命令行 -DLUA_32BITS，否则和头文件里的定义冲突（后者会覆盖成 0）。

# Lua 源码有些老式写法，不参与我们的严格告警
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
