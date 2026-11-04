/*
 *  test_prot_memory_lf.cpp - Management services with long APDUs and the frame format of the responses
 *                            (KNX 3/2/2 2.2.5.1, 3/3/7 3.4.4, 3.5.3, 3.5.4, 3/5/1 4.3.7).
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <protocol.h>
#include <sblib/eib/bus_const.h>
#include <sblib/eib/knx_npdu.h>
#include <sblib/eib/userEepromSYSTEMB.h>
#include <sblib/eib/userRam.h>
#include <cstring>
#include <vector>

#include <catch.hpp> // If possible, include catch.hpp as last header

namespace
{

    using Frame = std::vector<uint8_t>;

    void (*scenario)() = nullptr; //!< test steps that run on the initialized BCU under test

    void runScenario([[maybe_unused]] void* state, [[maybe_unused]] const unsigned int param)
    {
        scenario();
    }

    void tcSetup(Telegram* tel, const uint16_t telCount)
    {
        bcuUnderTest->setOwnAddress(0xA000); // 10.0.0
        telegramPreparation(bcuUnderTest, tel, telCount);
    }

    Telegram scenarioTelegrams[] =
    {
        {LOOP, 0, 0, 0, runScenario, {}},
        {END, 0, 0, 0, nullptr, {0}}
    };

    Test_Case scenarioTestCase =
    {
        "management services with long APDUs",
        0x0004, 0x2060, 0x01,
        0,       // powerOnDelay
        nullptr, // eepromSetup
        tcSetup,
        nullptr,
        nullptr,
        nullptr,
        scenarioTelegrams
    };

    /**
     * @brief Run test steps on a new BCU of the given type.
     * @param bcuType BCU type
     * @param steps   Test steps, they use @ref bcuUnderTest
     */
    void run(const BcuType bcuType, void (*steps)())
    {
        scenario = steps;
        executeTest(bcuType, &scenarioTestCase);
        scenario = nullptr;
    }

    /**
     * @brief Frame from 10.0.1 to 10.0.0, system priority, hop count 6.
     * @param extended True for an L_Data_Extended frame, false for a standard frame
     * @param tpdu     TPCI octet and the following APDU octets
     */
    Frame request(const bool extended, const Frame& tpdu)
    {
        Frame frame;
        const auto apduLength = static_cast<uint8_t>(tpdu.size() - 1);
        if (extended)
        {
            frame = {0x30, 0x60, 0xA0, 0x01, 0xA0, 0x00, apduLength};
        }
        else
        {
            frame = {0xB0, 0xA0, 0x01, 0xA0, 0x00, static_cast<uint8_t>(0x60 | apduLength)};
        }
        frame.insert(frame.end(), tpdu.begin(), tpdu.end());
        return frame;
    }

    /**
     * @brief Expected frame from 10.0.0 to 10.0.1, system priority, hop count 6.
     * @param extended True for an L_Data_Extended frame, false for a standard frame
     * @param tpdu     TPCI octet and the following APDU octets
     */
    Frame response(const bool extended, const Frame& tpdu)
    {
        Frame frame;
        const auto apduLength = static_cast<uint8_t>(tpdu.size() - 1);
        if (extended)
        {
            frame = {0x30, 0x60, 0xA0, 0x00, 0xA0, 0x01, apduLength};
        }
        else
        {
            frame = {0xB0, 0xA0, 0x00, 0xA0, 0x01, static_cast<uint8_t>(0x60 | apduLength)};
        }
        frame.insert(frame.end(), tpdu.begin(), tpdu.end());
        return frame;
    }

    /**
     * @brief Let the BCU under test receive a frame, the checksum is added.
     * @param frame Frame without the checksum
     */
    void receive(Frame frame)
    {
        Bus* bus = bcuUnderTest->bus;
        uint8_t checksum = 0xff;
        for (const uint8_t octet : frame)
        {
            checksum ^= octet;
        }
        frame.push_back(checksum);
        REQUIRE(frame.size() <= TelegramBufferSize);

        LPC_TMR16B1->IR = 0x00;
        memcpy(bus->telegram, frame.data(), frame.size());
        bus->telegramLen = static_cast<TelegramLength>(frame.size());
        bcuUnderTest->processTelegram(bus->telegram, bus->telegramLen);
        REQUIRE(bus->telegramLen == 0);
    }

    /**
     * @brief The frame the bus of the BCU under test sends next. Sending finishes with an acknowledge.
     * @return The frame without the checksum, empty if the bus sends nothing
     */
    Frame sent()
    {
        Bus* bus = bcuUnderTest->bus;
        LPC_TMR16B1->IR = 0x00;
        if (bus->state == Bus::WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE)
        {
            bus->timerInterruptHandler();
        }
        if (bus->sendCurTelegram == nullptr)
        {
            return {};
        }

        const Frame frame(bus->sendCurTelegram, bus->sendCurTelegram + telegramSize(bus->sendCurTelegram));

        // the same as _handleBusSendingInterrupt() in protocol.cpp
        if (bus->state == Bus::SEND_START_BIT)
        {
            _LPC_TMR16B1.TC += 10;
            bus->state = Bus::SEND_END_OF_TX;
            bus->timerInterruptHandler();
            if (bus->state == Bus::SEND_WAIT_FOR_RX_ACK_WINDOW)
            {
                bus->telegram[0] = bus->currentByte = SB_BUS_ACK;
                bus->telegramLen = 1;
                bus->state = Bus::RECV_WAIT_FOR_STARTBIT_OR_TELEND;
                bus->nextByteIndex = 1;
                bus->parity = 1;
                _LPC_TMR16B1.IR = 0;
                bus->timerInterruptHandler();
                bus->telegramLen = 0;
                LPC_TMR16B1->IR = 0x00;
            }
        }
        REQUIRE(((bus->state == Bus::IDLE) || (bus->state == Bus::WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE)));
        REQUIRE(bus->sendCurTelegram == nullptr);
        return frame;
    }

    /**
     * @brief Open a connection and send one T_Data_Connected with sequence number 0.
     * @param extended True for L_Data_Extended frames, false for standard frames
     * @param tpdu     TPCI octet (APCI bits only) and the following APDU octets
     * @return The response of the BCU under test, empty if there is none
     */
    Frame connectedRequest(const bool extended, Frame tpdu)
    {
        receive(request(extended, {0x80})); // T_Connect
        CHECK(sent().empty());

        tpdu[0] |= 0x40; // T_Data_Connected, sequence number 0
        receive(request(extended, tpdu));
        CHECK(sent() == Frame({0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2})); // T_ACK, sequence number 0
        bcuUnderTest->loop();
        return sent();
    }

    /**
     * @brief Open a connection and send one A_PropertyValue_Read with standard frames.
     * @return The response of the BCU under test
     */
    Frame propertyValueRead(const uint8_t objectIndex, const PropertyID propertyId, const uint8_t count)
    {
        return connectedRequest(false, {0x03, 0xD5, objectIndex, static_cast<uint8_t>(propertyId),
                                        static_cast<uint8_t>(count << 4), 0x01});
    }

    // PID_MAX_APDULENGTH of the device object holds SBLIB_MAX_APDU_LENGTH as unsigned int
    void maxApduLength()
    {
        const Frame expected = response(false, {0x43, 0xD6, 0x00, PID_MAX_APDULENGTH, 0x10, 0x01, 0x00,
                                                SBLIB_MAX_APDU_LENGTH});
        CHECK(propertyValueRead(OT_DEVICE, PID_MAX_APDULENGTH, 1) == expected);
    }

} // namespace

