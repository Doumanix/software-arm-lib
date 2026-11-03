/*
 * Tests for ComObjects::firstObjectAddr() on SYSTEMB (com_objectsSYSTEMB.h, addr_tablesSYSTEMB.h)
 *
 * SYSTEMB address table: 2 octets count (big endian), then the group addresses, no individual address.
 * SYSTEMB association table: 2 octets count, then entries of 4 octets (address table index, object number),
 * both big endian. The address table index of the first group address is 1, like in indexOfAddr().
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
    constexpr uint16_t AddrTableAddress = 0x3400;
    constexpr uint16_t AssocTableAddress = 0x3800;
    constexpr uint16_t ObjectTableAddress = 0x3c00;

    class ComObjectsProbe final : public ComObjectsSYSTEMB
    {
    public:
        explicit ComObjectsProbe(BcuDefault* bcuInstance) :
            ComObjectsSYSTEMB(bcuInstance)
        {}

        using ComObjectsSYSTEMB::firstObjectAddr;
        using ComObjectsSYSTEMB::objectCount;
    };

    class TestBcu final : public SYSTEMB
    {
    public:
        TestBcu() :
            SYSTEMB(new UserRamSYSTEMB(), new UserEepromSYSTEMB(), new ComObjectsProbe(this),
                    new AddrTablesSYSTEMB(this), new PropertiesSYSTEMB(this))
        {}
    };

    struct Fixture
    {
        TestBcu bcu;

        Fixture()
        {
            bcu.userEeprom->addrTabAddr() = AddrTableAddress;
            bcu.userEeprom->assocTabAddr() = AssocTableAddress;
            bcu.userEeprom->commsTabAddr() = ObjectTableAddress;
        }

        uint8_t* memory(const uint16_t address)
        {
            return &bcu.userEeprom->userEepromData[address - bcu.userEeprom->startAddr()];
        }

        void putWord(uint8_t* ptr, const int value)
        {
            ptr[0] = value >> 8;
            ptr[1] = value & 255;
        }

        void setAddressCount(const int count) { putWord(memory(AddrTableAddress), count); }

        // index 1 is the first group address
        void putAddress(const int index, const int value) { putWord(memory(AddrTableAddress) + index * 2, value); }

        void setAssocCount(const int count) { putWord(memory(AssocTableAddress), count); }

        void putAssoc(const int entry, const int addressIndex, const int objno)
        {
            uint8_t* ptr = memory(AssocTableAddress) + 2 + entry * 4;
            putWord(ptr, addressIndex);
            putWord(ptr + 2, objno);
        }

        void setObjectCount(const int count) { putWord(memory(ObjectTableAddress), count); }

        int objectCount() { return static_cast<ComObjectsProbe*>(bcu.comObjects)->objectCount(); }

        // one octet per object value, objects with write and communication enabled
        void setObjects(const int count)
        {
            setObjectCount(count);
            for (int i = 0; i < count; ++i)
            {
                memory(ObjectTableAddress)[2 + i * 2] = COMCONF_WRITE_COMM;
                memory(ObjectTableAddress)[3 + i * 2] = BYTE_1;
            }
        }

        void receiveWrite(const int addr, const int value)
        {
            uint8_t telegram[9] = {};
            telegram[8] = value;
            bcu.comObjects->processGroupTelegram(addr, APCI_GROUP_VALUE_WRITE_PDU, telegram);
        }

        int firstObjectAddr(const int objno)
        {
            return static_cast<ComObjectsProbe*>(bcu.comObjects)->firstObjectAddr(objno);
        }
    };
} // namespace

TEST_CASE("SYSTEMB first group address of a com object", "[com_objects_systemb]")
{
    IAP_Init_Flash(0xff);
    Fixture f;

    SECTION("tables are mapped into the UserEeprom")
    {
        CHECK(f.bcu.addrTables->addrTable() == f.memory(AddrTableAddress));
        CHECK(f.bcu.addrTables->assocTable() == f.memory(AssocTableAddress));
    }

    SECTION("less than 256 group addresses")
    {
        f.setAddressCount(3);
        f.putAddress(1, 0x0801);
        f.putAddress(2, 0x0802);
        f.putAddress(3, 0x0803);
        f.putAddress(4, 0xa55a); // behind the table
        f.setAssocCount(4);
        f.putAssoc(0, 2, 5);
        f.putAssoc(1, 1, 5);
        f.putAssoc(2, 3, 6);
        f.putAssoc(3, 4, 7); // association behind the address table

        CHECK(f.bcu.addrTables->addrCount() == 3);
        CHECK(f.firstObjectAddr(4) == 0x0802);
        CHECK(f.firstObjectAddr(5) == 0x0803);
        CHECK(f.firstObjectAddr(6) == 0);
        CHECK(f.firstObjectAddr(7) == 0);
    }

    SECTION("256 and more group addresses")
    {
        constexpr int n = 300;
        f.setAddressCount(n);
        for (int i = 1; i <= n; ++i)
        {
            f.putAddress(i, 0x2000 + i);
        }
        f.putAddress(n + 1, 0xa55a); // behind the table
        f.setAssocCount(4);
        f.putAssoc(0, 1, 2);
        f.putAssoc(1, 257, 0x0103);
        f.putAssoc(2, n, 9);
        f.putAssoc(3, n + 1, 10); // association behind the address table

        CHECK(f.bcu.addrTables->addrCount() == n);
        CHECK(f.firstObjectAddr(1) == 0x2001);
        CHECK(f.firstObjectAddr(0x0102) == 0x2000 + 257);
        CHECK(f.firstObjectAddr(8) == 0x2000 + n);
        CHECK(f.firstObjectAddr(9) == 0);
    }

    SECTION("ASAP starts at 1, the object number of the application at 0")
    {
        f.setAddressCount(2);
        f.putAddress(1, 0x0801);
        f.putAddress(2, 0x0802);
        f.setAssocCount(2);
        f.putAssoc(0, 1, 1);
        f.putAssoc(1, 2, 0); // ASAP 0 does not exist
        CHECK(f.firstObjectAddr(0) == 0x0801);
        CHECK(f.firstObjectAddr(1) == 0);
    }

    SECTION("address table index zero is no group address")
    {
        f.setAddressCount(1);
        f.putAddress(1, 0x0801);
        f.setAssocCount(1);
        f.putAssoc(0, 0, 5);
        CHECK(f.firstObjectAddr(4) == 0);
    }
}

TEST_CASE("SYSTEMB number of group objects has two octets", "[com_objects_systemb]")
{
    IAP_Init_Flash(0xff);
    Fixture f;

    SECTION("empty table")
    {
        CHECK(f.objectCount() == 0);
    }

    SECTION("less than 256 group objects")
    {
        f.setObjectCount(3);
        CHECK(f.objectCount() == 3);
    }

    SECTION("256 and more group objects")
    {
        f.setObjectCount(300);
        CHECK(f.objectCount() == 300);
    }
}

TEST_CASE("SYSTEMB group telegram is assigned to the object number ASAP minus one", "[com_objects_systemb]")
{
    IAP_Init_Flash(0xff);
    Fixture f;

    f.setAddressCount(3);
    f.putAddress(1, 0x0801);
    f.putAddress(2, 0x0802);
    f.putAddress(3, 0x0803);
    f.setObjects(3);
    f.setAssocCount(3);
    f.putAssoc(0, 1, 1); // ASAP 1 is object 0
    f.putAssoc(1, 2, 2);
    f.putAssoc(2, 3, 3); // ASAP 3 is the last object

    SECTION("first object")
    {
        f.receiveWrite(0x0801, 0x11);
        CHECK(f.bcu.comObjects->objectRead(0) == 0x11);
        CHECK(f.bcu.comObjects->objectRead(1) == 0);
        CHECK(f.bcu.comObjects->objectRead(2) == 0);
    }

    SECTION("last object")
    {
        f.receiveWrite(0x0803, 0x33);
        CHECK(f.bcu.comObjects->objectRead(0) == 0);
        CHECK(f.bcu.comObjects->objectRead(1) == 0);
        CHECK(f.bcu.comObjects->objectRead(2) == 0x33);
    }

    SECTION("ASAP behind the group object table is ignored")
    {
        f.putAssoc(2, 3, 4);
        f.receiveWrite(0x0803, 0x33);
        CHECK(f.bcu.comObjects->objectRead(0) == 0);
        CHECK(f.bcu.comObjects->objectRead(1) == 0);
        CHECK(f.bcu.comObjects->objectRead(2) == 0);
    }
}
