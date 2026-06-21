# AttentioLight-1 Wireless Module Firmware — root project CMakeLists.
# Sources ESP-IDF's project.cmake (set via IDF_PATH after sourcing export.sh).

cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Trim the build to the minimum component set + main + dependencies.
idf_build_set_property(MINIMAL_BUILD ON)

project(fw_al1_wmod VERSION 1.0.0)
