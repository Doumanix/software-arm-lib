/*
 * Tests for the table address and MCB setters of the SYSTEM B user EEPROM.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include <sblib/eib/userEepromBCU2.h>
#include <sblib/eib/userEepromSYSTEMB.h>
#include <iap_emu.h>
#include <cstdint>
#include <catch.hpp>

TEST_CASE("SYSTEM B writes the table addresses at its own offsets", "[eeprom-setters]")
{
    IAP_Init_Flash(0xff);
    UserEepromSYSTEMB eeprom;
    UserEeprom& base = eeprom;

    REQUIRE(static_cast<UserEepromBCU2&>(eeprom).setAddrTabAddr(0x3A00));
    REQUIRE(static_cast<UserEepromBCU2&>(eeprom).setAssocTabAddr(0x3B12));

    CHECK(base[0x3321] == 0x00);
    CHECK(base[0x3322] == 0x3A);
    CHECK(base[0x3323] == 0x12);
    CHECK(base[0x3324] == 0x3B);
    CHECK(eeprom.addrTabAddr() == 0x3A00);
    CHECK(eeprom.assocTabAddr() == 0x3B12);
    CHECK(eeprom.isModified());
    delete[] eeprom.userEepromData;
}

TEST_CASE("BCU2 writes the table addresses at the BCU2 offsets", "[eeprom-setters]")
{
    IAP_Init_Flash(0xff);
    UserEepromBCU2 eeprom;

    REQUIRE(eeprom.setAddrTabAddr(0x1234));
    REQUIRE(eeprom.setAssocTabAddr(0x5678));

    CHECK(eeprom[0x478] == 0x34);
    CHECK(eeprom[0x479] == 0x12);
    CHECK(eeprom[0x47A] == 0x78);
    CHECK(eeprom[0x47B] == 0x56);
    CHECK(eeprom.addrTabAddr() == 0x1234);
    CHECK(eeprom.assocTabAddr() == 0x5678);
    delete[] eeprom.userEepromData;
}

TEST_CASE("SYSTEM B writes size and CRC of the five MCBs", "[eeprom-setters]")
{
    // MCB start addresses 0x334D, 0x3355, 0x335D, 0x3365, 0x336D
    const uint32_t mcbStart[] = {0x334D, 0x3355, 0x335D, 0x3365, 0x336D};

    IAP_Init_Flash(0xff);
    UserEepromSYSTEMB eeprom;

    for (uint32_t idx = 0; idx < 5; idx++)
    {
        INFO("table " << idx);
        REQUIRE(eeprom.setMcbSize(idx, 0x01020304 + idx));
        REQUIRE(eeprom.setMcbCrc(idx, 0xA500 + idx));

        CHECK(eeprom[mcbStart[idx] + 0] == 0x00); // untouched
        CHECK(eeprom[mcbStart[idx] + 1] == 0x00);
        CHECK(eeprom[mcbStart[idx] + 2] == 0x01);
        CHECK(eeprom[mcbStart[idx] + 3] == 0x02);
        CHECK(eeprom[mcbStart[idx] + 4] == 0x03);
        CHECK(eeprom[mcbStart[idx] + 5] == 0x04 + idx);
        CHECK(eeprom[mcbStart[idx] + 6] == 0xA5);
        CHECK(eeprom[mcbStart[idx] + 7] == idx);
    }

    CHECK(eeprom.addrTabMcb()[5] == 0x04);
    CHECK(eeprom.commsSeg0Mcb()[5] == 0x08);
    CHECK_FALSE(eeprom.setMcbSize(5, 1));
    CHECK_FALSE(eeprom.setMcbCrc(5, 1));
    delete[] eeprom.userEepromData;
}
