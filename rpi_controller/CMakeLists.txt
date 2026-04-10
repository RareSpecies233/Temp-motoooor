cmake_minimum_required(VERSION 3.16)
project(rpi5_stm32_controller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(rpi5_stm32_controller
    src/main.cpp
)

if(WIN32)
    target_compile_definitions(rpi5_stm32_controller PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
