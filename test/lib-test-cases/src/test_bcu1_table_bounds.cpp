/*
 * BCU1 table boundary regression tests
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include <protocol.h>

namespace
{
    struct TableFixture
    {
        BCU1 bcu;

        TableFixture()
        {
            // Padding exposes logical EEPROM overreads without crossing the host allocation.
            delete[] bcu.userEeprom->userEepromData;
            bcu.userEeprom->userEepromData = new uint8_t[1024]();
            uint8_t* eeprom = bcu.userEeprom->userEepromData;
            eeprom[0x16] = 2;
            eeprom[0x19] = 0;
            eeprom[0x1a] = 100;
            bcu.userEeprom->commsTabPtr() = 0x20;
            eeprom[0x20] = 1;
            eeprom[0x21] = 0xc0;
            eeprom[0x22] = 0xd0;
            eeprom[0x23] = COMCONF_COMM | COMCONF_WRITE;
            eeprom[0x24] = BIT_1;
        }
    };
}

TEST_CASE("BCU1 valid table entries remain accessible", "[table_bounds]")
{
    IAP_Init_Flash(0xff);
    TableFixture f;
    auto* objects = static_cast<ComObjectsBCU1*>(f.bcu.comObjects);
    CHECK(f.bcu.addrTables->indexOfAddr(100) == 1);
    REQUIRE(objects->objectConfigBCU1(0) != nullptr);
    CHECK(objects->objectConfig(0).config == (COMCONF_COMM | COMCONF_WRITE));
    CHECK(objects->objectValuePtr(0) != nullptr);
}

TEST_CASE("BCU1 oversized address count cannot find bytes beyond EEPROM", "[table_bounds]")
{
    IAP_Init_Flash(0xff);
    TableFixture f;
    uint8_t* eeprom = f.bcu.userEeprom->userEepromData;
    eeprom[0x16] = 0xff;
    eeprom[0x17 + 2 * 200] = 0x55;
    eeprom[0x18 + 2 * 200] = 0xaa;
    CHECK(f.bcu.addrTables->indexOfAddr(0x55aa) == -1);
}

TEST_CASE("BCU1 object indices stay within count and EEPROM", "[table_bounds]")
{
    IAP_Init_Flash(0xff);
    TableFixture f;
    auto* objects = static_cast<ComObjectsBCU1*>(f.bcu.comObjects);
    CHECK(objects->objectConfigBCU1(-1) == nullptr);
    CHECK(objects->objectConfigBCU1(1) == nullptr);
    CHECK(objects->objectConfigBCU1(255) == nullptr);
    f.bcu.userEeprom->commsTabPtr() = 0xfe;
    f.bcu.userEeprom->userEepromData[0xfe] = 1;
    CHECK(objects->objectConfigBCU1(0) == nullptr);
}

TEST_CASE("BCU1 absent object table returns disabled configuration", "[table_null]")
{
    IAP_Init_Flash(0xff);
    TableFixture f;
    f.bcu.userEeprom->commsTabPtr() = 0;
    auto* objects = static_cast<ComObjectsBCU1*>(f.bcu.comObjects);
    CHECK(objects->objectConfigBCU1(0) == nullptr);
    CHECK(objects->objectValuePtr(0) == nullptr);
    CHECK(objects->objectConfig(0).config == 0);
}
