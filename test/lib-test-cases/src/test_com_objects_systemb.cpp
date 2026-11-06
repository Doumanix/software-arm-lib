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
#include <cstring>
#include <vector>

namespace
{
    constexpr uint16_t AddrTableAddress = 0x3400;
    constexpr uint16_t AssocTableAddress = 0x3800;
    constexpr uint16_t ObjectTableAddress = 0x3900;

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

        // one octet per object value, objects with transmit, write and communication enabled
        void setObjects(const int count)
        {
            setObjectCount(count);
            for (int i = 0; i < count; ++i)
            {
                memory(ObjectTableAddress)[2 + i * 2] = COMCONF_TRANS | COMCONF_WRITE_COMM;
                memory(ObjectTableAddress)[3 + i * 2] = BYTE_1;
            }
        }

        // SYSTEM B object type, the value size follows from it (ComObjectsSYSTEMB::objectSize())
        void setObjectType(const int objno, const uint8_t type) { memory(ObjectTableAddress)[3 + objno * 2] = type; }

        void receiveWrite(const int addr, const int value)
        {
            receiveWrite(addr, std::vector<uint8_t>{static_cast<uint8_t>(value)});
        }

        /**
         * @brief Let the group objects receive an A_GroupValue_Write with value octets.
         * @param addr       Group address
         * @param data       Value octets after the APCI octet
         * @param apduLength APDU length in the frame, -1 for the length of the data plus the APCI octet.
         *                   The frame is an extended frame above 15 octets, it always holds all data octets.
         */
        void receiveWrite(const int addr, const std::vector<uint8_t>& data, int apduLength = -1)
        {
            if (apduLength < 0)
            {
                apduLength = static_cast<int>(data.size()) + 1;
            }
            std::vector<uint8_t> telegram;
            if (apduLength > 15)
            {
                telegram = {0x3C, 0xE0, 0x11, 0x02, 0x00, 0x00, static_cast<uint8_t>(apduLength)};
            }
            else
            {
                // standard frame control byte, otherwise telegramTpciIndex() treats the telegram as an extended frame
                telegram = {0xBC, 0x11, 0x02, 0x00, 0x00, static_cast<uint8_t>(0xE0 | apduLength)};
            }
            telegram.push_back(0x00); // TPCI T_Data_Group
            telegram.push_back(0x80); // APCI A_GroupValue_Write
            telegram.insert(telegram.end(), data.begin(), data.end());
            bcu.comObjects->processGroupTelegram(addr, APCI_GROUP_VALUE_WRITE_PDU, telegram.data());
        }

        int firstObjectAddr(const int objno)
        {
            return static_cast<ComObjectsProbe*>(bcu.comObjects)->firstObjectAddr(objno);
        }
    };

    struct LongObject
    {
        uint8_t type; // SYSTEM B object type
        int size;     // value size in octets
    };

    // values above 14 octets need an APDU above 15 octets, that is an extended frame
    constexpr LongObject LongObjects[] = {{21, 15}, {30, 24}};
    constexpr int LongObjectCount = sizeof(LongObjects) / sizeof(LongObjects[0]);

    // object n has the group address 0x0801 + n
    void setLongObjects(Fixture& f)
    {
        f.setAddressCount(LongObjectCount);
        f.setObjects(LongObjectCount);
        f.setAssocCount(LongObjectCount);
        for (int i = 0; i < LongObjectCount; ++i)
        {
            f.putAddress(i + 1, 0x0801 + i);
            f.setObjectType(i, LongObjects[i].type);
            f.putAssoc(i, i + 1, i + 1);
        }
    }

    std::vector<uint8_t> longValue(const int objno)
    {
        std::vector<uint8_t> value(LongObjects[objno].size);
        for (size_t i = 0; i < value.size(); ++i)
        {
            value[i] = static_cast<uint8_t>(0x40 * objno + i + 1);
        }
        return value;
    }
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

