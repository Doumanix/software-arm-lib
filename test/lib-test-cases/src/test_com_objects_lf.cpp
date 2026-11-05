/*
 *  test_com_objects_lf.cpp - SYSTEM B group objects with values above 14 octets in L_Data_Extended frames
 *                            (KNX 3/2/2 2.2.5.1, 3/3/7 3.1, 3/5/1 4.18.6).
 *
 *  An A_GroupValue_Write or _Response with a value of n octets has an APDU of n + 1 octets: the APCI octet and
 *  the value. Up to 14 octets the frame is a standard frame, above it is an extended frame with the frame type
 *  bit 0 in the control field, the address type and hop count in CTRLE and the APDU length in octet 6.
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <protocol.h>
#include <sblib/eib/knx_lpdu.h>
#include <sblib/eib/knx_npdu.h>
#include <vector>

#include <catch.hpp> // If possible, include catch.hpp as last header

#if SBLIB_MAX_APDU_LENGTH > 15

namespace
{
    using Frame = std::vector<uint8_t>;

    constexpr uint16_t AddrTableAddress = 0x3400;
    constexpr uint16_t AssocTableAddress = 0x3800;
    constexpr uint16_t ObjectTableAddress = 0x3900;
    constexpr uint16_t FirstGroupAddress = 0x0801; // group address of object 0, one more for each object
    constexpr uint8_t ConfigFlags = COMCONF_TRANS | COMCONF_WRITE_COMM | COMCONF_READ_COMM; // priority system

    struct LongObject
    {
        uint8_t type; // SYSTEM B object type
        int size;     // value size in octets, ComObjectsSYSTEMB::objectSize()
    };

    // the last object has the longest value in a standard frame
    constexpr LongObject Objects[] = {{21, 15}, {106, 100}, {254, 248}, {255, 252}, {14, 14}};
    constexpr int ObjectCount = sizeof(Objects) / sizeof(Objects[0]);
    constexpr int StandardObject = ObjectCount - 1;

    class ComObjectsProbe final : public ComObjectsSYSTEMB
    {
    public:
        explicit ComObjectsProbe(BcuDefault* bcuInstance) :
            ComObjectsSYSTEMB(bcuInstance)
        {}

        using ComObjectsSYSTEMB::sendGroupWriteTelegram;
    };

    // Keeps the last telegram given to the bus layer and releases the send buffer again
    class TestBcu final : public SYSTEMB
    {
    public:
        TestBcu() :
            SYSTEMB(new UserRamSYSTEMB(), new UserEepromSYSTEMB(), new ComObjectsProbe(this),
                    new AddrTablesSYSTEMB(this), new PropertiesSYSTEMB(this))
        {}

        Frame sent;
        int sentCount = 0;

    protected:
        void send(unsigned char* telegram, const unsigned short length) override
        {
            sent.assign(telegram, telegram + length);
            sentCount++;
            finishedSendingTelegram(true);
        }
    };

    void putWord(uint8_t* ptr, const int value)
    {
        ptr[0] = value >> 8;
        ptr[1] = value & 255;
    }

    // value octet i of object objno
    uint8_t valueOctet(const int objno, const int i)
    {
        return static_cast<uint8_t>(objno * 64 + i * 7 + 1);
    }

    struct Fixture
    {
        TestBcu bcu;

        // one group address and one association for each object
        Fixture()
        {
            IAP_Init_Flash(0xff);
            bcu.userEeprom->addrTabAddr() = AddrTableAddress;
            bcu.userEeprom->assocTabAddr() = AssocTableAddress;
            bcu.userEeprom->commsTabAddr() = ObjectTableAddress;

            putWord(memory(AddrTableAddress), ObjectCount);
            putWord(memory(AssocTableAddress), ObjectCount);
            putWord(memory(ObjectTableAddress), ObjectCount);
            for (int i = 0; i < ObjectCount; ++i)
            {
                putWord(memory(AddrTableAddress) + (i + 1) * 2, FirstGroupAddress + i);
                putWord(memory(AssocTableAddress) + 2 + i * 4, i + 1); // address table index starts at 1
                putWord(memory(AssocTableAddress) + 4 + i * 4, i + 1); // ASAP starts at 1
                memory(ObjectTableAddress)[2 + i * 2] = ConfigFlags;
                memory(ObjectTableAddress)[3 + i * 2] = Objects[i].type;
            }
        }

        uint8_t* memory(const uint16_t address)
        {
            return &bcu.userEeprom->userEepromData[address - bcu.userEeprom->startAddr()];
        }

        ComObjectsProbe* objects() { return static_cast<ComObjectsProbe*>(bcu.comObjects); }

        uint8_t* value(const int objno) { return bcu.comObjects->objectValuePtr(objno); }

        void fillValue(const int objno)
        {
            for (int i = 0; i < Objects[objno].size; ++i)
            {
                value(objno)[i] = valueOctet(objno, i);
            }
        }

        void receive(const int objno, const int apci, Frame frame)
        {
            bcu.comObjects->processGroupTelegram(FirstGroupAddress + objno, apci, frame.data());
        }
    };

    /**
     * @brief Group telegram with the value of an object, priority system, hop count 6.
     * @param objno   Object number, its group address is the destination
     * @param apci    Low octet of the APCI
     * @param sender  Source address
     * @param reverse True for the octet order of the object value in the frame (reverseCopy())
     */
    Frame groupFrame(const int objno, const uint8_t apci, const uint16_t sender, const bool reverse)
    {
        const int size = Objects[objno].size;
        const auto apduLength = static_cast<uint8_t>(size + 1);
        const uint16_t destination = FirstGroupAddress + objno;
        Frame frame;
        if (apduLength > 15)
        {
            frame = {0x30,
                     0xE0,
                     static_cast<uint8_t>(sender >> 8),
                     static_cast<uint8_t>(sender),
                     static_cast<uint8_t>(destination >> 8),
                     static_cast<uint8_t>(destination),
                     apduLength};
        }
        else
        {
            frame = {0xB0,
                     static_cast<uint8_t>(sender >> 8),
                     static_cast<uint8_t>(sender),
                     static_cast<uint8_t>(destination >> 8),
                     static_cast<uint8_t>(destination),
                     static_cast<uint8_t>(0xE0 | apduLength)};
        }
        frame.push_back(0x00); // TPCI T_Data_Group
        frame.push_back(apci);
        for (int i = 0; i < size; ++i)
        {
            if (reverse)
            {
                frame.push_back(valueOctet(objno, size - 1 - i));
            }
            else
            {
                frame.push_back(valueOctet(objno, i));
            }
        }
        return frame;
    }

    uint16_t sender(const Frame& frame)
    {
        return lpduSenderAddress(frame.data());
    }
} // namespace

