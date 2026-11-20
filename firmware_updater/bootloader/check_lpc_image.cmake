# Checks a flash image of the LPC111x:
# - The sum of the first eight 32 bit words (little endian, modulo 2^32) must be 0. The boot ROM calculates the
#   checksum word 7 of the vector table this way and starts the image only if it is valid (UM10398, chapter
#   "Criterion for valid user code"). The linker script fills it in, but only if the handlers NMI_Handler and
#   HardFault_Handler stay global; otherwise the image builds and does not start.
# - For an image starting at 0x0 the word at address 0x2FC must not be one of the code read protection values of
#   the LPC111x boot ROM (UM10398, chapter "Code read protection"). The linker script does not reserve this word,
#   so any code change can place such a value there. CRP3 disables SWD and ISP permanently.
#
# Usage: cmake -DIMAGE=<image.bin> [-DFLASH_START=<address>] -P check_lpc_image.cmake
#   FLASH_START: flash address of the image, decimal or 0x<hex>, default 0x0. Behind the bootloader the word at
#                0x2FC of the file is ordinary code, therefore the CRP check is skipped, the vector table check
#                is not. Any other value (also empty) is an error.

if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "Image not found: ${IMAGE}")
endif()
if(NOT DEFINED FLASH_START)
    set(FLASH_START 0)
endif()
if(NOT "${FLASH_START}" MATCHES "^(0[xX][0-9a-fA-F]+|[0-9]+)$")
    message(FATAL_ERROR "Invalid FLASH_START '${FLASH_START}', expected a decimal or 0x<hex> address")
endif()
file(SIZE "${IMAGE}" _imageSize)
if(_imageSize LESS 32)
    message(FATAL_ERROR "${IMAGE}: image has ${_imageSize} bytes, the vector table (32 bytes) is incomplete")
endif()

# Sum of the first eight words, each word from its bytes in file order (little endian)
file(READ "${IMAGE}" _vectors OFFSET 0 LIMIT 32 HEX)
set(_sum 0)
foreach(_i RANGE 0 7)
    math(EXPR _pos "${_i} * 8")
    string(SUBSTRING "${_vectors}" ${_pos} 8 _hex)
    string(SUBSTRING "${_hex}" 6 2 _b3)
    string(SUBSTRING "${_hex}" 4 2 _b2)
    string(SUBSTRING "${_hex}" 2 2 _b1)
    string(SUBSTRING "${_hex}" 0 2 _b0)
    math(EXPR _sum "(${_sum} + 0x${_b3}${_b2}${_b1}${_b0}) & 0xFFFFFFFF")
endforeach()
if(NOT _sum EQUAL 0)
    math(EXPR _sum "${_sum}" OUTPUT_FORMAT HEXADECIMAL)
    message(FATAL_ERROR
        "${IMAGE}: the sum of the first eight words (vector table checksum) is ${_sum}, must be 0x0. "
        "The boot ROM would not start the image. "
        "Check that NMI_Handler and HardFault_Handler are kept (--undefined)")
endif()

if(NOT FLASH_START EQUAL 0 OR _imageSize LESS 768)
    return()
endif()

# Bytes at 0x2FC..0x2FF in file order (little endian)
file(READ "${IMAGE}" _crpWord OFFSET 764 LIMIT 4 HEX)
set(_crpValues
    "78563412" # CRP1   0x12345678
    "21436587" # CRP2   0x87654321
    "65872143" # CRP3   0x43218765
    "7073694e" # NO_ISP 0x4E697370
)
if(_crpWord IN_LIST _crpValues)
    message(FATAL_ERROR "${IMAGE}: the word at 0x2FC (bytes ${_crpWord}) is a code read protection value")
endif()
