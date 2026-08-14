cmake_minimum_required(VERSION 3.21)
project(titlebar-qml)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

qt_add_executable(titlebar-qml
    main.cpp
    bridge.cpp
    bridge.h
)

qt_add_resources(titlebar-qml "qml_res"
    PREFIX "/"
    FILES main.qml BarButton.qml
)

target_link_libraries(titlebar-qml PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

if(WIN32)
    target_compile_definitions(titlebar-qml PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(titlebar-qml PRIVATE user32 gdi32 dwmapi)
endif()
