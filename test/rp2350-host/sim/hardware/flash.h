/*
 * Minimal flash SDK interface for host tests.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#ifndef SBLIB_TEST_RP2350_FLASH_H_
#define SBLIB_TEST_RP2350_FLASH_H_
#include <cstddef>
#include <cstdint>
#define PICO_FLASH_SIZE_BYTES 16384u
void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t* data, size_t count);
#endif
