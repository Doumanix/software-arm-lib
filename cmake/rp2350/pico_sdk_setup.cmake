# Pico SDK setup for RP2350 builds (SBLIB_MCU=RP2350).
#
# Must be included BEFORE project() of the top-level CMake project, e.g.
#
#   cmake_minimum_required(VERSION 3.28)
#   include(<sblib repo>/cmake/rp2350/pico_sdk_setup.cmake)
#   project(my_app C CXX ASM)
#   pico_sdk_init()
#
# Defaults (all can be overridden on the command line / in presets):
#   PICO_SDK_PATH       CMake variable, then environment; optional fetch of release 2.2.0
#   PICO_PLATFORM       rp2350-arm-s (Cortex-M33, secure)
#   SBLIB_BOARD         pico2_testboard (sblib pin mapping; custom + SBLIB_BOARD_HEADER for own boards)
#   PICO_BOARD          pico2 for SBLIB_BOARD=pico2_testboard (Pico SDK board: flash size, clocks)
#   PICO_TOOLCHAIN_PATH environment ARM_TOOLCHAIN_PATH or MCUXPRESSO_TOOLCHAIN_PATH,
#                       so LPC1115 and RP2350 are built with the same arm-none-eabi GCC
#   picotool_DIR        environment PICOTOOL_DIR (picotool >= 2.1.1)

if(DEFINED PROJECT_NAME)
    message(FATAL_ERROR "pico_sdk_setup.cmake must be included before project()")
endif()

# sblib board: pin mapping of the controller board (sblib/inc/sblib/io_pin_names.h).
#   pico2_testboard  Raspberry Pi Pico 2 with KNX bus coupler (Pico SDK board pico2)
#   custom           own board header, file name in SBLIB_BOARD_HEADER
set(SBLIB_BOARD "pico2_testboard" CACHE STRING "sblib board (pico2_testboard, custom)")
set_property(CACHE SBLIB_BOARD PROPERTY STRINGS pico2_testboard custom)
set(SBLIB_BOARD_HEADER "" CACHE STRING "Board header file for SBLIB_BOARD=custom")

set(PICO_PLATFORM "rp2350-arm-s" CACHE STRING "Pico SDK platform")
if(SBLIB_BOARD STREQUAL "pico2_testboard")
    set(PICO_BOARD "pico2" CACHE STRING "Pico SDK board")
    if(NOT PICO_BOARD STREQUAL "pico2")
        message(WARNING "SBLIB_BOARD=pico2_testboard expects PICO_BOARD=pico2, but PICO_BOARD is ${PICO_BOARD}")
    endif()
endif()

if(NOT PICO_TOOLCHAIN_PATH)
    if(DEFINED ENV{ARM_TOOLCHAIN_PATH} AND NOT "$ENV{ARM_TOOLCHAIN_PATH}" STREQUAL "")
        set(PICO_TOOLCHAIN_PATH "$ENV{ARM_TOOLCHAIN_PATH}" CACHE PATH "arm-none-eabi toolchain directory")
    elseif(DEFINED ENV{MCUXPRESSO_TOOLCHAIN_PATH} AND NOT "$ENV{MCUXPRESSO_TOOLCHAIN_PATH}" STREQUAL "")
        set(PICO_TOOLCHAIN_PATH "$ENV{MCUXPRESSO_TOOLCHAIN_PATH}" CACHE PATH "arm-none-eabi toolchain directory")
    endif()
endif()

# picotool (host tool, needed by the Pico SDK for RP2350 builds). Without it the SDK
# tries to download and build picotool from source, which needs a host C/C++ compiler.
# Point the environment variable PICOTOOL_DIR to a directory containing picotoolConfig.cmake.
if(NOT picotool_DIR AND DEFINED ENV{PICOTOOL_DIR} AND NOT "$ENV{PICOTOOL_DIR}" STREQUAL "")
    set(picotool_DIR "$ENV{PICOTOOL_DIR}" CACHE PATH "Directory containing picotoolConfig.cmake")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake")