TEST_CASE("PID_MAX_APDULENGTH of the device object", "[protocol][APCI][property]")
{
    SECTION("BCU 2")
    {
        run(BCU_2, maxApduLength);
    }
    SECTION("BCU 0x0701")
    {
        run(BCU_0701, maxApduLength);
    }
    SECTION("BCU 0x0705")
    {
        run(BCU_0705, maxApduLength);
    }
    SECTION("SYSTEM B")
    {
        run(BCU_07B0, maxApduLength);
    }
}

#if SBLIB_MAX_APDU_LENGTH > 15

namespace
{

    // A_Memory_Read: 1..63 octets in a 6 bit field, at most the maximum APDU length - 3 (KNX 3/3/7 3.5.3)
    constexpr uint8_t maxMemoryNumber()
    {
        uint8_t number = 63;
        if (SBLIB_MAX_APDU_LENGTH - 3 < 63)
        {
            number = SBLIB_MAX_APDU_LENGTH - 3;
        }
        return number;
    }

    constexpr uint8_t memoryMaxNumber = maxMemoryNumber();

    constexpr uint16_t memoryOffset = 0x80; //!< offset of the tested block in the user EEPROM

    uint16_t memoryAddress(const uint16_t offset)
    {
        return static_cast<uint16_t>(bcuUnderTest->userEeprom->startAddr() + offset);
    }

