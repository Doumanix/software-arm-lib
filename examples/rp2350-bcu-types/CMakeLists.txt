cmake_minimum_required(VERSION 3.28)

# Link check of the BCU types BCU2, MASK0701, MASK0705 and SYSTEMB on the RP2350:
# one minimal firmware per type (instance, begin(), sblib main loop), see src/app_main.cpp.
# Targets: rp2350-bcu-bcu2, rp2350-bcu-mask0701, rp2350-bcu-mask0705, rp2350-bcu-systemb.

set(SBLIB_MCU "RP2350" CACHE STRING "Target microcontroller")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rp2350/pico_sdk_setup.cmake)

project(rp2350-bcu-types LANGUAGES C CXX ASM VERSION 1.0)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../sblib sblib)

# The Pico SDK toolchain does not set CMAKE_SIZE; look for size next to the compiler.
if(NOT CMAKE_SIZE)
    get_filename_component(_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(CMAKE_SIZE NAMES arm-none-eabi-size HINTS "${_toolchain_bin}" NO_DEFAULT_PATH REQUIRED)
endif()

foreach(_type BCU2 MASK0701 MASK0705 SYSTEMB)
    string(TOLOWER ${_type} _name)
    set(_target rp2350-bcu-${_name})
    add_executable(${_target} src/app_main.cpp)
    target_link_libraries(${_target} PRIVATE pico_stdlib sblib_rp2350)
    target_compile_definitions(${_target} PRIVATE BCU_TYPE_${_type})
    pico_enable_stdio_uart(${_target} 0)
    pico_enable_stdio_usb(${_target} 0)
    # ELF, BIN without picotool (UF2 needs picotool and is not required for SWD flashing)
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${_target}>
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${_target}> $<TARGET_FILE_DIR:${_target}>/${_target}.bin
        VERBATIM
    )
endforeach()
