/*
 * Tests for the read and write API of Memory (UserEeprom and UserRam)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "test_user_eeprom.h"

#include <cstdint>
#include <cstring>
#include <iap_emu.h>
#include <sblib/eib/userRamBCU1.h>

#include <catch.hpp> // If possible, include catch.hpp as last header


static constexpr uint32_t eepromStart = 0x100;
static constexpr uint32_t eepromSize = 1024;

/** Fills the memory with the pattern value = (index + 1) & 0xff through the array operator */
static void fillPattern(Memory& memory)
{
    for (uint32_t i = 0; i < memory.size(); i++)
    {
        memory[memory.startAddr() + i] = static_cast<uint8_t>(i + 1);
    }
}

static void checkReadApi(Memory& memory)
{
    const uint32_t first = memory.startAddr();
    const uint32_t last = memory.endAddr();

    SECTION("getUInt32 is big endian")
    {
        memory[first] = 0x12;
        memory[first + 1] = 0x34;
        memory[first + 2] = 0x56;
        memory[first + 3] = 0x78;
        REQUIRE(memory.getUInt32(first) == 0x12345678);
        REQUIRE(memory.getUInt16(first) == 0x1234);
        REQUIRE(memory.getUInt16(first + 2) == 0x5678);

        memory[first + 4] = 0xff;
        memory[first + 5] = 0x00;
        memory[first + 6] = 0x80;
        memory[first + 7] = 0x01;
        REQUIRE(memory.getUInt32(first + 4) == 0xff008001);
        REQUIRE(memory.getUInt32(first + 1) == 0x345678ff);
    }

    SECTION("getUInt32 limits")
    {
        fillPattern(memory);
        REQUIRE(memory.getUInt32(last - 3) == memory.getUInt32(first + memory.size() - 4));
        REQUIRE(memory.getUInt32(last - 3) != 0);
        REQUIRE(memory.getUInt32(last - 2) == 0);
        REQUIRE(memory.getUInt32(last) == 0);
        REQUIRE(memory.getUInt32(last + 1) == 0);
        REQUIRE(memory.getUInt32(UINT32_MAX) == 0);
        if (first > 0)
        {
            REQUIRE(memory.getUInt32(first - 1) == 0);
        }
    }

    SECTION("read copies a range")
    {
        fillPattern(memory);
        uint8_t buffer[16];
        memset(buffer, 0xee, sizeof(buffer));
        REQUIRE(memory.read(first + 2, buffer, 4));
        REQUIRE(buffer[0] == 3);
        REQUIRE(buffer[3] == 6);
        REQUIRE(buffer[4] == 0xee);

        REQUIRE(memory.read(first, buffer, 1));
        REQUIRE(buffer[0] == 1);
    }

    SECTION("read limits")
    {
        fillPattern(memory);
        uint8_t buffer[16];
        memset(buffer, 0xee, sizeof(buffer));
        REQUIRE(memory.read(last - 15, buffer, 16));
        REQUIRE(buffer[15] == static_cast<uint8_t>(memory.size()));
        REQUIRE(memory.read(last, buffer, 1));
        REQUIRE(buffer[0] == static_cast<uint8_t>(memory.size()));

        memset(buffer, 0xee, sizeof(buffer));
        REQUIRE_FALSE(memory.read(last - 14, buffer, 16));
        REQUIRE_FALSE(memory.read(last, buffer, 2));
        REQUIRE_FALSE(memory.read(last + 1, buffer, 1));
        REQUIRE_FALSE(memory.read(first, buffer, memory.size() + 1));
        REQUIRE_FALSE(memory.read(first, buffer, UINT32_MAX));
        REQUIRE_FALSE(memory.read(UINT32_MAX, buffer, 2));
        if (first > 0)
        {
            REQUIRE_FALSE(memory.read(first - 1, buffer, 1));
        }
        for (const uint8_t value : buffer)
        {
            REQUIRE(value == 0xee); // nothing copied on failure
        }
    }

    SECTION("read of the whole memory and of zero bytes")
    {
        fillPattern(memory);
        uint8_t* all = new uint8_t[memory.size()];
        REQUIRE(memory.read(first, all, memory.size()));
        for (uint32_t i = 0; i < memory.size(); i++)
        {
            REQUIRE(all[i] == static_cast<uint8_t>(i + 1));
        }
        delete[] all;

        uint8_t dummy = 0x55;
        REQUIRE(memory.read(first, &dummy, 0));
        REQUIRE(dummy == 0x55);
    }
}

