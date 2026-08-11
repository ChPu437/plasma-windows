cmake_minimum_required(VERSION 3.21)

project(kwindowsystem-probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui)
find_package(KF6WindowSystem 6.26 REQUIRED)

add_executable(kwindowsystem-probe main.cpp)

target_link_libraries(kwindowsystem-probe PRIVATE Qt6::Gui KF6::WindowSystem)
