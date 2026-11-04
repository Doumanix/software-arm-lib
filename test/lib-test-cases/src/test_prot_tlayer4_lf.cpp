/*
 *  test_prot_tlayer4_lf.cpp - Transport layer and APCI access with L_Data_Extended frames
 *                             (KNX 3/2/2 2.2.5, 3/3/4, 3/3/7).
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <sblib/eib/apci.h>
#include <sblib/eib/knx_npdu.h>
#include <protocol.h>

#include <catch.hpp> // If possible, include catch.hpp as last header

TEST_CASE("APCI access in a standard frame", "[SBLIB][KNX][APCI]")
{
    // T_Data_Connected seq 0, A_DeviceDescriptor_Read 0
    uint8_t telegram[8] = {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0x00};
    CHECK(telegramTpciIndex(telegram) == 6);
    CHECK(apciCommand(telegram) == APCI_DEVICEDESCRIPTOR_READ_PDU);

    setApciCommand(telegram, APCI_GROUP_VALUE_WRITE_PDU, 0x01);
    CHECK(telegram[5] == 0x61);
    CHECK(telegram[6] == 0x00);
    CHECK(telegram[7] == 0x81);
    CHECK(apciCommand(telegram) == APCI_GROUP_VALUE_WRITE_PDU + 1);
}

#if SBLIB_MAX_APDU_LENGTH > 15

TEST_CASE("APCI access in an extended frame", "[SBLIB][KNX][APCI][lf]")
{
    // T_Data_Connected seq 0, A_DeviceDescriptor_Read 0, the TPCI is octet 7
    uint8_t telegram[9] = {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, 0x01, 0x43, 0x00};
    CHECK(telegramTpciIndex(telegram) == 7);
    CHECK(apciCommand(telegram) == APCI_DEVICEDESCRIPTOR_READ_PDU);

    // A_GroupValue_Write 1 in octets 7 and 8
    telegram[7] = 0x00;
    telegram[8] = 0x81;
    CHECK(apciCommand(telegram) == APCI_GROUP_VALUE_WRITE_PDU + 1);
}

TEST_CASE("APCI of a send buffer without control field", "[SBLIB][KNX][APCI][lf]")
{
    // Connected responses get their control field after processApci(): the APCI is written to the
    // octets 6 and 7 of a standard frame, independent of the control field.
    uint8_t sendBuffer[10] = {};
    setApciCommand(sendBuffer, APCI_DEVICEDESCRIPTOR_RESPONSE_PDU, 0);
    CHECK(sendBuffer[6] == 0x03);
    CHECK(sendBuffer[7] == 0x40);
    CHECK(sendBuffer[8] == 0x00);
}

namespace
{

    constexpr uint16_t ownAddress = 0xA000;    // 10.0.0
    constexpr uint16_t remoteAddress = 0xA001; // 10.0.1

    /**
     * BCU 1 that counts the calls of the application layer.
     */
    class Bcu1ApplicationLayerProbe : public BCU1
    {
    public:
        unsigned int apciCount = 0;
        unsigned int groupCount = 0;
        unsigned int broadcastCount = 0;

        bool processApci(ApciCommand apciCmd, unsigned char* telegram, TelegramLength telLength,
                         uint8_t* sendBuffer) override
        {
            apciCount++;
            return BCU1::processApci(apciCmd, telegram, telLength, sendBuffer);
        }

        bool processGroupAddressTelegram(ApciCommand apciCmd, uint16_t groupAddress, unsigned char* telegram,
                                         TelegramLength telLength) override
        {
            groupCount++;
            return BCU1::processGroupAddressTelegram(apciCmd, groupAddress, telegram, telLength);
        }

        bool processBroadCastTelegram(ApciCommand apciCmd, unsigned char* telegram, TelegramLength telLength) override
        {
            broadcastCount++;
            return BCU1::processBroadCastTelegram(apciCmd, telegram, telLength);
        }
    };

    Bcu1ApplicationLayerProbe* probe = nullptr;

    struct LfState
    {
        TLayer4::TL4State machineState;
        uint16_t connectedAddr;
        uint8_t seqNoRcv;
        unsigned int apciCount;
        unsigned int groupCount;
        unsigned int broadcastCount;
    };

    LfState lfState[2];

