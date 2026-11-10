/**
 * Unit tests of the frame check of Bus when receiving standard and
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
    constexpr uint16_t OwnAddress = 0x1165;    // 1.1.101
    constexpr uint16_t RemoteAddress = 0x1102; // 1.1.2

    constexpr uint8_t LayersWithTransportLayer = BCU_STATUS_LINK_LAYER | BCU_STATUS_TRANSPORT_LAYER;
    constexpr uint8_t LayersWithoutTransportLayer = BCU_STATUS_LINK_LAYER;

    /**
     * Build a standard frame 1.1.2 -> 1.1.101 (physical address, hop count 6) with the checksum.
     *
     * @param frame       - Buffer for at least 8 + apduLength octets
     * @param apduLength  - The APDU length in the header (0..15)
     * @param dataOctets  - The number of octets after the TPCI octet
     * @return The number of octets including the checksum
     */
    uint16_t buildStandardFrame(uint8_t* frame, const uint8_t apduLength, const uint8_t dataOctets)
    {
        frame[0] = 0xBC;
        frame[1] = RemoteAddress >> 8;
        frame[2] = RemoteAddress & 0xff;
        frame[3] = OwnAddress >> 8;
        frame[4] = OwnAddress & 0xff;
        frame[5] = 0x60 | apduLength;
        frame[6] = 0x00;
        for (uint8_t i = 0; i < dataOctets; ++i)
        {
            frame[LpduStandardHeaderLength + i] = static_cast<uint8_t>(0x80 + i);
        }
        const uint16_t length = LpduStandardHeaderLength + dataOctets;
        setFrameChecksum(frame, length);
        return length + 1;
    }

    void requireStoredAndAcknowledged(const BusTest& t, const uint8_t* frame, const uint16_t count)
    {
        REQUIRE(t.bus.telegramLen == count);
        REQUIRE(memcmp(t.bus.telegram, frame, count) == 0);
        REQUIRE(t.bus.sendAck == SB_BUS_ACK);
        REQUIRE(t.bus.state == Bus::RECV_WAIT_FOR_ACK_TX_START);
    }

} // namespace

TEST_CASE("Bus receive: a standard frame is stored and acknowledged", "[SBLIB][KNX][Bus]")
{
    BusTest t(OwnAddress, LayersWithTransportLayer);
    uint8_t frame[TelegramBufferSize + 1] = {};

    SECTION("APDU length 1")
    {
        const uint16_t count = buildStandardFrame(frame, 1, 1);
        REQUIRE(count == 9);
        t.receive(frame, count);
        requireStoredAndAcknowledged(t, frame, count);
        REQUIRE(t.bus.rx_error == RX_OK);
    }

    SECTION("APDU length 15, 23 octets")
    {
        const uint16_t count = buildStandardFrame(frame, 15, 15);
        REQUIRE(count == STANDARD_FRAME_MAX_SIZE);
        t.receive(frame, count);
        requireStoredAndAcknowledged(t, frame, count);
        REQUIRE(t.bus.rx_error == RX_OK);
    }
}

#if SBLIB_MAX_APDU_LENGTH > SBLIB_STANDARD_APDU_MAX_LENGTH

namespace
{
    /**
     * Build an extended frame 1.1.2 -> 1.1.101 (physical address, hop count 6, EFF 0000b) with the checksum.
     *
     * @param frame      - Buffer for at least 9 + dataOctets octets
     * @param apduLength - The length octet
     * @param dataOctets - The number of octets after the TPCI octet
     * @return The number of octets including the checksum
     */
    uint16_t buildExtendedFrame(uint8_t* frame, const uint8_t apduLength, const uint16_t dataOctets)
    {
        frame[0] = 0x3C;
        frame[1] = 0x60;
        frame[2] = RemoteAddress >> 8;
        frame[3] = RemoteAddress & 0xff;
        frame[4] = OwnAddress >> 8;
        frame[5] = OwnAddress & 0xff;
        frame[6] = apduLength;
        frame[7] = 0x00;
        for (uint16_t i = 0; i < dataOctets; ++i)
        {
            frame[LpduExtendedHeaderLength + i] = static_cast<uint8_t>(i);
        }
        const uint16_t length = LpduExtendedHeaderLength + dataOctets;
        setFrameChecksum(frame, length);
        return length + 1;
    }