    /**
     * @brief Expected A_Memory_Response: an extended frame with more than 12 octets, else a standard frame.
     * @param address Start address
     * @param data    The octets read
     */
    Frame memoryResponse(const uint16_t address, const Frame& data)
    {
        Frame tpdu = {0x42, static_cast<uint8_t>(0x40 | data.size()), static_cast<uint8_t>(address >> 8),
                      static_cast<uint8_t>(address & 0xff)};
        tpdu.insert(tpdu.end(), data.begin(), data.end());
        return response(data.size() > 12, tpdu);
    }

    Frame memoryReadRequest(const uint8_t number, const uint16_t address)
    {
        return {0x02, number, static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address & 0xff)};
    }

    Frame memoryWriteRequest(const uint8_t number, const uint16_t address, const Frame& data)
    {
        Frame tpdu = {0x02, static_cast<uint8_t>(0x80 | number), static_cast<uint8_t>(address >> 8),
                      static_cast<uint8_t>(address & 0xff)};
        tpdu.insert(tpdu.end(), data.begin(), data.end());
        return tpdu;
    }

    /** Fill the tested block of the user EEPROM with a pattern and return the first count octets */
    Frame fillMemory(const uint8_t count)
    {
        Frame data;
        for (uint8_t i = 0; i < 64; i++)
        {
            bcuUnderTest->userEeprom->userEepromData[memoryOffset + i] = static_cast<uint8_t>(0x10 + i);
        }
        for (uint8_t i = 0; i < count; i++)
        {
            data.push_back(static_cast<uint8_t>(0x10 + i));
        }
        return data;
    }

    Frame memory(const uint8_t count)
    {
        const uint8_t* eeprom = bcuUnderTest->userEeprom->userEepromData + memoryOffset;
        return Frame(eeprom, eeprom + count);
    }

    Frame pattern(const uint8_t count, const uint8_t first)
    {
        Frame data;
        for (uint8_t i = 0; i < count; i++)
        {
            data.push_back(static_cast<uint8_t>(first + i));
        }
        return data;
    }

    // (a) 13 octets need an extended response: LG in octet 6, TPCI in octet 7, number octets of data
    void memoryRead13()
    {
        const Frame data = fillMemory(13);
        const Frame actual = connectedRequest(true, memoryReadRequest(13, memoryAddress(memoryOffset)));
        CHECK(actual == memoryResponse(memoryAddress(memoryOffset), data));
        REQUIRE(actual.size() == 8 + 3 + 13);
        CHECK(actual[6] == 3 + 13);
        CHECK(actual[7] == 0x42);
        CHECK(actual[8] == (0x40 | 13));
    }

    // (a) the maximum number of the specification
    void memoryReadMaximum()
    {
        const Frame data = fillMemory(memoryMaxNumber);
        const Frame actual = connectedRequest(true, memoryReadRequest(memoryMaxNumber, memoryAddress(memoryOffset)));
        CHECK(actual == memoryResponse(memoryAddress(memoryOffset), data));
        CHECK(actual.size() == 8u + 3u + memoryMaxNumber);
    }

#   if SBLIB_MAX_APDU_LENGTH - 3 < 63
    // (a) a number above the maximum is answered with number 0
    void memoryReadTooMany()
    {
        fillMemory(0);
        const uint16_t address = memoryAddress(memoryOffset);
        CHECK(connectedRequest(true, memoryReadRequest(memoryMaxNumber + 1, address)) == memoryResponse(address, {}));
    }
