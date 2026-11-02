cmake_minimum_required(VERSION 3.28)

# RP2350 build of the KNX-Tx stress test (Pico 2 test board, preset rp2350-pico2). The LPC1115
# build uses the MCUXpresso project (.cproject) and the defaults in src/app_main.cpp.
# On the test board the example runs on a real bus, so it uses the group telegram mode of
# src/app_main.cpp: own address 1.1.101, A_GroupValue_Write to 0/0/100 ... 0/0/108, at least
# TEST_DELAY_MS between two telegrams, limited duration, report lines on UART1 (TX GPIO8, 115200 baud).

set(SBLIB_MCU "RP2350" CACHE STRING "Target microcontroller")

# The storage of the sblib (UserEeprom) is placed at the end of the flash. A flash size of 2 MiB
# keeps it away from the storage of an application at the end of the 4 MiB flash of the board,
# so the example does not change the stored configuration of that application.
set(PICO_FLASH_SIZE_BYTES 2097152 CACHE STRING "Flash size used by the example")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rp2350/pico_sdk_setup.cmake)

project(example-txstresstest LANGUAGES C CXX ASM VERSION 1.0)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

# Same flash size for the code (iapFlashSize()) as for the linker script
add_compile_definitions(PICO_FLASH_SIZE_BYTES=${PICO_FLASH_SIZE_BYTES})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../sblib sblib)

set(TXSTRESS_DELAY_MS 60 CACHE STRING "Pause in milliseconds after each telegram (at least 50 on a real bus)")
set(TXSTRESS_DURATION_MS 30000 CACHE STRING "Duration of the test in milliseconds")
if(TXSTRESS_DELAY_MS LESS 50)
    message(FATAL_ERROR "TXSTRESS_DELAY_MS (${TXSTRESS_DELAY_MS}) must be at least 50")
endif()
if(TXSTRESS_DURATION_MS GREATER 300000)
    message(FATAL_ERROR "TXSTRESS_DURATION_MS (${TXSTRESS_DURATION_MS}) must not exceed 300000 (5 min)")
endif()

set(TARGET ${PROJECT_NAME})
add_executable(${TARGET}
    src/app_main.cpp
    src/rp2350/stdio_rp2350.cpp
)
target_link_libraries(${TARGET} PRIVATE pico_stdlib sblib_rp2350)
target_compile_definitions(${TARGET} PRIVATE
    KNX_PHYS_ADDRESS=0x1165
    TEST_DELAY_MS=${TXSTRESS_DELAY_MS}
    TEST_MAX_SEND_RETRIES=3
    TEST_STARTUP_DELAY_MS=5000
    TEST_GROUP_ADDRESS_FIRST=0x0064
    TEST_GROUP_ADDRESS_COUNT=9
    TEST_DURATION_MS=${TXSTRESS_DURATION_MS}
    TEST_REPORT_MS=5000
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

# ELF, BIN without picotool (UF2 needs picotool and is not required for SWD flashing)
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>/${TARGET}.bin
    VERBATIM
)
