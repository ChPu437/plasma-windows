cmake_minimum_required(VERSION 3.21)

project(dbus-probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core DBus)

add_executable(dbus-probe main.cpp)

target_link_libraries(dbus-probe PRIVATE Qt6::Core Qt6::DBus)