#   define LfS(s) (static_cast<LfState*>(s))

    void gatherLfState(void* state, void* refState)
    {
        LfS(state)->machineState = probe->state;
        LfS(state)->connectedAddr = probe->connectedAddr;
        LfS(state)->seqNoRcv = probe->seqNoRcv;
        LfS(state)->apciCount = probe->apciCount;
        LfS(state)->groupCount = probe->groupCount;
        LfS(state)->broadcastCount = probe->broadcastCount;

        if (refState)
        {
            REQUIRE(LfS(state)->machineState == LfS(refState)->machineState);
            REQUIRE(LfS(state)->connectedAddr == LfS(refState)->connectedAddr);
            REQUIRE(LfS(state)->seqNoRcv == LfS(refState)->seqNoRcv);
            REQUIRE(LfS(state)->apciCount == LfS(refState)->apciCount);
            REQUIRE(LfS(state)->groupCount == LfS(refState)->groupCount);
            REQUIRE(LfS(state)->broadcastCount == LfS(refState)->broadcastCount);
        }
    }

    void lfConnected(void* state, const unsigned int param)
    {
        LfS(state)->machineState = TLayer4::OPEN_IDLE;
        LfS(state)->connectedAddr = static_cast<uint16_t>(param);
        LfS(state)->seqNoRcv = 0;
    }

    void lfSeqNoRcv(void* state, const unsigned int param)
    {
        LfS(state)->seqNoRcv = static_cast<uint8_t>(param);
    }

    void lfApciProcessed(void* state, const unsigned int param)
    {
        LfS(state)->seqNoRcv = static_cast<uint8_t>(param);
        LfS(state)->apciCount++;
    }

    void lfOpenWait(void* state, [[maybe_unused]] const unsigned int param)
    {
        LfS(state)->machineState = TLayer4::OPEN_WAIT;
    }

    void lfOpenIdle(void* state, [[maybe_unused]] const unsigned int param)
    {
        LfS(state)->machineState = TLayer4::OPEN_IDLE;
    }

    void lfDisconnected(void* state, [[maybe_unused]] const unsigned int param)
    {
        LfS(state)->machineState = TLayer4::CLOSED;
    }

    void lfGroupProcessed(void* state, [[maybe_unused]] const unsigned int param)
    {
        LfS(state)->groupCount++;
    }

    void lfBroadcastProcessed(void* state, [[maybe_unused]] const unsigned int param)
    {
        LfS(state)->broadcastCount++;
    }

    void lfIndividualProcessed(void* state, [[maybe_unused]] const unsigned int param)
    {
        LfS(state)->apciCount++;
    }

    void tcSetup(Telegram* tel, const uint16_t telCount)
    {
        bcuUnderTest->setOwnAddress(ownAddress);
        telegramPreparation(bcuUnderTest, tel, telCount);
    }

    // Connection with extended frames from 10.0.1 to 10.0.0. The TL4 reads source address,
    // TPCI and sequence number of the extended frames, the application layer gets none of them.
    Telegram lfConnectedTelegrams[] =
    {
        // 1. Extended T_Connect: CTRL system priority, CTRLE individual address, hop count 6, length 0, TPCI
        {TEL_RX,  8, 0, remoteAddress, lfConnected, {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, 0x00, 0x80}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 2. Extended T_Data_Connected seq 0 with APDU length 20 (A_DeviceDescriptor_Read and 19 octets)
        {TEL_RX, 28, 0, 1, lfSeqNoRcv, {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, 20, 0x43, 0x00}},
        // T_ACK seq 0, but no response
        {TEL_TX,  7, 1, 1, nullptr, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 3. Extended T_Data_Connected seq 1 -> T_ACK seq 1
        {TEL_RX, 28, 0, 2, lfSeqNoRcv, {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, 20, 0x47, 0x00}},
        {TEL_TX,  7, 1, 2, nullptr, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 4. Repetition of seq 1 (event E05) -> T_ACK seq 1 again, receive sequence number unchanged
        {TEL_RX, 28, 0, 2, lfSeqNoRcv, {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, 20, 0x47, 0x00}},
        {TEL_TX,  7, 1, 2, nullptr, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 5. Standard T_Data_Connected seq 2, A_DeviceDescriptor_Read -> T_ACK seq 2 and the response
        {TEL_RX,  8, 0, 3, lfApciProcessed, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x4B, 0x00}},
        {TEL_TX,  7, 1, 0, nullptr, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xCA}},
        {TEL_TX, 10, 0, 0, lfOpenWait, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x63, 0x43, 0x40, dummyMaskVersionHigh,
                                        dummyMaskVersionLow}},
        // 6. Extended T_ACK seq 0 for the response
        {TEL_RX,  8, 0, 0, lfOpenIdle, {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, 0x00, 0xC2}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 7. Extended T_Disconnect
        {TEL_RX,  8, 0, 0, lfDisconnected, {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, 0x00, 0x81}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        {END, 0, 0, 0, nullptr, {0}}
    };

    // Connectionless extended frames: the TL4 reads the address type, the application layer gets none of them.
    Telegram lfConnectionlessTelegrams[] =
    {
        // 1. Standard A_GroupValue_Write to 1/0/1 reaches the application layer
        {TEL_RX,  8, 0, 0, lfGroupProcessed, {0xBC, 0xA0, 0x01, 0x08, 0x01, 0xE1, 0x00, 0x81}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 2. Extended T_Data_Group A_GroupValue_Write to 1/0/1 with APDU length 20: CTRLE group address
        {TEL_RX, 28, 0, 0, nullptr, {0x3C, 0xE0, 0xA0, 0x01, 0x08, 0x01, 20, 0x00, 0x80}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 3. Extended frame to group address 0xA000 (same value as the own address) with TPCI 0x80:
        //    the address type is read from CTRLE, so this is no T_Connect
        {TEL_RX,  8, 0, 0, nullptr, {0x30, 0xE0, 0xA0, 0x01, 0xA0, 0x00, 0x00, 0x80}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 4. Standard and extended A_IndividualAddress_Read broadcast
        {TEL_RX,  8, 0, 0, lfBroadcastProcessed, {0xB0, 0xA0, 0x01, 0x00, 0x00, 0xE1, 0x01, 0x00}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        {TEL_RX,  9, 0, 0, nullptr, {0x30, 0xE0, 0xA0, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 5. Extended T_Data_Individual A_DeviceDescriptor_Read: no call, no response
        {TEL_RX,  9, 0, 0, nullptr, {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, 0x01, 0x03, 0x00}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 6. The same as standard frame is answered
        {TEL_RX,  8, 0, 0, lfIndividualProcessed, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x03, 0x00}},
        {TEL_TX, 10, 0, 0, nullptr, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x63, 0x03, 0x40, dummyMaskVersionHigh,
                                     dummyMaskVersionLow}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        {END, 0, 0, 0, nullptr, {0}}
    };

    Test_Case lfConnectedTestCase =
    {
        "TL4 connection with extended frames",
        0x0004, 0x2060, 0x01,
        0,       // powerOnDelay
        nullptr, // eepromSetup
        tcSetup,
        gatherLfState,
        reinterpret_cast<TestCaseState*>(&lfState[0]),
        reinterpret_cast<TestCaseState*>(&lfState[1]),
        lfConnectedTelegrams
    };

    Test_Case lfConnectionlessTestCase =
    {
        "TL4 connectionless extended frames",
        0x0004, 0x2060, 0x01,
        0,       // powerOnDelay
        nullptr, // eepromSetup
        tcSetup,
        gatherLfState,
        reinterpret_cast<TestCaseState*>(&lfState[0]),
        reinterpret_cast<TestCaseState*>(&lfState[1]),
        lfConnectionlessTelegrams
    };

    void executeOnProbe(Test_Case* tc)
    {
        probe = new Bcu1ApplicationLayerProbe();
        executeTestOnBcu(probe, tc);
        delete probe;
        probe = nullptr;
    }

} // namespace

TEST_CASE("Transport layer 4 with extended frames", "[protocol][L4][lf]")
{
    SECTION("Connection, sequence numbers and repetition")
    {
        executeOnProbe(&lfConnectedTestCase);
    }
    SECTION("Group, broadcast and individual frames")
    {
        executeOnProbe(&lfConnectionlessTestCase);
    }
}

#endif // SBLIB_MAX_APDU_LENGTH > 15