TEST_CASE("SYSTEMB group objects with long values are sent in extended frames", "[com_objects_systemb][lf]")
{
    for (int objno = 0; objno < ObjectCount; ++objno)
    {
        const int size = Objects[objno].size;
        INFO("objno " << objno << ", type " << static_cast<int>(Objects[objno].type) << ", " << size << " octets");
        Fixture f;
        f.fillValue(objno);
        f.objects()->sendGroupWriteTelegram(objno, FirstGroupAddress + objno, false);
        REQUIRE(f.bcu.sentCount == 1);
        CHECK(f.bcu.sent == groupFrame(objno, 0x80, sender(f.bcu.sent), true));
        CHECK(isExtendedFrame(f.bcu.sent.data()) == (objno != StandardObject));
        CHECK(lpduApduLength(f.bcu.sent.data()) == size + 1);
    }
}

TEST_CASE("SYSTEMB group objects with long values answer A_GroupValue_Read", "[com_objects_systemb][lf]")
{
    for (int objno = 0; objno < ObjectCount; ++objno)
    {
        INFO("objno " << objno << ", type " << static_cast<int>(Objects[objno].type));
        Fixture f;
        f.fillValue(objno);
        Frame read = {0xBC, 0x11, 0x02, 0x00, 0x00, 0xE1, 0x00, 0x00};
        putWord(read.data() + 3, FirstGroupAddress + objno);
        f.receive(objno, APCI_GROUP_VALUE_READ_PDU, read);
        REQUIRE(f.bcu.sentCount == 1);
        CHECK(f.bcu.sent == groupFrame(objno, 0x40, sender(f.bcu.sent), true));
        CHECK(isExtendedFrame(f.bcu.sent.data()) == (objno != StandardObject));
    }
}

TEST_CASE("SYSTEMB group objects with long values receive A_GroupValue_Write", "[com_objects_systemb][lf]")
{
    for (int objno = 0; objno < ObjectCount; ++objno)
    {
        const int size = Objects[objno].size;
        INFO("objno " << objno << ", type " << static_cast<int>(Objects[objno].type));
        Fixture f;
        f.receive(objno, APCI_GROUP_VALUE_WRITE_PDU, groupFrame(objno, 0x80, 0x1102, false));
        for (int i = 0; i < size; ++i)
        {
            INFO("octet " << i);
            CHECK(f.value(objno)[i] == valueOctet(objno, size - 1 - i));
        }
        CHECK(f.bcu.comObjects->nextUpdatedObject() == objno);
        CHECK(f.bcu.comObjects->nextUpdatedObject() == INVALID_OBJECT_NUMBER);
        CHECK(f.bcu.sentCount == 0);
    }
}

#endif // SBLIB_MAX_APDU_LENGTH > 15
