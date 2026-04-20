cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
  set(PICO_SDK_FETCH_FROM_GIT ON)
endif()

include(pico_sdk_import.cmake)

project(pico_switch_controller C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_switch_controller
    src/main.c
    src/usb_descriptors.c
)

target_include_directories(pico_switch_controller PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(pico_switch_controller PRIVATE
    pico_stdlib
    pico_unique_id
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_usb(pico_switch_controller 0)
pico_enable_stdio_uart(pico_switch_controller 0)

pico_add_extra_outputs(pico_switch_controller)
