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