TEST_CASE("Memory read API of UserEeprom", "[memory_api]")
{
    IAP_Init_Flash(0xff);
    TestUserEeprom userEeprom(eepromStart, eepromSize, eepromSize);
    checkReadApi(userEeprom);
}

TEST_CASE("Memory read API of UserRam", "[memory_api]")
{
    UserRamBCU1 userRam;
    checkReadApi(userRam);
}

/** Checks the write API, isModified tells if the memory has to be written to flash (UserEeprom) */
template <typename IsModified, typename ClearModified>
static void checkWriteApi(Memory& memory, IsModified isModified, ClearModified clearModified)
{
    const uint32_t first = memory.startAddr();
    const uint32_t last = memory.endAddr();
    fillPattern(memory);
    clearModified();

    SECTION("setUInt8 / setUInt16 / setUInt32 are big endian")
    {
        REQUIRE(memory.setUInt8(first, 0xa5));
        REQUIRE(memory[first] == 0xa5);
        REQUIRE(memory.getUInt8(first) == 0xa5);

        REQUIRE(memory.setUInt16(first + 1, 0x1234));
        REQUIRE(memory[first + 1] == 0x12);
        REQUIRE(memory[first + 2] == 0x34);
        REQUIRE(memory.getUInt16(first + 1) == 0x1234);

        REQUIRE(memory.setUInt32(first + 3, 0x89abcdef));
        REQUIRE(memory[first + 3] == 0x89);
        REQUIRE(memory[first + 4] == 0xab);
        REQUIRE(memory[first + 5] == 0xcd);
        REQUIRE(memory[first + 6] == 0xef);
        REQUIRE(memory.getUInt32(first + 3) == 0x89abcdef);
        REQUIRE(memory[first + 7] == 8); // neighbour unchanged
    }

    SECTION("write and fill")
    {
        const uint8_t source[] = {0xde, 0xad, 0xbe, 0xef, 0x01};
        REQUIRE(memory.write(first + 10, source, sizeof(source)));
        uint8_t buffer[sizeof(source)] = {};
        REQUIRE(memory.read(first + 10, buffer, sizeof(buffer)));
        REQUIRE(memcmp(buffer, source, sizeof(source)) == 0);
        REQUIRE(memory[first + 9] == 10);
        REQUIRE(memory[first + 15] == 16);

        REQUIRE(memory.fill(first + 20, 8, 0x77));
        for (uint32_t i = 20; i < 28; i++)
        {
            REQUIRE(memory[first + i] == 0x77);
        }
        REQUIRE(memory[first + 19] == 20);
        REQUIRE(memory[first + 28] == 29);
    }

    SECTION("write at the upper limit")
    {
        const uint8_t source[16] = {};
        REQUIRE(memory.write(last - 15, source, 16));
        REQUIRE(memory.fill(last, 1, 0x42));
        REQUIRE(memory[last] == 0x42);
        REQUIRE(memory.setUInt32(last - 3, 0x01020304));
        REQUIRE(memory.getUInt32(last - 3) == 0x01020304);
        REQUIRE(memory.setUInt16(last - 1, 0x0506));
        REQUIRE(memory.setUInt8(last, 0x07));
        REQUIRE(memory.getUInt16(last - 1) == 0x0507);
    }

    SECTION("write outside the range changes nothing")
    {
        const uint8_t source[16] = {};
        REQUIRE_FALSE(memory.setUInt8(last + 1, 1));
        REQUIRE_FALSE(memory.setUInt16(last, 1));
        REQUIRE_FALSE(memory.setUInt32(last - 2, 1));
        REQUIRE_FALSE(memory.setUInt8(UINT32_MAX, 1));
        REQUIRE_FALSE(memory.write(last - 14, source, 16));
        REQUIRE_FALSE(memory.write(first, source, memory.size() + 1));
        REQUIRE_FALSE(memory.write(UINT32_MAX, source, 2));
        REQUIRE_FALSE(memory.fill(last, 2, 0));
        REQUIRE_FALSE(memory.fill(first, UINT32_MAX, 0));
        if (first > 0)
        {
            REQUIRE_FALSE(memory.setUInt8(first - 1, 1));
            REQUIRE_FALSE(memory.fill(first - 1, 2, 0));
        }
        REQUIRE_FALSE(isModified());
        for (uint32_t i = 0; i < memory.size(); i++)
        {
            REQUIRE(memory[first + i] == static_cast<uint8_t>(i + 1));
        }
    }

    SECTION("empty ranges change nothing")
    {
        const uint8_t source = 0xff;
        REQUIRE(memory.write(first, &source, 0));
        REQUIRE(memory.fill(first, 0, 0xff));
        REQUIRE_FALSE(isModified());
        REQUIRE(memory[first] == 1);
    }
}

