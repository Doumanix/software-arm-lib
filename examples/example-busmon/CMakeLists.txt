cmake_minimum_required(VERSION 3.28)

# RP2350 build of the bus monitor (Pico 2 test board, preset rp2350-pico2). The LPC1115 build
# uses the MCUXpresso project (.cproject). The RP2350 differences are in this file and in
# src/rp2350/: serial_rp2350.cpp (serial port on UART1: TX GPIO8, RX GPIO9, 115200 baud) and
# cmsis_rp2350.h (__WFI()). src/app_main.cpp is the same for both.

set(SBLIB_MCU "RP2350" CACHE STRING "Target microcontroller")

# The storage of the sblib (UserEeprom) is placed at the end of the flash. A flash size of 2 MiB
# keeps it away from the storage of an application at the end of the 4 MiB flash of the board,
# so the example does not change the stored configuration of that application.
set(PICO_FLASH_SIZE_BYTES 2097152 CACHE STRING "Flash size used by the example")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rp2350/pico_sdk_setup.cmake)

project(example-busmon LANGUAGES C CXX ASM VERSION 1.0)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

# Same flash size for the code (iapFlashSize()) as for the linker script
add_compile_definitions(PICO_FLASH_SIZE_BYTES=${PICO_FLASH_SIZE_BYTES})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../sblib sblib)

set(TARGET ${PROJECT_NAME})
add_executable(${TARGET}
    src/app_main.cpp
    src/rp2350/serial_rp2350.cpp
)
target_link_libraries(${TARGET} PRIVATE pico_stdlib hardware_uart sblib_rp2350)
# __WFI() of the LPC1115 source
set_source_files_properties(src/app_main.cpp PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/src/rp2350/cmsis_rp2350.h"
)
pico_enable_stdio_uart(${TARGET} 0)
pico_enable_stdio_usb(${TARGET} 0)

# The Pico SDK toolchain does not set CMAKE_SIZE; look for size next to the compiler.
if(NOT CMAKE_SIZE)
    get_filename_component(_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(CMAKE_SIZE NAMES arm-none-eabi-size HINTS "${_toolchain_bin}" NO_DEFAULT_PATH REQUIRED)
endif()

# ELF, BIN without picotool (UF2 needs picotool and is not required for SWD flashing)
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>/${TARGET}.bin
    VERBATIM
)
