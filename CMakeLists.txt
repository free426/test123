cmake_minimum_required(VERSION 3.16)
project(MyPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

set(DAQ_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH "DAQ SDK 路径")

include(${DAQ_SDK_DIR}/cmake/DaqPlugin.cmake)

add_library(${PROJECT_NAME} SHARED
    src/MyPlugin.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Gui)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Gui)

daq_plugin_setup(${PROJECT_NAME})
