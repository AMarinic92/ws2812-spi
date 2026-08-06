cmake_minimum_required(VERSION 3.16)

project(ws2812_spi LANGUAGES C)

option(WS2812_ENABLE_FREERTOS "Build the FreeRTOS task-notification path" OFF)

add_library(ws2812 STATIC
    src/ws2812.c
    src/ws2812_fx.c
    src/ws2812_freertos.c)

target_include_directories(ws2812 PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

if(WS2812_ENABLE_FREERTOS)
    target_compile_definitions(ws2812 PUBLIC WS2812_ENABLE_FREERTOS)
endif()

# This target does not build standalone. It needs the MCC-generated peripheral
# headers (peripheral/dmac/plib_dmac.h, device.h) and the device pack headers,
# which only exist inside a consuming MPLAB project. Supply them with:
#
#   target_include_directories(ws2812 PRIVATE <path-to>/src/config/default ...)
#
# In MPLAB X projects whose file set globs the repo, you do not need this file
# at all - the sources are picked up and compiled with the project's own flags.
# See README.md.
