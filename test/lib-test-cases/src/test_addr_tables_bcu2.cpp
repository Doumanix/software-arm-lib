/*
 * Tests for the BCU2 and SYSTEMB address tables (addr_tablesBCU2.h, addr_tablesSYSTEMB.h)
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <catch.hpp> // If possible, include catch.hpp as last header

#include <cstdint>
#include <sblib/eib/addr_tablesBCU2.h>
#include <sblib/eib/addr_tablesSYSTEMB.h>
#include <vector>

namespace
{
    // Backing beyond the counted table makes a read past it observable without UB
    class TestAddrTablesBCU2 final : public AddrTablesBCU2
    {
    public:
        TestAddrTablesBCU2() : AddrTablesBCU2(nullptr), bytes(2 + 2 * 255 + 2, 0) {}

        uint8_t* addrTable() override
        {
            return bytes.data();
        }

        std::vector<uint8_t> bytes;
    };

    class TestAddrTablesSYSTEMB final : public AddrTablesSYSTEMB
    {
    public:
        TestAddrTablesSYSTEMB() : AddrTablesSYSTEMB(nullptr), bytes(2 + 2 * 65535 + 2, 0) {}

        uint8_t* addrTable() override
        {
            return bytes.data();
        }

        std::vector<uint8_t> bytes;
    };

    void putAddress(uint8_t* ptr, const int index, const int value)
    {
        ptr[index * 2] = value >> 8;
        ptr[index * 2 + 1] = value & 255;
    }
}

TEST_CASE("BCU2 address table count includes the individual address", "[addr_tables_bcu2]")
{
    TestAddrTablesBCU2 table;

    SECTION("count zero is empty")
    {
        table.bytes[0] = 0;
        CHECK(table.indexOfAddr(0x1401) == -1);
    }

    SECTION("count one holds only the individual address")
    {
        table.bytes[0] = 1;
        putAddress(table.bytes.data() + 1, 0, 0x1165);
        putAddress(table.bytes.data() + 3, 0, 0x1401);
        CHECK(table.indexOfAddr(0x1165) == -1);
        CHECK(table.indexOfAddr(0x1401) == -1);
    }

    SECTION("entry behind the counted table is not found")
    {
        table.bytes[0] = 2;
        putAddress(table.bytes.data() + 1, 0, 0x1165);
        putAddress(table.bytes.data() + 3, 0, 0x0064);
        putAddress(table.bytes.data() + 5, 0, 0x1401);
        CHECK(table.indexOfAddr(0x0064) == 1);
        CHECK(table.indexOfAddr(0x1401) == -1);
    }

    SECTION("maximum count")
    {
        constexpr int n = 255;
        table.bytes[0] = n;
        putAddress(table.bytes.data() + 1, 0, 0x1165);
        for (int i = 1; i < n; ++i)
        {
            putAddress(table.bytes.data() + 1, i, 0x2000 + i);
        }
        putAddress(table.bytes.data() + 1, n, 0xa55a);
        CHECK(table.indexOfAddr(0x2000 + n - 1) == n - 1);
        CHECK(table.indexOfAddr(0xa55a) == -1);
    }
}

TEST_CASE("SYSTEMB address table count excludes the individual address", "[addr_tables_bcu2]")
{
    TestAddrTablesSYSTEMB table;

    SECTION("count zero is empty")
    {
        table.bytes[0] = 0;
        table.bytes[1] = 0;
        CHECK(table.indexOfAddr(0x1401) == -1);
    }

    SECTION("count one holds one group address")
    {
        table.bytes[0] = 0;
        table.bytes[1] = 1;
        putAddress(table.bytes.data() + 2, 0, 0x0064);
        putAddress(table.bytes.data() + 2, 1, 0x1401);
        CHECK(table.indexOfAddr(0x0064) == 1);
        CHECK(table.indexOfAddr(0x1401) == -1);
    }

    SECTION("maximum count")
    {
        constexpr int n = 65535;
        table.bytes[0] = n >> 8;
        table.bytes[1] = n & 255;
        for (int i = 0; i < n; ++i)
        {
            putAddress(table.bytes.data() + 2, i, 0x2000 + (i & 255));
        }
        putAddress(table.bytes.data() + 2, n, 0xa55a);
        CHECK(table.indexOfAddr(0x20ff) == 256);
        CHECK(table.indexOfAddr(0xa55a) == -1);
    }
}