#   endif

    // (c) up to 12 octets the response is a standard frame, also for an extended request
    void memoryRead12Extended()
    {
        const Frame data = fillMemory(12);
        const Frame actual = connectedRequest(true, memoryReadRequest(12, memoryAddress(memoryOffset)));
        CHECK(actual == memoryResponse(memoryAddress(memoryOffset), data));
        CHECK(actual[0] == 0xB0);
        CHECK(actual[5] == (0x60 | 15));
    }

    // (b) BCU 1 writes 13 octets, no automatic response
    void memoryWrite13()
    {
        fillMemory(0);
        const Frame data = pattern(13, 0xA0);
        CHECK(connectedRequest(true, memoryWriteRequest(13, memoryAddress(memoryOffset), data)).empty());
        CHECK(memory(13) == data);
    }

    // (b) with DEVCTRL_MEM_AUTO_RESPONSE the BCU answers a write with the octets read back
    void memoryWriteAutoResponse()
    {
        fillMemory(0);
        bcuUnderTest->userRam->deviceControl() |= DEVCTRL_MEM_AUTO_RESPONSE;
        const Frame data = pattern(40, 0xA0);
        const uint16_t address = memoryAddress(memoryOffset);
        CHECK(connectedRequest(true, memoryWriteRequest(40, address, data)) == memoryResponse(address, data));
        CHECK(memory(40) == data);
    }

    // (b) a write with a number that does not match the frame length is ignored
    void memoryWriteWrongLength()
    {
        const Frame before = fillMemory(20);
        bcuUnderTest->userRam->deviceControl() |= DEVCTRL_MEM_AUTO_RESPONSE;
        const Frame data = pattern(19, 0xA0);
        CHECK(connectedRequest(true, memoryWriteRequest(20, memoryAddress(memoryOffset), data)).empty());
        CHECK(memory(20) == before);
    }

    /** Expected A_PropertyValue_Response: an extended frame with a value of more than 10 octets */
    Frame propertyResponse(const uint8_t objectIndex, const PropertyID propertyId, const uint8_t count,
                           const Frame& value)
    {
        Frame tpdu = {0x43, 0xD6, objectIndex, static_cast<uint8_t>(propertyId), static_cast<uint8_t>(count << 4),
                      0x01};
        tpdu.insert(tpdu.end(), value.begin(), value.end());
        return response(value.size() > 10, tpdu);
    }

    Frame propertyValue(const uint8_t objectIndex, const PropertyID propertyId, const uint8_t octets)
    {
        const PropertyDef* def = static_cast<BCU2*>(bcuUnderTest)->properties->propertyDef(objectIndex, propertyId);
        REQUIRE(def != nullptr);
        const uint8_t* value = def->valuePointer(bcuUnderTest);
        return Frame(value, value + octets);
    }

    // (d) SYSTEM B has one element per property, 2 elements of PID_MCB_TABLE (16 octets) are rejected:
    // number of elements 0, standard response
    void systemBPropertyRead()
    {
        const Frame actual = connectedRequest(true, {0x03, 0xD5, OT_ADDR_TABLE, PID_MCB_TABLE, 0x20, 0x01});
        CHECK(actual == propertyResponse(OT_ADDR_TABLE, PID_MCB_TABLE, 0, {}));
    }

    void systemBPropertyReadStandard()
    {
        uint8_t* eeprom = bcuUnderTest->userEeprom->userEepromData;
        for (uint8_t i = 0; i < 8; i++)
        {
            eeprom[UserEepromSYSTEMB::addrTabMcbOffset + i] = static_cast<uint8_t>(0x30 + i);
        }
        const Frame actual = connectedRequest(true, {0x03, 0xD5, OT_ADDR_TABLE, PID_MCB_TABLE, 0x10, 0x01});
        CHECK(actual == propertyResponse(OT_ADDR_TABLE, PID_MCB_TABLE, 1, pattern(8, 0x30)));
        CHECK(actual[5] == (0x60 | 13));
    }

    // (d) SYSTEM B rejects 2 elements of PID_MCB_TABLE (16 octets): number of elements 0, standard response,
    // the stored value is unchanged
    void systemBPropertyWrite()
    {
        const Frame before = propertyValue(OT_ADDR_TABLE, PID_MCB_TABLE, 16);
        Frame tpdu = {0x03, 0xD7, OT_ADDR_TABLE, PID_MCB_TABLE, 0x20, 0x01};
        const Frame value = pattern(16, 0x50);
        tpdu.insert(tpdu.end(), value.begin(), value.end());
        CHECK(connectedRequest(true, tpdu) == propertyResponse(OT_ADDR_TABLE, PID_MCB_TABLE, 0, {}));
        CHECK(propertyValue(OT_ADDR_TABLE, PID_MCB_TABLE, 16) == before);
    }

    // (d) a value shorter than nr_of_elem * size is rejected: number of elements 0, standard response
    void systemBPropertyWriteTooShort()
    {
        Frame tpdu = {0x03, 0xD7, OT_ADDR_TABLE, PID_MCB_TABLE, 0x10, 0x01};
        const Frame value = pattern(7, 0x50);
        tpdu.insert(tpdu.end(), value.begin(), value.end());
        CHECK(connectedRequest(true, tpdu) == propertyResponse(OT_ADDR_TABLE, PID_MCB_TABLE, 0, {}));
    }

    // (d) BCU 2 has one element per property, at most 10 octets (PID_ORDER_INFO): standard response
    void bcu2PropertyRead()
    {
        const Frame actual = connectedRequest(true, {0x03, 0xD5, OT_DEVICE, PID_ORDER_INFO, 0x10, 0x01});
        CHECK(actual == propertyResponse(OT_DEVICE, PID_ORDER_INFO, 1, propertyValue(OT_DEVICE, PID_ORDER_INFO, 10)));
        CHECK(actual[5] == (0x60 | 15));
    }

    // (d) BCU 2 rejects 2 elements of PID_HARDWARE_TYPE (12 octets): number of elements 0, standard response
    void bcu2PropertyWriteTwoElements()
    {
        Frame tpdu = {0x03, 0xD7, OT_DEVICE, PID_HARDWARE_TYPE, 0x20, 0x01};
        const Frame value = pattern(12, 0x50);
        tpdu.insert(tpdu.end(), value.begin(), value.end());
        CHECK(connectedRequest(true, tpdu) == propertyResponse(OT_DEVICE, PID_HARDWARE_TYPE, 0, {}));
    }

} // namespace