TEST_CASE("SYSTEMB RAM flags of the group objects", "[com_objects_systemb]")
{
    IAP_Init_Flash(0xff);
    Fixture f;

    f.setAddressCount(2);
    f.putAddress(1, 0x0801);
    f.putAddress(2, 0x0802);
    f.setObjects(3);
    f.setAssocCount(2);
    f.putAssoc(0, 1, 1);
    f.putAssoc(1, 2, 3);

    const uint8_t* flags = f.bcu.comObjects->objectFlagsTable();
    uint8_t* valueEnd = f.bcu.comObjects->objectValuePtr(3);

    SECTION("the flags follow the values and start cleared")
    {
        REQUIRE(flags != nullptr);
        CHECK(flags == valueEnd);
        CHECK(flags[0] == 0);
        CHECK(flags[1] == 0);
    }

    SECTION("objectWrite sets the transmit request and the first group address is sent")
    {
        f.bcu.comObjects->objectWrite(0, 0x5a);
        CHECK((flags[0] & 0x0f) == COMFLAG_TRANSREQ);
        CHECK((flags[0] & 0xf0) == 0);

        CHECK(f.bcu.comObjects->sendNextGroupTelegram());
        CHECK(f.bcu.bus->sendCurTelegram[3] == 0x08);
        CHECK(f.bcu.bus->sendCurTelegram[4] == 0x01);
        CHECK((flags[0] & COMFLAG_TRANS_MASK) == COMFLAG_ERROR);
    }

    SECTION("the odd object uses the high nibble")
    {
        f.bcu.comObjects->objectWrite(2, 0x5a);
        CHECK(flags[0] == 0);
        CHECK((flags[1] & 0x0f) == COMFLAG_TRANSREQ);
    }

    SECTION("a received group telegram sets the update flag")
    {
        f.receiveWrite(0x0802, 0x77);
        CHECK(f.bcu.comObjects->objectRead(2) == 0x77);
        CHECK(f.bcu.comObjects->nextUpdatedObject() == 2);
        CHECK(f.bcu.comObjects->nextUpdatedObject() == INVALID_OBJECT_NUMBER);
    }

    SECTION("too many group objects for the user RAM have no flags")
    {
        f.setObjects(520); // 520 values and 260 flag octets do not fit into 770 octets
        CHECK(f.bcu.comObjects->objectFlagsTable() == nullptr);
        CHECK(f.bcu.comObjects->nextUpdatedObject() == INVALID_OBJECT_NUMBER);
        f.bcu.comObjects->objectWrite(0, 1);
        CHECK(f.bcu.comObjects->objectRead(0) == 1);
    }
}

TEST_CASE("SYSTEMB group object values above 14 octets are sent only in an extended frame", "[com_objects_systemb]")
{
    for (int objno = 0; objno < LongObjectCount; ++objno)
    {
        const int size = LongObjects[objno].size;
        INFO("objno " << objno << ", " << size << " octets");
        IAP_Init_Flash(0xff);
        Fixture f;
        setLongObjects(f);
        REQUIRE(f.bcu.comObjects->objectSize(objno) == size);

        // mark the send buffer, it is free again afterwards
        uint8_t* sendBuffer = f.bcu.acquireSendBuffer();
        memset(sendBuffer, 0xa5, TelegramBufferSize);
        f.bcu.finishedSendingTelegram(true);

        const std::vector<uint8_t> value = longValue(objno);
        f.bcu.comObjects->objectWrite(objno, value.data());
        CHECK(f.bcu.comObjects->sendNextGroupTelegram());
        const uint8_t* sent = f.bcu.bus->sendCurTelegram;
#if SBLIB_MAX_APDU_LENGTH > 15
        REQUIRE(sent != nullptr);
        CHECK(sent[0] == 0x30); // extended frame, priority system
        CHECK(sent[1] == 0xE0); // group address, hop count 6
        CHECK(sent[4] == 0x08);
        CHECK(sent[5] == 0x01 + objno);
        CHECK(sent[6] == size + 1);
        CHECK(sent[7] == 0x00);
        CHECK(sent[8] == 0x80);
        for (int i = 0; i < size; ++i)
        {
            INFO("octet " << i);
            CHECK(sent[9 + i] == value[size - 1 - i]);
        }
#else
        // the APDU does not fit into a standard frame: nothing is written into the send buffer and nothing is sent
        CHECK(sent == nullptr);
        for (int i = 0; i < TelegramBufferSize; ++i)
        {
            INFO("octet " << i);
            CHECK(sendBuffer[i] == 0xa5);
        }
#endif
    }
}

