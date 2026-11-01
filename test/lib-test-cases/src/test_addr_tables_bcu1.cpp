/*
 * Tests for the BCU1 address table (addr_tablesBCU1.h)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <catch.hpp> // If possible, include catch.hpp as last header

#include <cstdint>
#include <iap_emu.h>
#include <sblib/eib/addr_tablesBCU1.h>
#include <sblib/eib/userEepromBCU1.h>

namespace
{
    struct AddressFixture
    {
        UserEepromBCU1 eeprom;
        AddrTablesBCU1 table;

        AddressFixture() : table(&eeprom)
        {
            // Backing beyond the real 256 byte EEPROM makes a read past the table observable without UB
            delete[] eeprom.userEepromData;
            eeprom.userEepromData = new uint8_t[0x240]();
            eeprom.addrTab()[0] = 0x11;
            eeprom.addrTab()[1] = 0x65;
        }

        ~AddressFixture()
        {
            delete[] eeprom.userEepromData;
        }

        void address(const int index, const int value)
        {
            uint8_t* p = eeprom.addrTab() + 2 * index;
            p[0] = value >> 8;
            p[1] = value & 255;
        }
    };
}

TEST_CASE("BCU1 address table count includes the individual address", "[addr_tables_bcu1]")
{
    IAP_Init_Flash(0xff);
    AddressFixture f;

    SECTION("count zero is empty")
    {
        f.eeprom.addrTabSize() = 0;
        f.address(1, 0x1401);
        CHECK(f.table.indexOfAddr(0x1165) == -1);
        CHECK(f.table.indexOfAddr(0x1401) == -1);
    }

    SECTION("count one holds only the individual address")
    {
        f.eeprom.addrTabSize() = 1;
        f.address(1, 0x1401);
        CHECK(f.table.indexOfAddr(0x1165) == -1);
        CHECK(f.table.indexOfAddr(0x1401) == -1);
    }

    SECTION("entry behind the counted table is not found")
    {
        f.eeprom.addrTabSize() = 2;
        f.address(1, 0x0064);
        f.address(2, 0x1401);
        CHECK(f.table.indexOfAddr(0x0064) == 1);
        CHECK(f.table.indexOfAddr(0x1401) == -1);
        CHECK(f.table.indexOfAddr(0x1165) == -1);
        CHECK(f.table.indexOfAddr(0x2222) == -1);
    }

    SECTION("maximum count of the 256 byte BCU1 EEPROM")
    {
        // The count is at offset 0x16, the addresses start at 0x17. 116 entries end at 0xfe, 0xff is the checksum.
        constexpr int n = 116;
        f.eeprom.addrTabSize() = n;
        for (int i = 1; i < n; ++i)
        {
            f.address(i, 0x2000 + i);
        }
        f.address(n, 0xa55a); // checksum bytes, never a table member
        CHECK(f.table.indexOfAddr(0x2001) == 1);
        CHECK(f.table.indexOfAddr(0x2000 + n - 1) == n - 1);
        CHECK(f.table.indexOfAddr(0xa55a) == -1);
        CHECK(f.table.indexOfAddr(0x3333) == -1);
    }

    SECTION("maximum encoded count in synthetic extended backing")
    {
        // 255 is not a valid BCU1 EEPROM layout, this only checks the loop bound
        constexpr int n = 255;
        f.eeprom.addrTabSize() = n;
        for (int i = 1; i < n; ++i)
        {
            f.address(i, 0x3000 + i);
        }
        f.address(n, 0xb66b);
        CHECK(f.table.indexOfAddr(0x3000 + n - 1) == n - 1);
        CHECK(f.table.indexOfAddr(0xb66b) == -1);
    }
}