    void requireDiscardedWithoutAcknowledge(const BusTest& t)
    {
        REQUIRE(t.bus.telegramLen == 0);
        REQUIRE(t.bus.sendAck == 0);
        REQUIRE(t.bus.state == Bus::WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE);
        REQUIRE((t.bus.rx_error & RX_INVALID_TELEGRAM_ERROR) != 0);
    }

} // namespace

TEST_CASE("Bus receive: a standard frame above 23 octets is discarded without acknowledge", "[SBLIB][KNX][Bus]")
{
    BusTest t(OwnAddress, LayersWithTransportLayer);
    uint8_t frame[TelegramBufferSize + 1] = {};

    const uint16_t count = buildStandardFrame(frame, 15, 16);
    REQUIRE(count == STANDARD_FRAME_MAX_SIZE + 1);
    t.receive(frame, count);
    requireDiscardedWithoutAcknowledge(t);
}

TEST_CASE("Bus receive: an extended frame with disabled transport layer is stored and acknowledged",
          "[SBLIB][KNX][Bus]")
{
    BusTest t(OwnAddress, LayersWithoutTransportLayer);
    uint8_t frame[TelegramBufferSize + 1] = {};

    SECTION("APDU length 1")
    {
        const uint16_t count = buildExtendedFrame(frame, 1, 1);
        REQUIRE(count == 10);
        t.receive(frame, count);
        requireStoredAndAcknowledged(t, frame, count);
    }

    SECTION("APDU length 16")
    {
        const uint16_t count = buildExtendedFrame(frame, 16, 16);
        t.receive(frame, count);
        requireStoredAndAcknowledged(t, frame, count);
    }

    SECTION("APDU length SBLIB_MAX_APDU_LENGTH fills the buffer")
    {
        const uint16_t count = buildExtendedFrame(frame, SBLIB_MAX_APDU_LENGTH, SBLIB_MAX_APDU_LENGTH);
        REQUIRE(count == TelegramBufferSize);
        t.receive(frame, count);
        requireStoredAndAcknowledged(t, frame, count);
    }

    REQUIRE(t.bus.rx_error == RX_OK);
    REQUIRE(isExtendedFrame(t.bus.telegram));
    REQUIRE(lpduSenderAddress(t.bus.telegram) == RemoteAddress);
    REQUIRE(lpduDestinationAddress(t.bus.telegram) == OwnAddress);
    REQUIRE_FALSE(lpduIsGroupAddress(t.bus.telegram));
    REQUIRE(t.bus.telegramLen == extendedFrameLength(t.bus.telegram) + 1);
}

TEST_CASE("Bus receive: an invalid extended frame is discarded without acknowledge", "[SBLIB][KNX][Bus]")
{
    BusTest t(OwnAddress, LayersWithoutTransportLayer);
    uint8_t frame[TelegramBufferSize + 1] = {};
    uint16_t count = 0;

    SECTION("extended frame format not 0000b")
    {
        count = buildExtendedFrame(frame, 1, 1);
        frame[1] = 0x61;
        setFrameChecksum(frame, count - 1);
    }

    SECTION("length octet 255")
    {
        // The frame would need 264 octets, the buffer holds 263
        count = buildExtendedFrame(frame, 0xff, SBLIB_MAX_APDU_LENGTH);
    }

    SECTION("length octet above the received octets")
    {
        count = buildExtendedFrame(frame, 10, 9);
    }

    SECTION("length octet below the received octets")
    {
        count = buildExtendedFrame(frame, 8, 9);
    }

    SECTION("more octets than TelegramBufferSize")
    {
        // A valid frame that fills the buffer and one more octet, which is not stored
        count = buildExtendedFrame(frame, SBLIB_MAX_APDU_LENGTH, SBLIB_MAX_APDU_LENGTH);
        REQUIRE(count == TelegramBufferSize);
        frame[count++] = 0x55;
    }

    t.receive(frame, count);
    requireDiscardedWithoutAcknowledge(t);
}

TEST_CASE("Bus receive: an extended frame with enabled transport layer is discarded without acknowledge",
          "[SBLIB][KNX][Bus]")
{
    // The transport layer handles standard frames only
    BusTest t(OwnAddress, LayersWithTransportLayer);
    uint8_t frame[TelegramBufferSize + 1] = {};

    const uint16_t count = buildExtendedFrame(frame, 1, 1);
    t.receive(frame, count);
    requireDiscardedWithoutAcknowledge(t);
}

#endif