TEST_CASE("SYSTEMB group object values above 14 octets need the exact APDU length", "[com_objects_systemb]")
{
    // receive a telegram with the given APDU length and check value and update flag of the object
    const auto receive = [](const int objno, const int apduLength, const bool updated)
    {
        const int size = LongObjects[objno].size;
        INFO("objno " << objno << ", " << size << " octets, APDU length " << apduLength);
        IAP_Init_Flash(0xff);
        Fixture f;
        setLongObjects(f);
        const std::vector<uint8_t> value = longValue(objno);
        std::vector<uint8_t> data = value;
        while (static_cast<int>(data.size()) + 1 < apduLength)
        {
            data.push_back(0xee); // octets of a value that is too long
        }
        f.receiveWrite(0x0801 + objno, data, apduLength);

        const uint8_t* objectValue = f.bcu.comObjects->objectValuePtr(objno);
        for (int i = 0; i < size; ++i)
        {
            INFO("octet " << i);
            if (updated)
            {
                CHECK(objectValue[i] == value[size - 1 - i]);
            }
            else
            {
                CHECK(objectValue[i] == 0);
            }
        }
        if (updated)
        {
            CHECK(f.bcu.comObjects->nextUpdatedObject() == objno);
        }
        CHECK(f.bcu.comObjects->nextUpdatedObject() == INVALID_OBJECT_NUMBER);
    };

    SECTION("a standard frame changes neither value nor update flag")
    {
        for (int objno = 0; objno < LongObjectCount; ++objno)
        {
            receive(objno, 15, false);
        }
    }

#if SBLIB_MAX_APDU_LENGTH > 15
    SECTION("an extended frame one octet too short changes neither value nor update flag")
    {
        for (int objno = 0; objno < LongObjectCount; ++objno)
        {
            receive(objno, LongObjects[objno].size, false);
        }
    }

    SECTION("an extended frame with the whole value updates the object")
    {
        for (int objno = 0; objno < LongObjectCount; ++objno)
        {
            receive(objno, LongObjects[objno].size + 1, true);
        }
    }

    SECTION("an extended frame one octet too long changes neither value nor update flag")
    {
        for (int objno = 0; objno < LongObjectCount; ++objno)
        {
            receive(objno, LongObjects[objno].size + 2, false);
        }
    }
#endif
}

TEST_CASE("SYSTEMB group object value one octet too long in a standard frame", "[com_objects_systemb]")
{
    // values above 14 octets need an extended frame, so a standard frame is checked with 13 octets (type 20)
    IAP_Init_Flash(0xff);
    Fixture f;
    setLongObjects(f);
    f.setObjectType(0, 20);
    REQUIRE(f.bcu.comObjects->objectSize(0) == 13);
    const uint8_t* objectValue = f.bcu.comObjects->objectValuePtr(0);
    std::vector<uint8_t> value(14, 0x5a);

    SECTION("an APDU of 15 octets changes neither value nor update flag")
    {
        f.receiveWrite(0x0801, value);
        for (int i = 0; i < 13; ++i)
        {
            INFO("octet " << i);
            CHECK(objectValue[i] == 0);
        }
        CHECK(f.bcu.comObjects->nextUpdatedObject() == INVALID_OBJECT_NUMBER);
    }

    SECTION("an APDU of 14 octets updates the object")
    {
        value.pop_back();
        f.receiveWrite(0x0801, value);
        for (int i = 0; i < 13; ++i)
        {
            INFO("octet " << i);
            CHECK(objectValue[i] == 0x5a);
        }
        CHECK(f.bcu.comObjects->nextUpdatedObject() == 0);
    }
}
