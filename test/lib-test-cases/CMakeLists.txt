cmake_minimum_required(VERSION 3.28)

project(lib-test-cases LANGUAGES CXX)

set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "Enable verbose output from the build system")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Creates compile_commands.json file for easier debugging and IDE support

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CATCH_PATH "${CMAKE_SOURCE_DIR}/../../Catch" CACHE PATH "Path to the Catch project")
set(SBLIB_PATH "${CMAKE_SOURCE_DIR}/../../sblib" CACHE PATH "Path to the sblib project")
set(SBLIB_TEST_PATH "${CMAKE_SOURCE_DIR}/../sblib" CACHE PATH "Path to the sblib-test project")

if (NOT EXISTS "${CATCH_PATH}")
    message(FATAL_ERROR "Catch project not found. Check setting of CATCH_PATH (${CATCH_PATH})")
endif()

if (NOT EXISTS "${SBLIB_PATH}")
    message(FATAL_ERROR "sblib project not found. Check setting of SBLIB_PATH (${SBLIB_PATH})")
endif()

if (NOT EXISTS "${SBLIB_TEST_PATH}")
    message(FATAL_ERROR "sblib-test project not found. Check setting of SBLIB_TEST_PATH (${SBLIB_TEST_PATH})")
endif()

# Enable testing
include(CTest)

include(${CATCH_PATH}/catch.cmake)                # add catch.hpp
include(${CMAKE_SOURCE_DIR}/lib-test-cases.cmake) # add all test cases

set(SBLIB_TEST_CASE_SRC
    ${SBLIB_CATCH_SRC}
    ${SBLIB_LIB_TEST_CASES_SRC}
)

# Create targets
set(TARGET_X86 ${PROJECT_NAME}_x86)
set(TARGET_X64 ${PROJECT_NAME}_x64)
add_executable(${TARGET_X86} ${SBLIB_TEST_CASE_SRC})
add_executable(${TARGET_X64} ${SBLIB_TEST_CASE_SRC})

# Add include directories for targets
foreach(target ${TARGET_X86} ${TARGET_X64})
    target_include_directories(${target} PUBLIC
        ${CATCH_PATH}/inc          # catch
        ${SBLIB_PATH}/inc          # sblib
        ${SBLIB_TEST_PATH}/inc     # sblib-test
        ${SBLIB_TEST_PATH}/cpu-emu # sblib-test cpu emulation
    )
endforeach()

# definitions for all targets
add_compile_definitions(
    IAP_EMULATION
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

# Host tests of the RP2350 MemMapper code path: wide allocation-table entries plus a larger
# simulated flash. Set by the x64_rp preset; reaches the sblib subdirectory like IAP_EMULATION.
if(SBLIB_MEMMAPPER_LARGE_INDICES)
    add_compile_definitions(MEMMAPPER_LARGE_INDICES)
endif()

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

foreach(target ${TARGET_X86} ${TARGET_X64})
    target_compile_options(${target} PRIVATE
        -Wall -Wlogical-op -Woverloaded-virtual -Wextra -pedantic
        # todo warnings to enable in future
        # -Wconversion
        -fmessage-length=0
        -g3
        $<$<CONFIG:Debug>: -O0>
        $<$<CONFIG:Release>: -Os>
    )
endforeach()

foreach(target ${TARGET_X86} ${TARGET_X64})
    target_link_options(${target} PRIVATE
        #-fuse-ld=lld
#        $<$<CONFIG:Release>:-flto>
#        $<$<CONFIG:Release>:-ffat-lto-objects>
        $<$<CONFIG:Release>:-Os>
        #$<$<CONFIG:Debug>: >
    )
endforeach()

# Check if building for 32bit architecture
if(SBLIB_ARCHITECTURE STREQUAL "x86")
    message(NOTICE "Configuring ${PROJECT_NAME} for x86 architecture.")
    add_subdirectory(${SBLIB_TEST_PATH} sblib-test_x86)
    target_compile_options(${TARGET_X86} PUBLIC
        -m32
    )
    target_link_options(${TARGET_X86} PRIVATE
        -m32
    )
    target_link_libraries(${TARGET_X86} sblib-test_x86)

    # Register test to run with JUnit XML output
    add_test(NAME ${TARGET_X86}_junit
        COMMAND ${TARGET_X86} --reporter junit --out $<TARGET_FILE_DIR:${TARGET_X86}>/${TARGET_X86}_result.xml
        WORKING_DIRECTORY $<TARGET_FILE_DIR:${TARGET_X86}>
    )

# Check if building for 64bit architecture
elseif(SBLIB_ARCHITECTURE STREQUAL "x64")
    message(NOTICE "Configuring ${PROJECT_NAME} for x64 architecture.")
#    target_include_directories(${TARGET_X64} PRIVATE ${INCLUDE_DIRECTORIES})
    add_subdirectory(${SBLIB_TEST_PATH} sblib-test_x64)
    target_compile_options(${TARGET_X64} PRIVATE
        -m64
    )
    target_link_options(${TARGET_X64} PRIVATE 
        -m64
    )
    target_link_libraries(${TARGET_X64} sblib-test_x64)

    # Register test to run with JUnit XML output
    add_test(NAME ${TARGET_X64}_junit
        COMMAND ${TARGET_X64} --reporter junit --out $<TARGET_FILE_DIR:${TARGET_X64}>/${TARGET_X64}_result.xml
        WORKING_DIRECTORY $<TARGET_FILE_DIR:${TARGET_X64}>
    )
else()
    message(FATAL_ERROR "Unknown architecture ${SBLIB_ARCHITECTURE}. (project ${PROJECT_NAME})")
endif()