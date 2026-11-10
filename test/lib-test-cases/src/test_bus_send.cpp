/**
 * Unit tests of Bus::sendTelegram() with standard and
 * L_Data_Extended frames (KNX 3/2/2 2.2.4 and 2.2.5).
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * @par
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "test_bus.h"

#include <sblib/eib/bus_const.h>
#include <sblib/eib/knx_lpdu.h>

#include <cstdint>
#include <cstring>

namespace
{
    constexpr uint16_t OwnAddress = 0x1165; // 1.1.101
    constexpr uint8_t Layers = BCU_STATUS_LINK_LAYER | BCU_STATUS_TRANSPORT_LAYER;

    // Octets behind the telegram buffer, they must stay unchanged
    constexpr uint16_t GuardOctets = 16;
    constexpr uint8_t GuardValue = 0xa5;
    constexpr uint16_t BufferSize = TelegramBufferSize + GuardOctets;

    void requireGuardUnchanged(const uint8_t* buffer)
    {
        for (uint16_t i = TelegramBufferSize; i < BufferSize; ++i)
        {
            REQUIRE(buffer[i] == GuardValue);
        }
    }

    bool hasValidChecksum(const uint8_t* frame, const uint16_t count)
    {
        uint8_t checksum = 0xff;
        for (uint16_t i = 0; i < count; ++i)
        {
            checksum ^= frame[i];
        }
        return checksum == 0;
    }

} // namespace

TEST_CASE("Bus send: a standard frame gets the own address and the checksum", "[SBLIB][KNX][Bus]")
{
    BusTest t(OwnAddress, Layers);
    uint8_t buffer[BufferSize];
    memset(buffer, GuardValue, BufferSize);

    // GroupValueWrite 0/0/100, sender 0.0.0 is replaced by the own address
    const uint8_t frame[] = {0xBC, 0x00, 0x00, 0x00, 0x64, 0xE1, 0x00, 0x81};
    constexpr uint16_t FrameLength = sizeof(frame);
    memcpy(buffer, frame, FrameLength);

    t.bus.sendTelegram(buffer, FrameLength);

    REQUIRE(t.callback.finishedCount == 0);
    REQUIRE(t.bus.sendCurTelegram == buffer);
    REQUIRE(buffer[0] == 0xBC);
    REQUIRE(buffer[1] == (OwnAddress >> 8));
    REQUIRE(buffer[2] == (OwnAddress & 0xff));
    REQUIRE(memcmp(&buffer[3], &frame[3], FrameLength - 3) == 0);
    REQUIRE(hasValidChecksum(buffer, FrameLength + 1));

    REQUIRE(t.sendUntilAcknowledgeWindow() == FrameLength + 1);
    REQUIRE(t.bus.sendTelegramLen == FrameLength + 1);
    requireGuardUnchanged(buffer);
}

#if SBLIB_MAX_APDU_LENGTH > SBLIB_STANDARD_APDU_MAX_LENGTH

namespace
{
    /**
     * Build an extended frame 0.0.0 -> 0/0/100 (group address, hop count 6, EFF 0000b) without the checksum.
     *
     * @param frame      - Buffer for at least 8 + dataOctets octets
     * @param apduLength - The length octet
     * @param dataOctets - The number of octets after the TPCI octet
     * @return The length of the frame without the checksum
     */
    uint16_t buildExtendedFrame(uint8_t* frame, const uint8_t apduLength, const uint16_t dataOctets)
    {
        frame[0] = 0x3C;
        frame[1] = 0xE0;
        frame[2] = 0x00;
        frame[3] = 0x00;
        frame[4] = 0x00;
        frame[5] = 0x64;
        frame[6] = apduLength;
        frame[7] = 0x00;
        for (uint16_t i = 0; i < dataOctets; ++i)
        {
            frame[LpduExtendedHeaderLength + i] = static_cast<uint8_t>(i);
        }
        return LpduExtendedHeaderLength + dataOctets;
    }

} // namespace

