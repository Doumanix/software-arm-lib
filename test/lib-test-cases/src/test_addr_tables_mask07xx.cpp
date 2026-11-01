/*
 * Tests for the MASK0701 and MASK0705 address tables (addr_tablesMASK0701.h, addr_tablesMASK0705.h)
 *
 * Both use the BCU2 table format (count includes the individual address). Unlike
 * test_addr_tables_bcu2.cpp the table is reached through a complete BCU instance: the address of
 * the table is taken from the UserEeprom and mapped into the EEPROM range of the mask (0x3f00).
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
    constexpr uint16_t TableAddress = 0x4000; // usual place of the address table in BIM112 applications

    template<typename Bcu>
    struct TableFixture
    {
        Bcu bcu;

        TableFixture() { bcu.userEeprom->addrTabAddr() = TableAddress; }

        uint8_t* table() { return &bcu.userEeprom->userEepromData[TableAddress - bcu.userEeprom->startAddr()]; }

        void putAddress(const int index, const int value)
        {
            uint8_t* ptr = table() + 1 + index * 2;
            ptr[0] = value >> 8;
            ptr[1] = value & 255;
        }
    };
} // namespace

TEMPLATE_TEST_CASE("MASK07xx address table count includes the individual address", "[addr_tables_mask07xx]", MASK0701,
                   MASK0705)
{
    IAP_Init_Flash(0xff);
    TableFixture<TestType> f;

    SECTION("table is mapped into the UserEeprom")
    {
        CHECK(f.bcu.userEeprom->startAddr() == 0x3f00);
        CHECK(f.bcu.addrTables->addrTable() == f.table());
    }

    SECTION("count zero is empty")
    {
        f.table()[0] = 0;
        CHECK(f.bcu.addrTables->indexOfAddr(0x1401) == -1);
    }

    SECTION("count one holds only the individual address")
    {
        f.table()[0] = 1;
        f.putAddress(0, 0x1165);
        f.putAddress(1, 0x1401);
        CHECK(f.bcu.addrTables->indexOfAddr(0x1165) == -1);
        CHECK(f.bcu.addrTables->indexOfAddr(0x1401) == -1);
    }

    SECTION("entry behind the counted table is not found")
    {
        f.table()[0] = 2;
        f.putAddress(0, 0x1165);
        f.putAddress(1, 0x0064);
        f.putAddress(2, 0x1401);
        CHECK(f.bcu.addrTables->indexOfAddr(0x0064) == 1);
        CHECK(f.bcu.addrTables->indexOfAddr(0x1401) == -1);
    }

    SECTION("maximum count")
    {
        constexpr int n = 255;
        f.table()[0] = n;
        f.putAddress(0, 0x1165);
        for (int i = 1; i < n; ++i)
        {
            f.putAddress(i, 0x2000 + i);
        }
        f.putAddress(n, 0xa55a);
        CHECK(f.bcu.addrTables->indexOfAddr(0x2000 + n - 1) == n - 1);
        CHECK(f.bcu.addrTables->indexOfAddr(0xa55a) == -1);
    }

    SECTION("table address outside of the user memory finds nothing")
    {
        f.bcu.userEeprom->addrTabAddr() = 0x0100; // BCU2 EEPROM, not mapped by the mask
        CHECK(f.bcu.addrTables->addrTable() == nullptr);
        CHECK(f.bcu.addrTables->indexOfAddr(0x1401) == -1);
    }
}
