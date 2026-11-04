/******************************************************************************
 * @addtogroup SBLIB_MAIN_GROUP Main Group Description
 * @defgroup SBLIB_SUB_GROUP_1 Sub Group 1 Description
 * @ingroup SBLIB_MAIN_GROUP
 * @brief   
 * @details 
 *
 *
 * @{
 *
 * @file   memory.cpp
 * @author Darthyson <darth@maptrack.de>
 * @bug No known bugs.
 ******************************************************************************/

/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 3 as
 published by the Free Software Foundation.
 ---------------------------------------------------------------------------*/

#include <sblib/eib/memory.h>
#include <sblib/bits.h>
#include <cstring>

Memory::Memory(const uint32_t start, const uint32_t size):
    startAddress(start),
    endAddress(start + size - 1),
    sizeTotal(size) {}

uint32_t Memory::startAddr() const
{
    return (startAddress);
}

uint32_t Memory::endAddr() const
{
    return (endAddress);
}

uint32_t Memory::size() const
{
    return (sizeTotal);
}

bool Memory::rangeValid(const uint32_t address, const uint32_t count) const
{
    if (count == 0)
    {
        return (true);
    }
    if (address < startAddress)
    {
        return (false);
    }
    const uint32_t offset = address - startAddress;
    return ((offset < sizeTotal) && (count <= (sizeTotal - offset)));
}

uint8_t Memory::getUInt8(uint32_t address) const
{
    normalizeAddress(&address);
    if (address > (size() - 1))
    {
        return 0;
    }
    return data[address];
}

uint16_t Memory::getUInt16(uint32_t address) const
{
    normalizeAddress(&address);
    if ((address + 1) > (size() - 1))
    {
        return 0;
    }
    return makeWord(data[address], data[address + 1]);
}

uint32_t Memory::getUInt32(uint32_t address) const
{
    if (!rangeValid(address, 4))
    {
        return 0;
    }
    normalizeAddress(&address);
    uint32_t value = makeWord(data[address], data[address + 1]);
    value <<= 16;
    value |= makeWord(data[address + 2], data[address + 3]);
    return value;
}

bool Memory::read(uint32_t address, uint8_t* dst, const uint32_t count) const
{
    if (!rangeValid(address, count))
    {
        return false;
    }
    normalizeAddress(&address);
    memcpy(dst, data + address, count);
    return true;
}

bool Memory::setUInt8(const uint32_t address, const uint8_t value)
{
    return write(address, &value, 1);
}

bool Memory::setUInt16(const uint32_t address, const uint16_t value)
{
    const uint8_t bytes[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return write(address, bytes, sizeof(bytes));
}

bool Memory::setUInt32(const uint32_t address, const uint32_t value)
{
    const uint8_t bytes[4] = {static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                              static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return write(address, bytes, sizeof(bytes));
}

bool Memory::write(uint32_t address, const uint8_t* src, const uint32_t count)
{
    if (!rangeValid(address, count))
    {
        return false;
    }
    normalizeAddress(&address);
    if (changedHook == nullptr)
    {
        memcpy(data + address, src, count);
    }
    else if (memcmp(data + address, src, count) != 0)
    {
        memcpy(data + address, src, count);
        changedHook(this);
    }
    return true;
}

bool Memory::fill(uint32_t address, const uint32_t count, const uint8_t value)
{
    if (!rangeValid(address, count))
    {
        return false;
    }
    normalizeAddress(&address);
    if (changedHook == nullptr)
    {
        memset(data + address, value, count);
        return true;
    }
    bool changed = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (data[address + i] != value)
        {
            data[address + i] = value;
            changed = true;
        }
    }
    if (changed)
    {
        changedHook(this);
    }
    return true;
}

bool Memory::inRange(const uint32_t address) const
{
    return ((address >= startAddr()) && (address <= endAddr()));
}

bool Memory::inRange(const uint32_t start, const uint32_t end) const
{
    return ((start <= end) &&
            (start >= startAddr()) &&
            (end <= endAddr()));
}


/** @}*/