TEST_CASE("Bus send: an extended frame gets the own address at octets 2 and 3", "[SBLIB][KNX][Bus]")
{
    BusTest t(OwnAddress, Layers);
    uint8_t buffer[BufferSize];
    memset(buffer, GuardValue, BufferSize);
    uint16_t length = 0;

    SECTION("APDU length 1")
    {
        length = buildExtendedFrame(buffer, 1, 1);
        REQUIRE(length == 9);
    }

    SECTION("APDU length SBLIB_MAX_APDU_LENGTH fills the buffer")
    {
        length = buildExtendedFrame(buffer, SBLIB_MAX_APDU_LENGTH, SBLIB_MAX_APDU_LENGTH);
        REQUIRE(length + 1 == TelegramBufferSize);
    }

    uint8_t frame[BufferSize];
    memcpy(frame, buffer, BufferSize);

    t.bus.sendTelegram(buffer, length);

    REQUIRE(t.callback.finishedCount == 0);
    REQUIRE(t.bus.sendCurTelegram == buffer);
    REQUIRE(buffer[0] == frame[0]);
    REQUIRE(buffer[1] == frame[1]);
    REQUIRE(buffer[2] == (OwnAddress >> 8));
    REQUIRE(buffer[3] == (OwnAddress & 0xff));
    REQUIRE(memcmp(&buffer[4], &frame[4], length - 4) == 0);
    REQUIRE(hasValidChecksum(buffer, length + 1));

    REQUIRE(t.sendUntilAcknowledgeWindow() == length + 1);
    REQUIRE(t.bus.sendTelegramLen == length + 1);
    requireGuardUnchanged(buffer);
}

TEST_CASE("Bus send: an extended frame at the buffer end is repeated with the repeat flag", "[SBLIB][KNX][Bus]")
{
    BusTest t(OwnAddress, Layers);
    uint8_t buffer[BufferSize];
    memset(buffer, GuardValue, BufferSize);
    const uint16_t length = buildExtendedFrame(buffer, SBLIB_MAX_APDU_LENGTH, SBLIB_MAX_APDU_LENGTH);
    REQUIRE((buffer[0] & SB_TEL_REPEAT_FLAG) != 0);

    t.bus.sendTelegram(buffer, length);
    REQUIRE(t.sendUntilAcknowledgeWindow() == TelegramBufferSize);
    const uint8_t checksum = buffer[length];

    // No acknowledge within the window: the frame is sent again, now marked as repeated
    t.timeout();
    t.timeout();
    REQUIRE(t.bus.repeatTelegram);
    REQUIRE(t.bus.state == Bus::WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE);
    t.timeout();
    REQUIRE(t.bus.state == Bus::SEND_START_BIT);
    REQUIRE((buffer[0] & SB_TEL_REPEAT_FLAG) == 0);
    REQUIRE(buffer[length] == (checksum ^ SB_TEL_REPEAT_FLAG));
    REQUIRE(hasValidChecksum(buffer, length + 1));

    REQUIRE(t.sendUntilAcknowledgeWindow() == TelegramBufferSize);
    REQUIRE(t.callback.finishedCount == 0);
    requireGuardUnchanged(buffer);
}

// With DEBUG, sendTelegram() calls fatalError() for a frame that does not fit, which does not return.
#   if !defined(DEBUG)
TEST_CASE("Bus send: a frame that does not fit into the buffer is rejected", "[SBLIB][KNX][Bus]")
{
    BusTest t(OwnAddress, Layers);
    uint8_t buffer[BufferSize];
    memset(buffer, GuardValue, BufferSize);
    uint16_t length = 0;

    SECTION("length + 1 above TelegramBufferSize")
    {
        // The header announces a frame that fits, the given length does not leave room for the checksum
        buildExtendedFrame(buffer, SBLIB_MAX_APDU_LENGTH, SBLIB_MAX_APDU_LENGTH);
        length = TelegramBufferSize;
    }

    SECTION("length in the header + 1 above TelegramBufferSize")
    {
        // The given length fits, the bus would send the 264 octets announced by the length octet 255
        length = buildExtendedFrame(buffer, 0xff, 1);
    }

    uint8_t frame[BufferSize];
    memcpy(frame, buffer, BufferSize);

    t.bus.sendTelegram(buffer, length);

    REQUIRE(t.callback.finishedCount == 1);
    REQUIRE_FALSE(t.callback.lastSuccessful);
    REQUIRE(t.bus.sendCurTelegram == nullptr);
    REQUIRE(t.bus.state == Bus::IDLE);
    REQUIRE(memcmp(buffer, frame, BufferSize) == 0);
}
#   endif

#endif
