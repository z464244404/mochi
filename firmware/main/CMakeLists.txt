idf_component_register(
    SRCS
        "main.c"
        "gfx.c"
        "fonts.c"
        "apps.c"
        "builtin_apps.c"
        "dsec.c"
        "assets.c"
        "app_pet.c"
        "app_net.c"
        "lua_rt.c"
        "net.c"
        "web.c"
        "plugins.c"
        "plugin_conf.c"
        "builtin_plugins.c"
        "icons.c"
        "bg.c"
        "imu.c"
        "rtc.c"
        "sd.c"
        "io.c"
        "lcd.c"
    INCLUDE_DIRS "."
)

# 关键：默认 sqrtf/fmaxf 会因为要设置 errno 而走 libm 函数调用，
# 渲染内层循环每像素一次调用直接把帧率打到个位数。
# -fno-math-errno 后 sqrtf 内联成 sqrt.s，fmaxf 内联成 max.s。
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fno-math-errno
    -fno-trapping-math
)

# 临时：把余额轮询缩到 20 秒，并让桌宠 30/60 秒自动切动画，验证用
# target_compile_definitions(${COMPONENT_LIB} PRIVATE DSEC_FAST_TEST)

# 素材包（pet.mjp / work.mjp / sleep.mjp / icons.bin / bg.bin）烧到 assets 分区，
# 设备开机时 assets_sync() 只把「大小不一致」的文件写到 TF 卡，写完把分区擦掉。
# 素材包是生成物：python tools/pack_assets.py --out firmware/assets/assets.bin ...
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../assets/assets.bin")
    esptool_py_flash_to_partition(flash "assets" "${CMAKE_CURRENT_LIST_DIR}/../assets/assets.bin")
endif()
