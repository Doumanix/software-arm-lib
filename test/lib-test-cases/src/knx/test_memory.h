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
        Memory(start, size) {}

    //using Memory::startAddr;
    //using Memory::endAddr;
    //using Memory::size;
    using Memory::normalizeAddress;
    //using Memory::inRange;

    // Implement dummy abstract methods,
    // [[maybe_unused]] is used to avoid warnings about unused parameters, as these methods are not actually tested.
    uint8_t& operator[]([[maybe_unused]] const uint32_t address) override { return dummyByte; }
    uint8_t& operator[]([[maybe_unused]] const uint32_t address) const override { return dummyByte; }

    [[nodiscard]] uint8_t getUInt8([[maybe_unused]] uint32_t address) const override { return 0;}
    [[nodiscard]] uint16_t getUInt16([[maybe_unused]] uint32_t address) const override { return 0;}
    [[nodiscard]] uint32_t getUInt32([[maybe_unused]] uint32_t address) const override { return 0;}
    [[nodiscard]] bool read([[maybe_unused]] uint32_t address, [[maybe_unused]] uint8_t* dst, [[maybe_unused]] uint32_t count) const override { return false;}

    bool setUInt8([[maybe_unused]] uint32_t address, [[maybe_unused]] uint8_t value) override { return false;}
    bool setUInt16([[maybe_unused]] uint32_t address, [[maybe_unused]] uint16_t value) override { return false;}
    bool setUInt32([[maybe_unused]] uint32_t address, [[maybe_unused]] uint32_t value) override { return false;}
    bool write([[maybe_unused]] uint32_t address, [[maybe_unused]] const uint8_t* src, [[maybe_unused]] uint32_t count) override { return false;}
    bool fill([[maybe_unused]] uint32_t address, [[maybe_unused]] uint32_t count, [[maybe_unused]] uint8_t value) override { return false;}

private:
    mutable uint8_t dummyByte = 0;
};

#endif /* TEST_SBLIB_KNX_MEMORY_H_ */
