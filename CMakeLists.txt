# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set custom partition table
set(PARTITION_CSV_PATH ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv)

# 设置项目版本（必须与 common.h 中的 FIRMWARE_VERSION_MAIN 保持一致，用于OTA验证）
set(PROJECT_VER "1.0.3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(wifi_station)
