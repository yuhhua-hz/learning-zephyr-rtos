# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_lightmodbus)

add_subdirectory(lib)

target_sources(app PRIVATE src/main.c)

target_link_libraries(app PRIVATE lightmodbus_lib)

target_compile_definitions(app PRIVATE
    LIGHTMODBUS_MASTER
)