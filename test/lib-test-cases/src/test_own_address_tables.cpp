/*
 * Tests for the individual address while the address table is loading (BCU2::setOwnAddress(), BCU2::begin())
 *
 * BCU2 and MASK07xx keep the individual address behind the 1 octet count of the address table.
 * SYSTEMB has a 2 octet count and group addresses only, so its address table must stay untouched.
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
    constexpr uint16_t AddrTableAddressBCU2 = 0x0300;    // inside the BCU2 UserEeprom 0x0100..0x04ff
    constexpr uint16_t AddrTableAddressSYSTEMB = 0x3400; // inside the SYSTEMB UserEeprom

    template<class Bcu, uint16_t AddrTableAddress>
    struct Fixture
    {
        Bcu bcu;

        Fixture()
        {
            bcu.userEeprom->addrTabAddr() = AddrTableAddress;
            bcu.userEeprom->loadState()[OT_ADDR_TABLE] = LS_LOADING;
        }

        uint8_t* table() { return bcu.addrTables->addrTable(); }

        void fillTable()
        {
            const uint8_t content[] = {0x00, 0x02, 0x08, 0x01, 0x08, 0x02};
            for (unsigned int i = 0; i < sizeof(content); ++i)
            {
                table()[i] = content[i];
            }
        }
    };
} // namespace

TEST_CASE("BCU2 address table count beyond the user memory finds nothing", "[own_address_tables]")
{
    IAP_Init_Flash(0xff);
    BCU2 bcu;
    constexpr uint16_t LastAddress = 0x04f0; // 16 bytes before the end of the UserEeprom 0x0100..0x04ff
    bcu.userEeprom->addrTabAddr() = LastAddress;
    uint8_t* table = bcu.addrTables->addrTable();
    REQUIRE(table != nullptr);
    const uint8_t content[] = {0x03, 0x11, 0x65, 0x00, 0x64, 0x00, 0x65};
    for (unsigned int i = 0; i < sizeof(content); ++i)
    {
        table[i] = content[i];
    }

    SECTION("count inside the memory")
    {
        CHECK(bcu.addrTables->indexOfAddr(0x0064) == 1);
        CHECK(bcu.addrTables->indexOfAddr(0x0065) == 2);
    }

    SECTION("count whose entries end behind the memory")
    {
        table[0] = 0xff;
        CHECK(bcu.addrTables->indexOfAddr(0x0064) == -1);
        CHECK(bcu.addrTables->indexOfAddr(0x0065) == -1);
    }
}

TEST_CASE("BCU2 stores the individual address in the loading address table", "[own_address_tables]")
{
    IAP_Init_Flash(0xff);
    Fixture<BCU2, AddrTableAddressBCU2> f;
    f.fillTable();

    SECTION("setOwnAddress writes behind the count")
    {
        f.bcu.setOwnAddress(0x1101);

        CHECK(f.table()[0] == 0x00);
        CHECK(f.table()[1] == 0x11);
        CHECK(f.table()[2] == 0x01);
        CHECK(f.table()[3] == 0x01);
        CHECK(f.bcu.ownAddress() == 0x1101);
    }

    SECTION("begin keeps the individual address in the table")
    {
        f.bcu.setOwnAddress(0x1101);
        f.bcu.begin(0x0004, 0x0001, 0x01);

        CHECK(f.bcu.ownAddress() == 0x1101);
        CHECK(f.table()[1] == 0x11);
        CHECK(f.table()[2] == 0x01);
    }
}

TEST_CASE("SYSTEMB keeps the loading address table when the individual address is set", "[own_address_tables]")
{
    IAP_Init_Flash(0xff);
    Fixture<SYSTEMB, AddrTableAddressSYSTEMB> f;
    f.fillTable();

    SECTION("setOwnAddress leaves the table unchanged")
    {
        f.bcu.setOwnAddress(0x1101);

        const uint8_t expected[] = {0x00, 0x02, 0x08, 0x01, 0x08, 0x02};
        for (unsigned int i = 0; i < sizeof(expected); ++i)
        {
            CHECK(f.table()[i] == expected[i]);
        }
        CHECK(f.bcu.addrTables->addrCount() == 2);
        CHECK(f.bcu.ownAddress() == 0x1101);
    }

    SECTION("begin does not take a group address as individual address")
    {
        f.bcu.setOwnAddress(0x1101);
        f.bcu.begin(0x0004, 0x0001, 0x01);

        CHECK(f.bcu.ownAddress() == 0x1101);
        CHECK(f.bcu.addrTables->addrCount() == 2);
        CHECK(f.table()[3] == 0x01);
    }
}