TEST_CASE("Memory write API of UserEeprom", "[memory_api]")
{
    IAP_Init_Flash(0xff);
    TestUserEeprom userEeprom(eepromStart, eepromSize, eepromSize);
    auto isModified = [&userEeprom]() { return userEeprom.isModified(); };
    auto clearModified = [&userEeprom]() { userEeprom.modified(false); };

    checkWriteApi(userEeprom, isModified, clearModified);
}

TEST_CASE("Memory write API of UserRam", "[memory_api]")
{
    UserRamBCU1 userRam;
    auto isModified = []() { return false; };
    auto clearModified = []() {};

    checkWriteApi(userRam, isModified, clearModified);
}

TEST_CASE("UserEeprom marks modified only if a byte changes", "[memory_api]")
{
    IAP_Init_Flash(0xff);
    TestUserEeprom userEeprom(eepromStart, eepromSize, eepromSize);
    const uint32_t first = userEeprom.startAddr();
    fillPattern(userEeprom); // bytes 1, 2, 3, ...
    userEeprom.modified(false);

    const uint8_t same[] = {3, 4, 5};
    REQUIRE(userEeprom.setUInt8(first, 1));
    REQUIRE(userEeprom.setUInt16(first, 0x0102));
    REQUIRE(userEeprom.setUInt32(first, 0x01020304));
    REQUIRE(userEeprom.write(first + 2, same, sizeof(same)));
    REQUIRE(userEeprom.fill(first + 4, 0, 0));
    REQUIRE_FALSE(userEeprom.isModified());

    SECTION("setUInt8")
    {
        REQUIRE(userEeprom.setUInt8(first, 2));
        REQUIRE(userEeprom.isModified());
    }
    SECTION("setUInt16 changing the low byte")
    {
        REQUIRE(userEeprom.setUInt16(first, 0x0103));
        REQUIRE(userEeprom.isModified());
    }
    SECTION("setUInt32 changing the last byte")
    {
        REQUIRE(userEeprom.setUInt32(first, 0x01020305));
        REQUIRE(userEeprom.isModified());
    }
    SECTION("write changing one byte")
    {
        const uint8_t different[] = {3, 4, 6};
        REQUIRE(userEeprom.write(first + 2, different, sizeof(different)));
        REQUIRE(userEeprom.isModified());
    }
    SECTION("fill changing one byte")
    {
        userEeprom[first + 4] = 9;
        userEeprom[first + 5] = 9;
        userEeprom.modified(false);
        REQUIRE(userEeprom.fill(first + 4, 3, 9)); // the third byte differs
        REQUIRE(userEeprom.isModified());
    }
    SECTION("fill with the current content")
    {
        userEeprom[first + 4] = 9;
        userEeprom[first + 5] = 9;
        userEeprom.modified(false);
        REQUIRE(userEeprom.fill(first + 4, 2, 9));
        REQUIRE_FALSE(userEeprom.isModified());
    }
}

TEST_CASE("UserEeprom keeps the write delay of 50 ms", "[memory_api]")
{
    IAP_Init_Flash(0xff);
    TestUserEeprom userEeprom(eepromStart, eepromSize, eepromSize);
    userEeprom.modified(false);

    REQUIRE(userEeprom.setUInt8(userEeprom.startAddr(), 0x5a));
    REQUIRE(userEeprom.isModified());
    REQUIRE_FALSE(userEeprom.writeDelayElapsed());
}
