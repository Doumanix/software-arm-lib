cmake_minimum_required(VERSION 3.28)

# Host unit tests of the RP2350 port. The RP2350 sources of the sblib are compiled
# for the host against a simulation of the Pico SDK functions they use (sim/pico_sim.*).
# Usage: cmake --workflow --preset host-test

project(rp2350-host-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CATCH_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Catch" CACHE PATH "Path to the Catch project")
set(SBLIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../sblib" CACHE PATH "Path to the sblib project")
# Original PIO instruction encoders of the Pico SDK (hardware/pio_instructions.h); the rest of hardware/pio.h is simulated
set(SBLIB_PICO_SDK_HEADERS_ONLY ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/rp2350/pico_sdk_import.cmake")
unset(SBLIB_PICO_SDK_HEADERS_ONLY)
set(PICO_SDK_PIO_INCLUDE "${PICO_SDK_PATH}/src/rp2_common/hardware_pio/include"
    CACHE PATH "Path to the include directory of hardware_pio of the Pico SDK")

enable_testing()

add_executable(rp2350_host_tests
    src/main.cpp
    src/test_timer.cpp
    sim/pico_sim.cpp
)
target_include_directories(rp2350_host_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sim  # hardware/*.h of the Pico SDK (simulation)
    ${SBLIB_PATH}/inc
    ${SBLIB_PATH}/src                # rp2350/timer.cpp is included by the test
    ${CATCH_PATH}/inc
)
target_compile_definitions(rp2350_host_tests PRIVATE
    # Platform RP2350 without the __arm__ check of sblib/platform_detect.h
    SBLIB_PLATFORM_DETECT_H_
    SBLIB_PLATFORM_RP2350=1
    SBLIB_BOARD_RP2350_PICO2_TESTBOARD
    SBLIB_RP2350_TIMER_STATS         # diagnostics counters of the emulation (short capture gaps)
)
target_compile_options(rp2350_host_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_test(NAME rp2350_host_tests COMMAND rp2350_host_tests)

# White-box bus invariants without changing the production class definition.
set_source_files_properties(src/test_bus.cpp PROPERTIES COMPILE_OPTIONS -fno-access-control)

# KNX bus (src/eib/bus.cpp, unchanged) on top of the timer emulation and a simulated bus line
add_executable(rp2350_bus_tests
    src/main.cpp
    src/test_bus.cpp
    src/test_bus_t9.cpp
    sim/pico_sim.cpp
    ${SBLIB_PATH}/src/eib/bus.cpp
    ${SBLIB_PATH}/src/eib/knx_lpdu.cpp
    ${SBLIB_PATH}/src/eib/addr_tables.cpp
)
target_include_directories(rp2350_bus_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sim
    ${SBLIB_PATH}/inc
    ${SBLIB_PATH}/src
    ${CATCH_PATH}/inc
)
target_compile_definitions(rp2350_bus_tests PRIVATE
    SBLIB_PLATFORM_DETECT_H_
    SBLIB_PLATFORM_RP2350=1
    SBLIB_BOARD_RP2350_PICO2_TESTBOARD
)
target_compile_options(rp2350_bus_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_test(NAME rp2350_bus_tests COMMAND rp2350_bus_tests)

# The same tests with the KNX bus by PIO (CMake option SBLIB_RP2350_PIO_BUS of the sblib)
foreach(name rp2350_host_tests rp2350_bus_tests)
    get_target_property(_sources ${name} SOURCES)
    get_target_property(_includes ${name} INCLUDE_DIRECTORIES)
    get_target_property(_definitions ${name} COMPILE_DEFINITIONS)
    add_executable(${name}_pio ${_sources})
    target_include_directories(${name}_pio PRIVATE ${_includes} ${PICO_SDK_PIO_INCLUDE})
    target_compile_definitions(${name}_pio PRIVATE ${_definitions} SBLIB_RP2350_PIO_BUS)
    target_compile_options(${name}_pio PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME ${name}_pio COMMAND ${name}_pio)
endforeach()

# Flash and pin modules compile unchanged against the SDK simulation.
foreach(module iap pin)
    add_executable(rp2350_${module}_tests src/main.cpp src/test_${module}.cpp sim/pico_sim.cpp)
    target_include_directories(rp2350_${module}_tests PRIVATE sim ${SBLIB_PATH}/inc ${SBLIB_PATH}/src ${CATCH_PATH}/inc)
    target_compile_definitions(rp2350_${module}_tests PRIVATE SBLIB_PLATFORM_DETECT_H_ SBLIB_PLATFORM_RP2350=1
        SBLIB_BOARD_RP2350_PICO2_TESTBOARD)
    target_compile_options(rp2350_${module}_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(module STREQUAL "iap")
        target_sources(rp2350_${module}_tests PRIVATE sim/flash_sim.cpp)
        target_compile_definitions(rp2350_${module}_tests PRIVATE IAP_EMULATION)
    endif()
    add_test(NAME rp2350_${module}_tests COMMAND rp2350_${module}_tests)
endforeach()
