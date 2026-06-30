# Minimal CMake build for the core (MIT) LibSensel.
# Builds the "no pressure" standalone variant: full contact data (position,
# force, area, ellipse, deltas) with no dependency on the closed-source
# LibSenselDecompress. Compiles natively on arm64 (Apple Silicon) and x86_64.
cmake_minimum_required(VERSION 3.16)
project(sensel C)

add_library(sensel STATIC
    src/sensel.c
    src/sensel_register.c
    src/sensel_serial_linux.c)   # POSIX serial layer; works on macOS and Linux

target_include_directories(sensel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(sensel PRIVATE SENSEL_EXPORTS)
set_target_properties(sensel PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

if(NOT WIN32)
    target_compile_options(sensel PRIVATE -fvisibility=hidden -O2 -Wall)
endif()
