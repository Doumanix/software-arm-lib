cmake_minimum_required(VERSION 3.28)

# Target MCU. LPC1115 is the default and behaves exactly as before.
set(SBLIB_MCU "LPC1115" CACHE STRING "Target microcontroller (LPC1115, RP2350)")
set_property(CACHE SBLIB_MCU PROPERTY STRINGS LPC1115 RP2350)
if(NOT SBLIB_MCU MATCHES "^(LPC1115|RP2350)$")
    message(FATAL_ERROR "Unknown SBLIB_MCU '${SBLIB_MCU}'. Valid values: LPC1115, RP2350")
endif()

# RP2350: the Pico SDK must be imported before the first project() call.
# If sblib is built as part of an application, the application does this
# (include cmake/rp2350/pico_sdk_setup.cmake before its project()).
if(SBLIB_MCU STREQUAL "RP2350" AND NOT DEFINED PROJECT_NAME)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/rp2350/pico_sdk_setup.cmake)
endif()

project(sblib LANGUAGES CXX VERSION 2.10)

if(SBLIB_MCU STREQUAL "RP2350")
    enable_language(C ASM)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        pico_sdk_init()
    elseif(NOT COMMAND pico_sdk_init)
        message(FATAL_ERROR "SBLIB_MCU=RP2350: include cmake/rp2350/pico_sdk_setup.cmake before project() of the application")
    endif()
    # Board (pin mapping, see inc/sblib/io_pin_names.h). The default is set in pico_sdk_setup.cmake.
    if(SBLIB_BOARD STREQUAL "pico2_testboard")
        set(SBLIB_BOARD_DEFINITION SBLIB_BOARD_RP2350_PICO2_TESTBOARD)
    elseif(SBLIB_BOARD STREQUAL "custom")
        if(NOT SBLIB_BOARD_HEADER)
            message(FATAL_ERROR "SBLIB_BOARD=custom requires SBLIB_BOARD_HEADER (board header file name, found via the include path)")
        endif()
        set(SBLIB_BOARD_DEFINITION "SBLIB_BOARD_HEADER=\"${SBLIB_BOARD_HEADER}\"")
    else()
        message(FATAL_ERROR "Unknown SBLIB_BOARD '${SBLIB_BOARD}'. Valid values: pico2_testboard, custom")
    endif()
    message(NOTICE "Configuring ${PROJECT_NAME} for RP2350 (SBLIB_BOARD=${SBLIB_BOARD}, PICO_PLATFORM=${PICO_PLATFORM}, PICO_BOARD=${PICO_BOARD}).")
    # The RP2350 platform sources of the sblib, see SBLIB_RP2350_SRC in sblib.cmake.
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(sblib.cmake)
    add_library(sblib_rp2350 STATIC ${SBLIB_RP2350_SRC})
    target_include_directories(sblib_rp2350 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
    option(SBLIB_RP2350_TIMER_STATS "Diagnostics: counters of the RP2350 timer emulation (timerEmulationStats())" OFF)
    option(SBLIB_RP2350_PIO_BUS "KNX bus RX capture and TX pulses by PIO state machines (OFF: GPIO interrupt and alarm)" ON)
    target_compile_definitions(sblib_rp2350 PUBLIC
        ${SBLIB_BOARD_DEFINITION}
        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:Debug>:DEBUG>
        $<$<BOOL:${SBLIB_RP2350_TIMER_STATS}>:SBLIB_RP2350_TIMER_STATS>
        $<$<BOOL:${SBLIB_RP2350_PIO_BUS}>:SBLIB_RP2350_PIO_BUS>
    )
    target_link_libraries(sblib_rp2350 PUBLIC
        hardware_clocks
        hardware_flash
        hardware_sync
        hardware_gpio
        hardware_irq
        hardware_timer
        hardware_watchdog
        pico_rand
        pico_unique_id
    )
    if(SBLIB_RP2350_PIO_BUS)
        target_link_libraries(sblib_rp2350 PUBLIC hardware_dma hardware_pio)
    endif()

    return()
endif()

set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "Enable verbose output from the build system")
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Creates compile_commands.json file for easier debugging and IDE support

set(SBLPC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/CMSIS_CORE_LPC11xx CACHE PATH "Path to the 'CMSIS_CORE_LPC11xx' directory")
set(SBLIB_CPU_EMULATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../test/sblib/cpu-emu CACHE PATH "Path to the sblib 'cpu-emu' directory")
set(SBLIB_LOGGING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../logging CACHE PATH "UNTESTED! Path to the sblib 'logging' source directory")

