cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH} OR NOT EXISTS "$ENV{IDF_PATH}/tools/cmake/project.cmake")
    message(FATAL_ERROR
        "ESP-IDF is not available through IDF_PATH. Activate an installed ESP-IDF "
        "environment with its export.sh before building firmware. "
        "For host checks, configure with: cmake -S tests -B /tmp/rpw-acoustic-tests")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(IDF_TARGET esp32 CACHE STRING "Target MCU")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rpw_acoustic_node)
