/*
 * Test class for the memory.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#ifndef TEST_SBLIB_KNX_MEMORY_H_
#define TEST_SBLIB_KNX_MEMORY_H_

#include <sblib/eib/memory.h>


class TestMemory final : public Memory
{
public:
    TestMemory() = delete;
    TestMemory(const uint32_t start, const uint32_t size) :
        Memory(start, size)
    {
        data = dummyData;
    }

    //using Memory::startAddr;
    //using Memory::endAddr;
    //using Memory::size;
    using Memory::normalizeAddress;
    //using Memory::inRange;

    // Implement dummy abstract methods, the access functions of Memory work on dummyData,
    // [[maybe_unused]] is used to avoid warnings about unused parameters, as these methods are not actually tested.
    uint8_t& operator[]([[maybe_unused]] const uint32_t address) override { return dummyByte; }
    uint8_t& operator[]([[maybe_unused]] const uint32_t address) const override { return dummyByte; }

private:
    mutable uint8_t dummyByte = 0;
    uint8_t dummyData[4] = {};
};

#endif /* TEST_SBLIB_KNX_MEMORY_H_ */
