/*
 * Tests for the transmission priority of group telegrams (com_objects.cpp)
 *
 * The priority of a group object is stored in bits 1 and 0 of its config octet (KNX 03_05_01 4.18.3.1.2.1).
 * It is coded like the priority in the control field of the frame (KNX 03_02_02 2.2.2, bits 3 and 2), so
 * the config octet value is the value of KNXPriority. The names of the COMCONF_PRIO_* constants do not match
 * the names of KNXPriority, therefore the objects below are configured by value.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include <protocol.h>

#include <cstring>

namespace
{
    struct PriorityCase
    {
        uint8_t configPriority; // bits 1/0 of the config octet
        KNXPriority priority;   // expected priority in the frame
    };

    constexpr PriorityCase PriorityCases[] = {
            {COMCONF_PRIO_SYSTEM, PRIORITY_SYSTEM}, // system
            {COMCONF_PRIO_ALARM, PRIORITY_HIGH},    // normal
            {COMCONF_PRIO_HIGH, PRIORITY_ALARM},    // urgent
            {COMCONF_PRIO_LOW, PRIORITY_LOW},       // low
    };
    constexpr int CaseCount = sizeof(PriorityCases) / sizeof(PriorityCases[0]);

    constexpr int GroupAddress = 0x0801;
    constexpr uint8_t ConfigFlags = COMCONF_TRANS | COMCONF_WRITE_COMM | COMCONF_READ;

    constexpr uint8_t configOctet(const int objno)
    {
        return ConfigFlags | PriorityCases[objno].configPriority;
    }

    template<class ComObjectsType>
    class ComObjectsProbe final : public ComObjectsType
    {
    public:
        explicit ComObjectsProbe(BcuDefault* bcuInstance) :
            ComObjectsType(bcuInstance)
        {}

        using ComObjectsType::sendGroupReadTelegram;
        using ComObjectsType::sendGroupWriteTelegram;
    };

    // Keeps the last telegram given to the bus layer and releases the send buffer again
    template<class BcuType>
    class SendCapture : public BcuType
    {
    public:
        using BcuType::BcuType;

        uint8_t sent[16] = {};
        int sentCount = 0;

    protected:
        void send(unsigned char* telegram, const unsigned short length) override
        {
            size_t copyLength = length;
            if (copyLength > sizeof(sent))
            {
                copyLength = sizeof(sent);
            }
            memcpy(sent, telegram, copyLength);
            sentCount++;
            this->finishedSendingTelegram(true);
        }
    };

    class TestBcu1 final : public SendCapture<BCU1>
    {
    public:
        TestBcu1() :
            TestBcu1(new UserEepromBCU1())
        {}

    private:
        explicit TestBcu1(UserEepromBCU1* eeprom) :
            SendCapture(new UserRamBCU1(), eeprom, new ComObjectsProbe<ComObjectsBCU1>(this),
                        new AddrTablesBCU1(eeprom))
        {}
    };

    class TestBcu2 final : public SendCapture<BCU2>
    {
    public:
        TestBcu2() :
            SendCapture(new UserRamBCU2(), new UserEepromBCU2(), new ComObjectsProbe<ComObjectsBCU2>(this),
                        new AddrTablesBCU2(this), new PropertiesBCU2(this))
        {}
    };

    class TestBcuSystemB final : public SendCapture<SYSTEMB>
    {
    public:
        TestBcuSystemB() :
            SendCapture(new UserRamSYSTEMB(), new UserEepromSYSTEMB(), new ComObjectsProbe<ComObjectsSYSTEMB>(this),
                        new AddrTablesSYSTEMB(this), new PropertiesSYSTEMB(this))
        {}
    };

    // The pointers in the object table are stored high byte first (le_ptr of ComObjects)
    void putWord(uint8_t* ptr, const int value)
    {
        ptr[0] = value >> 8;
        ptr[1] = value & 255;
    }

    // Four objects with the priorities system, normal, urgent and low
    void setupBcu1(TestBcu1& bcu)
    {
        delete[] bcu.userEeprom->userEepromData;
        bcu.userEeprom->userEepromData = new uint8_t[1024]();
        uint8_t* table = bcu.userEeprom->userEepromData + 0x20;
        bcu.userEeprom->commsTabPtr() = 0x20;
        table[0] = CaseCount;
        table[1] = 0xc0; // RAM flags
        for (int i = 0; i < CaseCount; ++i)
        {
            table[2 + i * 3] = 0xd0 + i; // value in the user RAM
            table[3 + i * 3] = configOctet(i);
            table[4 + i * 3] = BIT_1;
        }
    }

    void setupBcu2(TestBcu2& bcu)
    {
        const uint16_t tableAddress = bcu.userEeprom->startAddr() + 0x40;
        bcu.userEeprom->commsTabAddr() = tableAddress;
        uint8_t* table = bcu.userMemoryPtr(tableAddress);
        const uint16_t flagsAddress = bcu.userRam->startAddr() + 0x10;
        const uint16_t valueAddress = bcu.userRam->startAddr() + 0x20;
        table[0] = CaseCount;
        putWord(table + 1, flagsAddress);
        for (int i = 0; i < CaseCount; ++i)
        {
            putWord(table + 3 + i * 4, valueAddress + i);
            table[5 + i * 4] = configOctet(i);
            table[6 + i * 4] = BIT_1;
        }
    }

    void setupBcuSystemB(TestBcuSystemB& bcu)
    {
        // empty address and association table: no local receiver for the group telegrams
        const uint16_t addrTableAddress = bcu.userEeprom->startAddr() + 0x100;
        const uint16_t assocTableAddress = bcu.userEeprom->startAddr() + 0x200;
        bcu.userEeprom->addrTabAddr() = addrTableAddress;
        bcu.userEeprom->assocTabAddr() = assocTableAddress;
        putWord(bcu.userMemoryPtr(addrTableAddress), 0);
        putWord(bcu.userMemoryPtr(assocTableAddress), 0);

        const uint16_t tableAddress = bcu.userEeprom->startAddr() + 0x400;
        bcu.userEeprom->commsTabAddr() = tableAddress;
        uint8_t* table = bcu.userMemoryPtr(tableAddress);
        table[0] = 0;
        table[1] = CaseCount;
        for (int i = 0; i < CaseCount; ++i)
        {
            table[2 + i * 2] = configOctet(i);
            table[3 + i * 2] = BIT_1;
        }
    }

    template<class ComObjectsType, class BcuType>
    void checkPriorities(BcuType& bcu)
    {
        auto* objects = static_cast<ComObjectsProbe<ComObjectsType>*>(bcu.comObjects);
        for (int objno = 0; objno < CaseCount; ++objno)
        {
            const KNXPriority expected = PriorityCases[objno].priority;
            INFO("objno " << objno << ", config octet " << static_cast<int>(configOctet(objno)));

            objects->sendGroupWriteTelegram(objno, GroupAddress, false);
            CHECK(priority(bcu.sent) == expected);
            CHECK((bcu.sent[0] & 0xf3) == 0xb0);
            CHECK(bcu.sent[0] == (0xb0 | (expected << 2)));
            CHECK(apciCommand(bcu.sent) == APCI_GROUP_VALUE_WRITE_PDU);

            objects->sendGroupWriteTelegram(objno, GroupAddress, true);
            CHECK(priority(bcu.sent) == expected);
            CHECK((bcu.sent[0] & 0xf3) == 0xb0);
            CHECK(apciCommand(bcu.sent) == APCI_GROUP_VALUE_RESPONSE_PDU);

            objects->sendGroupReadTelegram(objno, GroupAddress);
            CHECK(priority(bcu.sent) == expected);
            CHECK((bcu.sent[0] & 0xf3) == 0xb0);
            CHECK(apciCommand(bcu.sent) == APCI_GROUP_VALUE_READ_PDU);
        }
        CHECK(bcu.sentCount == 3 * CaseCount);
    }
} // namespace

TEST_CASE("BCU1 group telegrams are sent with the priority of the object", "[com_objects_priority]")
{
    IAP_Init_Flash(0xff);
    TestBcu1 bcu;
    setupBcu1(bcu);
    checkPriorities<ComObjectsBCU1>(bcu);
}

TEST_CASE("BCU2 group telegrams are sent with the priority of the object", "[com_objects_priority]")
{
    IAP_Init_Flash(0xff);
    TestBcu2 bcu;
    setupBcu2(bcu);
    checkPriorities<ComObjectsBCU2>(bcu);
}

TEST_CASE("SYSTEMB group telegrams are sent with the priority of the object", "[com_objects_priority]")
{
    IAP_Init_Flash(0xff);
    TestBcuSystemB bcu;
    setupBcuSystemB(bcu);
    checkPriorities<ComObjectsSYSTEMB>(bcu);
}
