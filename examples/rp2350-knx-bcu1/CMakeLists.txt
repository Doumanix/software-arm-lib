cmake_minimum_required(VERSION 3.28)

# Minimal KNX BCU1 device on the RP2350 with the complete sblib (KNX stack, bus timer
# emulation, flash storage) and the sblib main(). No outputs.
# Identifies as out8-bcu1 (Jung 2138.10: manufacturer 0x04, device type 0x2060, version 1),
# so the ETS product of out8-bcu1 can be used to read the device info, set the physical
# address and download.

set(SBLIB_MCU "RP2350" CACHE STRING "Target microcontroller")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rp2350/pico_sdk_setup.cmake)

project(rp2350-knx-bcu1 LANGUAGES C CXX ASM VERSION 1.0)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

# Diagnostics of the timer emulation, printed once per second (see src/app_main.cpp)
option(SBLIB_RP2350_TIMER_STATS "Diagnostics: counters of the RP2350 timer emulation" ON)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../sblib sblib)

set(TARGET ${PROJECT_NAME})
add_executable(${TARGET} src/app_main.cpp)
target_link_libraries(${TARGET} PRIVATE pico_stdlib sblib_rp2350)
target_compile_definitions(${TARGET} PRIVATE
    PICO_DEFAULT_UART=1
    PICO_DEFAULT_UART_TX_PIN=8
    PICO_DEFAULT_UART_RX_PIN=9
)
pico_enable_stdio_uart(${TARGET} 1)
pico_enable_stdio_usb(${TARGET} 0)

# The Pico SDK toolchain does not set CMAKE_SIZE; look for size next to the compiler.
if(NOT CMAKE_SIZE)
    get_filename_component(_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(CMAKE_SIZE NAMES arm-none-eabi-size HINTS "${_toolchain_bin}" NO_DEFAULT_PATH REQUIRED)
endif()

# ELF, BIN, HEX without picotool (UF2 needs picotool and is not required for SWD flashing)
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>/${TARGET}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>/${TARGET}.hex
    VERBATIM
)
