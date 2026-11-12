cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.2.0")

# 本地组件（esp_new_jpeg 是预编译库，从 smart-emblem 里搬过来的，不依赖联网拉取）
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mochi)
