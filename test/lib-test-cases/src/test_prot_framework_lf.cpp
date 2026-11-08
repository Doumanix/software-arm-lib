/*
 *  test_prot_framework_lf.cpp - Test framework with L_Data_Extended frames of up to 263 octets
 *                               (KNX 3/2/2 2.2.5, 3/3/7 3.1, 3/5/1 4.18.6).
 *
 *  A SYSTEM B device with two group objects of 65 and 252 octets. The group value read requests are standard
 *  frames, the responses are extended frames with an APDU length of 66 and 253. The write requests are extended
 *  frames, the last one has the largest APDU length of 254, so the telegram has 263 octets with the checksum.
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <protocol.h>

#include <catch.hpp> // If possible, include catch.hpp as last header

#if SBLIB_MAX_APDU_LENGTH > 15

namespace
{
    constexpr uint16_t ownAddress = 0xA000;    // 10.0.0
    constexpr uint16_t remoteAddress = 0xA001; // 10.0.1

    constexpr uint16_t addrTableAddress = 0x3400;
    constexpr uint16_t assocTableAddress = 0x3800;
    constexpr uint16_t objectTableAddress = 0x3900;
    constexpr uint16_t firstGroupAddress = 0x0801; // group address of object 0, one more for object 1
    constexpr uint16_t unusedGroupAddress = 0x0900;
    constexpr uint8_t configFlags = COMCONF_TRANS | COMCONF_WRITE_COMM | COMCONF_READ_COMM; // priority system

    constexpr int objectCount = 2;
    constexpr uint8_t objectTypes[objectCount] = {71, 255};  // object type n has n - 6 octets, 255 has 252 octets
    constexpr int objectSizes[objectCount] = {65, 252};
    constexpr int maxApduLength = 254;

    // Steps of the test case, the frames are built in tcSetup()
    enum Step
    {
        ReadObject0 = 0,
        ResponseObject0 = 1,
        ReadObject1 = 3,
        ResponseObject1 = 4,
        WriteObject0 = 6,
        WriteMaxApduLength = 7,
    };

    // octet i of the object value in memory, or of the write request
    uint8_t valueOctet(const int objno, const int i, const int variant)
    {
        return static_cast<uint8_t>(objno * 64 + i * 7 + variant * 31 + 1);
    }

    void putWord(uint8_t* ptr, const int value)
    {
        ptr[0] = value >> 8;
        ptr[1] = value & 255;
    }

    uint8_t* memory(const uint16_t address)
    {
        return &bcuUnderTest->userEeprom->userEepromData[address - bcuUnderTest->userEeprom->startAddr()];
    }

    /**
     * Builds an extended group frame, priority system, hop count 6. The octets of the value are in the order
     * of the memory of the group object, the frame has them reversed.
     */
    void extendedGroupFrame(Telegram* tel, const uint16_t source, const uint16_t destination, const int apduLength,
                            const uint8_t apci, const int objno, const int variant)
    {
        const int size = apduLength - 1;
        tel->length = 8 + apduLength;
        tel->bytes[0] = 0x30;
        tel->bytes[1] = 0xE0;
        putWord(&tel->bytes[2], source);
        putWord(&tel->bytes[4], destination);
        tel->bytes[6] = static_cast<uint8_t>(apduLength);
        tel->bytes[7] = 0x00; // T_Data_Group
        tel->bytes[8] = apci;
        for (int i = 0; i < size; ++i)
        {
            tel->bytes[9 + i] = valueOctet(objno, size - 1 - i, variant);
        }
    }

    // The frame has the octets of the write request reversed, so the memory gets them in the original order
    void checkObject0Written([[maybe_unused]] void* state, [[maybe_unused]] const unsigned int variant)
    {
        const uint8_t* value = bcuUnderTest->comObjects->objectValuePtr(0);
        for (int i = 0; i < objectSizes[0]; ++i)
        {
            INFO("octet " << i);
            CHECK(value[i] == valueOctet(0, i, 1));
        }
        CHECK(bcuUnderTest->comObjects->nextUpdatedObject() == 0);
        CHECK(bcuUnderTest->comObjects->nextUpdatedObject() == INVALID_OBJECT_NUMBER);
    }

    void checkNoUpdate([[maybe_unused]] void* state, [[maybe_unused]] const unsigned int variant)
    {
        CHECK(bcuUnderTest->comObjects->nextUpdatedObject() == INVALID_OBJECT_NUMBER);
    }

    void tcSetup(Telegram* tel, [[maybe_unused]] const uint16_t telCount)
    {
        bcuUnderTest->setOwnAddress(ownAddress);

        UserEepromSYSTEMB* userEeprom = static_cast<UserEepromSYSTEMB*>(bcuUnderTest->userEeprom);
        userEeprom->addrTabAddr() = addrTableAddress;
        userEeprom->assocTabAddr() = assocTableAddress;
        userEeprom->commsTabAddr() = objectTableAddress;

        putWord(memory(addrTableAddress), objectCount);
        putWord(memory(assocTableAddress), objectCount);
        putWord(memory(objectTableAddress), objectCount);
        for (int i = 0; i < objectCount; ++i)
        {
            putWord(memory(addrTableAddress) + (i + 1) * 2, firstGroupAddress + i);
            putWord(memory(assocTableAddress) + 2 + i * 4, i + 1); // address table index starts at 1
            putWord(memory(assocTableAddress) + 4 + i * 4, i + 1); // ASAP starts at 1
            memory(objectTableAddress)[2 + i * 2] = configFlags;
            memory(objectTableAddress)[3 + i * 2] = objectTypes[i];

            uint8_t* value = bcuUnderTest->comObjects->objectValuePtr(i);
            for (int j = 0; j < objectSizes[i]; ++j)
            {
                value[j] = valueOctet(i, j, 0);
            }
        }

        // The read requests are standard frames: A_GroupValue_Read to the group address of the object
        const Step readSteps[objectCount] = {ReadObject0, ReadObject1};
        for (int i = 0; i < objectCount; ++i)
        {
            Telegram* read = &tel[readSteps[i]];
            read->bytes[0] = 0xBC;
            putWord(&read->bytes[1], remoteAddress);
            putWord(&read->bytes[3], firstGroupAddress + i);
            read->bytes[5] = 0xE1;
        }

        extendedGroupFrame(&tel[ResponseObject0], ownAddress, firstGroupAddress, objectSizes[0] + 1, 0x40, 0, 0);
        extendedGroupFrame(&tel[ResponseObject1], ownAddress, firstGroupAddress + 1, objectSizes[1] + 1, 0x40, 1, 0);
        extendedGroupFrame(&tel[WriteObject0], remoteAddress, firstGroupAddress, objectSizes[0] + 1, 0x80, 0, 1);

        // APDU length 254 to a group address without association, the object values stay unchanged
        extendedGroupFrame(&tel[WriteMaxApduLength], remoteAddress, unusedGroupAddress, maxApduLength, 0x80, 1, 2);
    }

    Telegram extendedFrameTelegrams[] =
    {
        // 1. A_GroupValue_Read of object 0 -> A_GroupValue_Response with an APDU length of 66
        {TEL_RX,   8, 0, 0, nullptr, {}},
        {TEL_TX,   0, 0, 0, nullptr, {}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 2. A_GroupValue_Read of object 1 -> A_GroupValue_Response with an APDU length of 253
        {TEL_RX,   8, 0, 0, nullptr, {}},
        {TEL_TX,   0, 0, 0, nullptr, {}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        // 3. Extended A_GroupValue_Write to object 0 with an APDU length of 66, the value is stored
        {TEL_RX,   0, 0, 0, checkObject0Written, {}},
        // 4. Extended A_GroupValue_Write with an APDU length of 254 and 263 octets, nobody is addressed
        {TEL_RX,   0, 0, 0, checkNoUpdate, {}},
        {CHECK_TX_BUFFER, 0, 0, 0, nullptr, {}},
        {END, 0, 0, 0, nullptr, {0}}
    };

    Test_Case extendedFrameTestCase =
    {
        "Test framework with extended frames",
        0x0004, 0x2060, 0x01,
        0,       // powerOnDelay
        nullptr, // eepromSetup
        tcSetup,
        nullptr,
        nullptr,
        nullptr,
        extendedFrameTelegrams
    };

} // namespace

TEST_CASE("Test framework: extended frames in both directions", "[protocol][lf]")
{
    // the largest telegram of the test case and the framework buffer
    CHECK(TelegramBufferSize == 9 + SBLIB_MAX_APDU_LENGTH);
    CHECK(sizeof(Telegram::bytes) == TelegramBufferSize);
    CHECK(8 + maxApduLength + 1 == TelegramBufferSize);

    executeTest(BCU_07B0, &extendedFrameTestCase);
}

#endif // SBLIB_MAX_APDU_LENGTH > 15