# Toolchain file is set in CMakePresets.json or on command line
if(CMAKE_CROSSCOMPILING AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Cross-compiling but CMAKE_TOOLCHAIN_FILE not set. Set in CMakePresets.json or configure with -DCMAKE_TOOLCHAIN_FILE=<path to>/arm-none-eabi_mcuxpresso.cmake")
endif()
if(CMAKE_CROSSCOMPILING AND DEFINED CMAKE_TOOLCHAIN_FILE AND NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "Toolchain file does not exist: ${CMAKE_TOOLCHAIN_FILE}")
endif()
if (NOT EXISTS "${SBLPC_DIR}")
    message(FATAL_ERROR "CMSIS_CORE_LPC11xx project not found. Check setting of SBLPC_DIR (${SBLPC_DIR})")
endif()
if (NOT EXISTS "${SBLIB_CPU_EMULATION_DIR}")
    message(FATAL_ERROR "cpu-emu directory not found. Check setting of SBLIB_CPU_EMULATION_DIR (${SBLIB_CPU_EMULATION_DIR})")
endif()

# Create options
set(SBLIB_ARCHITECTURE "arm" CACHE STRING "Architecture to build for (arm, x86, x64).")
set_property(CACHE SBLIB_ARCHITECTURE PROPERTY STRINGS arm x86 x64)
option(SBLIB_DEBUG_BUS "Enable dumping of state machine interrupt data e.g timer values." OFF)
option(SBLIB_DEBUG_BUS_BITLEVEL "Extension used with DEBUG_BUS to dump interrupt of each bit -
                                 Use with care due to easy overflow of the trace buffer." OFF)
option(SBLIB_INCLUDE_SERIAL "sblib should open serial port." OFF)
set(SBLIB_SERIAL_SPEED "OFF" CACHE STRING "Serial port speed for debugging (OFF, 57600, 115200, 230400, 576000, 1500000).")
set_property(CACHE SBLIB_SERIAL_SPEED PROPERTY STRINGS OFF 57600 115200 230400 576000 1500000)
option(SBLIB_DUMP_TELEGRAMS "Dump rx and tx telegrams, incl. received ack and timing info over serial interface." OFF)
set(SBLIB_PIO_FOR_TEL_END_IND "OFF" CACHE STRING "Toggles defined PIOx_y pin at the end of a received telegram.")
option(SBLIB_DUMP_COM_OBJ "Dump object handling information on app-server level over serial interface." OFF)
option(SBLIB_DUMP_MEM_OPS "Dump APCI_MEMORY_WRITE_PDU and APCI_MEMORY_READ_PDU over serial interface." OFF)
option(SBLIB_DUMP_PROPERTIES "Dump interface object and property handling over serial interface." OFF)
option(SBLIB_DUMP_TL4 "Dump transport layer 4 protocol handling over serial interface." OFF)
set(SBLIB_SERIAL_RX_PIN "OFF" CACHE STRING "Serial receive pin to use (PIOx_y).")
set(SBLIB_SERIAL_TX_PIN "OFF" CACHE STRING "Serial transmit pin to use (PIOx_y).")
option(SBLIB_ROUTER "Enable to build a ROUTER." OFF)
option(SBLIB_BUSMONITOR "Enables bus monitoring: dump of all bus traffic incl. timing info (no sending, no processing of rx-telegrams)." OFF)
option(SBLIB_LOGGING "UNTESTED! Whether to log through the serial port" OFF)

# sblib source code
include(sblib.cmake) # list of all sblib source code files (*.h, *.cpp, ...)

# Targets
set(TARGET_ARM ${PROJECT_NAME}_arm)
set(TARGET_X86 ${PROJECT_NAME}_x86)
set(TARGET_X64 ${PROJECT_NAME}_x64)
add_library(${TARGET_ARM} STATIC ${SBLIB_SRC}) # ARM sblib
add_library(${TARGET_X86} STATIC ${SBLIB_SRC}) # 32bit sblib
add_library(${TARGET_X64} STATIC ${SBLIB_SRC}) # 64bit sblib

# common includes
foreach(target ${TARGET_ARM} ${TARGET_X86} ${TARGET_X64})
    # sblib
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
endforeach()

# ARM includes
foreach(target ${TARGET_ARM})
    message(NOTICE "CMSIS directory ${SBLPC_DIR}")
    target_include_directories(${target} PUBLIC ${SBLPC_DIR}/inc) # CMSIS_CORE_LPC11xx/inc
endforeach()

# x86, x64 includes
foreach(target ${TARGET_X86} ${TARGET_X64})
    target_include_directories(${target} PUBLIC ${SBLIB_CPU_EMULATION_DIR}) # ../test/sblib/cpu-emu
endforeach()

# common definitions
add_compile_definitions(
    __LPC11XX__
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Debug>:DEBUG>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_DEBUG_BUS}>>:DEBUG_BUS>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_DEBUG_BUS_BITLEVEL}>>:DEBUG_BUS_BITLEVEL>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_INCLUDE_SERIAL}>>:INCLUDE_SERIAL>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_DUMP_TELEGRAMS}>>:DUMP_TELEGRAMS>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_DUMP_COM_OBJ}>>:DUMP_COM_OBJ>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_DUMP_MEM_OPS}>>:DUMP_MEM_OPS>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_DUMP_PROPERTIES}>>:DUMP_PROPERTIES>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_DUMP_TL4}>>:DUMP_TL4>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_ROUTER}>>:ROUTER>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${SBLIB_BUSMONITOR}>>:BUSMONITOR>
)

