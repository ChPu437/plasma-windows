cmake_minimum_required(VERSION 3.21)

project(kf6-probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build inside the Craft environment: craftenv.ps1 sets CMAKE_PREFIX_PATH
# to D:\Projects\CraftRoot (Qt 6.11.1 + KF6 6.28.0 installed there).
find_package(Qt6 6.10 REQUIRED COMPONENTS Core)
find_package(KF6Config 6.26 REQUIRED)
find_package(KF6CoreAddons 6.26 REQUIRED)

add_executable(kf6-probe main.cpp)

target_link_libraries(kf6-probe PRIVATE Qt6::Core KF6::ConfigCore KF6::CoreAddons)