TEST_CASE("Memory services with long APDUs", "[protocol][APCI][apci-memory][lf]")
{
    SECTION("Memory_Read of 13 octets")
    {
        run(BCU_1, memoryRead13);
        run(BCU_2, memoryRead13);
    }
    SECTION("Memory_Read of the maximum number")
    {
        run(BCU_1, memoryReadMaximum);
        run(BCU_07B0, memoryReadMaximum);
    }
#   if SBLIB_MAX_APDU_LENGTH - 3 < 63
    SECTION("Memory_Read above the maximum number")
    {
        run(BCU_1, memoryReadTooMany);
    }
#   endif
    SECTION("Memory_Read of 12 octets from an extended frame")
    {
        run(BCU_1, memoryRead12Extended);
        run(BCU_0705, memoryRead12Extended);
    }
    SECTION("Memory_Write of 13 octets")
    {
        run(BCU_1, memoryWrite13);
    }
    SECTION("Memory_Write with automatic response")
    {
        run(BCU_2, memoryWriteAutoResponse);
        run(BCU_07B0, memoryWriteAutoResponse);
    }
    SECTION("Memory_Write with a wrong length")
    {
        run(BCU_2, memoryWriteWrongLength);
    }
}

TEST_CASE("Property value services with long APDUs", "[protocol][APCI][property][lf]")
{
    SECTION("SYSTEM B property value read")
    {
        run(BCU_07B0, systemBPropertyRead);
        run(BCU_07B0, systemBPropertyReadStandard);
    }
    SECTION("SYSTEM B property value write")
    {
        run(BCU_07B0, systemBPropertyWrite);
        run(BCU_07B0, systemBPropertyWriteTooShort);
    }
    SECTION("BCU 2 property values")
    {
        run(BCU_2, bcu2PropertyRead);
        run(BCU_2, bcu2PropertyWriteTwoElements);
    }
}

#endif // SBLIB_MAX_APDU_LENGTH > 15