# Conditionally add common compile definitions which have values e.g., SERIAL_SPEED=115200
if(DEFINED SBLIB_PIO_FOR_TEL_END_IND AND NOT SBLIB_PIO_FOR_TEL_END_IND STREQUAL "" AND NOT SBLIB_PIO_FOR_TEL_END_IND STREQUAL "OFF")
    add_compile_definitions($<$<CONFIG:Debug>:PIO_FOR_TEL_END_IND=${SBLIB_PIO_FOR_TEL_END_IND}>)
endif()
if(DEFINED SBLIB_SERIAL_SPEED AND NOT SBLIB_SERIAL_SPEED STREQUAL "" AND NOT SBLIB_SERIAL_SPEED STREQUAL "OFF")
    add_compile_definitions($<$<CONFIG:Debug>:SERIAL_SPEED=${SBLIB_SERIAL_SPEED}>)
endif()
if(DEFINED SBLIB_SERIAL_RX_PIN AND NOT SBLIB_SERIAL_RX_PIN STREQUAL "" AND NOT SBLIB_SERIAL_RX_PIN STREQUAL "OFF")
    add_compile_definitions($<$<CONFIG:Debug>:SERIAL_RX_PIN=${SBLIB_SERIAL_RX_PIN}>)
endif()
if(DEFINED SBLIB_SERIAL_TX_PIN AND NOT SBLIB_SERIAL_TX_PIN STREQUAL "" AND NOT SBLIB_SERIAL_TX_PIN STREQUAL "OFF")
    add_compile_definitions($<$<CONFIG:Debug>:SERIAL_TX_PIN=${SBLIB_SERIAL_TX_PIN}>)
endif()
if(DEFINED SBLIB_LOGGING AND NOT SBLIB_LOGGING STREQUAL "" AND NOT SBLIB_LOGGING STREQUAL "OFF")
    if (NOT EXISTS "${SBLIB_LOGGING_DIR}")
        message(FATAL_ERROR "logging directory not found. Check setting of SBLIB_LOGGING_DIR (${SBLIB_LOGGING_DIR})")
    endif()
    set(SBLIB_INCLUDE_LOGGING ON)
endif()

# ARM specific compile definitions
target_compile_definitions(${TARGET_ARM} PUBLIC
    __USE_CMSIS=CMSIS_CORE_LPC11xx
    __NEWLIB__
    CORE_M0
)

# x86, x64 specific compile definitions
foreach(target ${TARGET_X86} ${TARGET_X64})
    target_compile_definitions(${target} PUBLIC IAP_EMULATION)
endforeach()

# x86, x64 specific compile options
foreach(target ${TARGET_X86} ${TARGET_X64})
    target_compile_options(${target} PRIVATE
        -Wall -Wlogical-op -Woverloaded-virtual -Wextra -pedantic
        # TODO warnings to enable in future
        # -Wconversion
        -fmessage-length=0
        -g3
        $<$<CONFIG:Debug>: -O0>
        $<$<CONFIG:Release>: -Os>
    )
endforeach()

# Logging source
if(DEFINED SBLIB_INCLUDE_LOGGING)
    # todo This is untested
    message(NOTICE "Logging source directory: ${SBLIB_LOGGING_DIR}")
    include(${SBLIB_LOGGING_DIR}/logging.cmake)
    foreach(target ${TARGET_ARM} ${TARGET_X86} ${TARGET_X64})
        foreach(srcFileName ${SBLIB_LOGGING_SRC})
            target_sources(${target} PUBLIC "${SBLIB_LOGGING_DIR}/${srcFileName}")
        endforeach()
        target_include_directories(${target} PUBLIC ${SBLIB_LOGGING_DIR}/inc)
    endforeach()
endif()

# Check if cross-compiling for ARM target
if(CMAKE_CROSSCOMPILING)
    message(NOTICE "Configuring ${PROJECT_NAME} for ARM architecture.")
# Check if building for 32bit architecture
elseif(SBLIB_ARCHITECTURE STREQUAL "x86")
    message(NOTICE "Configuring ${PROJECT_NAME} for x86 architecture.")
    target_compile_options(${TARGET_X86} PRIVATE -m32)
# Check if building for 64bit architecture
elseif(SBLIB_ARCHITECTURE STREQUAL "x64")
    message(NOTICE "Configuring ${PROJECT_NAME} for x64 architecture.")
    target_compile_options(${TARGET_X64} PRIVATE -m64)
else()
    message(FATAL_ERROR "Unknown architecture ${SBLIB_ARCHITECTURE}. (project ${PROJECT_NAME})")
endif()