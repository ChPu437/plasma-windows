cmake_minimum_required(VERSION 3.20)
project(imewin VERSION 1.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick)
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
find_package(KF6Config CONFIG REQUIRED)
find_package(KF6CoreAddons CONFIG REQUIRED)
find_package(KF6I18n CONFIG REQUIRED)
find_package(KF6Package CONFIG REQUIRED)
find_package(KF6Service CONFIG REQUIRED)
find_package(Plasma CONFIG REQUIRED)

add_library(org.kde.plasma.imewin MODULE imecontroller.cpp imecontroller.h imewin.cpp)
target_link_libraries(org.kde.plasma.imewin PRIVATE Qt6::Core Qt6::Qml Qt6::Quick Plasma::Plasma KF6::CoreAddons KF6::I18n ole32 uuid)
target_compile_definitions(org.kde.plasma.imewin PRIVATE -DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_TO_ASCII)
set_target_properties(org.kde.plasma.imewin PROPERTIES PREFIX "")

install(TARGETS org.kde.plasma.imewin DESTINATION plugins/plasma/applets)

# KPackage data
install(DIRECTORY plasmoid/ DESTINATION ${KDE_INSTALL_DATADIR}/plasma/plasmoids/org.kde.plasma.imewin)





