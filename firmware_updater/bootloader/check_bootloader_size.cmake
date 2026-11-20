# Fails if the Release image of the bootloader does not end before the application descriptor block.
# The descriptor block uses the flash page in front of the application (0x3000 - 0x100 = 0x2F00,
# bootDescriptorBlockAddress() in src/boot_descriptor_block.cpp). Writing the descriptor erases this page,
# so bootloader code there would be lost. Debug images are larger and only used without an application.
#
# Usage: cmake -DIMAGE=<image.bin> -DCONFIG=<build configuration> -P check_bootloader_size.cmake

if(NOT CONFIG STREQUAL "Release")
    return()
endif()
if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "Image not found: ${IMAGE}")
endif()

set(_maxSize 12032) # 0x2F00
file(SIZE "${IMAGE}" _imageSize)
if(_imageSize GREATER _maxSize)
    message(FATAL_ERROR "${IMAGE}: ${_imageSize} bytes, the bootloader must end before 0x2F00 (${_maxSize} bytes)")
endif()
