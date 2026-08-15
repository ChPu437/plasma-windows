cmake_minimum_required(VERSION 3.21)

project(plasma-windows
    VERSION 0.4.0
    DESCRIPTION "Plasma Shell for Windows - KDE Plasma desktop shell on Windows"
    LANGUAGES CXX
)

# Pin the Windows SDK to the project target (10.0.19041 per AGENTS.md).
# Override on the command line with -DCMAKE_SYSTEM_VERSION=10.0.xxxxx
# if a different SDK is installed on the build machine.
set(CMAKE_SYSTEM_VERSION 10.0.19041 CACHE STRING "Windows SDK version used to build")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# AGENTS.md requires MSVC v143 (Visual Studio 2022).
if(MSVC AND MSVC_VERSION VERSION_LESS 1930)
    message(FATAL_ERROR "MSVC v143 (Visual Studio 2022, MSVC_VERSION >= 1930) is required.")
endif()

# Qt: point CMake at the installation with -DCMAKE_PREFIX_PATH,
# e.g. -DCMAKE_PREFIX_PATH=E:\Qt\6.11.1\msvc2022_64
# Plasma 6.7 (the Phase 3 target) requires Qt >= 6.10.
find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

add_executable(shell WIN32
    src/main.cpp
)

target_compile_definitions(shell PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(shell PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(shell PRIVATE
    Qt6::Widgets
    user32
)

qt_finalize_executable(shell)
