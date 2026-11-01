/*
 * Flash sector/page model with one-way programming and injected failures.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "flash_sim.h"
#include "pico_sim.h"
#include "hardware/flash.h"
#include <cstring>
#include <stdexcept>

alignas(4096) uint8_t FLASH[PICO_FLASH_SIZE_BYTES];
// Storage occupies the final two sectors of the simulated XIP array.
asm(".global __sblib_storage_start\n.set __sblib_storage_start, FLASH + 8192\n"
    ".global __sblib_storage_end\n.set __sblib_storage_end, FLASH + 16384\n");
namespace sim
{
    bool failErase = false;
    bool failProgram = false;
    unsigned eraseCalls = 0;
    unsigned programCalls = 0;
    void resetFlash()
    {
        memset(FLASH, 0xff, PICO_FLASH_SIZE_BYTES);
        failErase = false;
        failProgram = false;
        eraseCalls = 0;
        programCalls = 0;
    }
} // namespace sim
namespace
{
    void check(const uint32_t offset, const size_t count, const uint32_t alignment)
    {
        if (sim::interruptsEnabled() || count == 0 || offset % alignment != 0 || count % alignment != 0 ||
            offset >= PICO_FLASH_SIZE_BYTES || count > PICO_FLASH_SIZE_BYTES - offset)
        {
            throw std::logic_error("flash SDK contract violated");
        }
    }
} // namespace
void flash_range_erase(const uint32_t offset, const size_t count)
{
    check(offset, count, 4096);
    ++sim::eraseCalls;
    memset(FLASH + offset, 0xff, count);
    if (sim::failErase)
    {
        FLASH[offset] = 0;
    }
}
void flash_range_program(const uint32_t offset, const uint8_t* data, const size_t count)
{
    check(offset, count, 256);
    ++sim::programCalls;
    for (size_t i = 0; i < count; ++i)
    {
        FLASH[offset + i] &= data[i];
    }
    if (sim::failProgram)
    {
        FLASH[offset] ^= 1;
    }
}
