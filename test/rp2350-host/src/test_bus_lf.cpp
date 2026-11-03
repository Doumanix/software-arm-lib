/*
 *  test_bus_lf.cpp - Host tests of the reception and the sending of L_Data_Extended frames by the KNX
 *                    TP1 bus (sblib src/eib/bus.cpp) on the simulated bus line, bit level like test_bus_t9.cpp.
 *
 *  Built with SBLIB_MAX_APDU_LENGTH 15 (default, extended frames are ignored), 56 and 254.
 *  Expectations: KNX 3/2/2 2.2.5 (L_Data_Extended frame), 2.3 (timing), 2.4 (acknowledge, repetition
 *  with the repeat flag, collision; no acknowledge for a frame above the receive capacity, never NACK
 *  or BUSY), 3/3/2 2.2.5.
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include "bus_test_support.h"

#include "sblib/eib/bus.h"
#include "sblib/eib/bus_const.h"
#include "sblib/eib/bcu_const.h"
#include "sblib/eib/knx_lpdu.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <hardware/gpio.h> // simulation: sim::

#include <catch.hpp> // If possible, include catch.hpp as last header

using namespace bustest;

namespace
{

    constexpr uint8_t ExtendedControl = 0x3C; // frame type 0 (extended), not repeated, low priority
    constexpr uint8_t HopCount = 6;

    // L_Data_Extended frame of 1.1.2 with apduLength octets after the TPCI octet, with checksum
    std::vector<uint8_t> extendedFrame(const uint16_t destination, const bool group, const uint16_t apduLength,
                                       const uint8_t frameFormat = 0)
    {
        uint8_t ctrle = HopCount << 4 | frameFormat;
        if (group)
        {
            ctrle |= 0x80;
        }
        std::vector<uint8_t> tel = {ExtendedControl, ctrle, 0x11, 0x02,
                                    static_cast<uint8_t>(destination >> 8), static_cast<uint8_t>(destination & 0xff),
                                    static_cast<uint8_t>(apduLength), 0x00};
        for (uint16_t i = 0; i < apduLength; ++i)
        {
            tel.push_back(static_cast<uint8_t>(0x80 + i));
        }
        return withChecksum(tel);
    }

    // Another device sends the frame; run until the frame and a possible acknowledge are over
    void receive(const std::vector<uint8_t>& frame)
    {
        remoteSend(sim::now() + 1000, frame);
        sim::advance(frame.size() * CharBits * BitTime + 50000);
    }

} // namespace

#if SBLIB_MAX_APDU_LENGTH > 15

namespace
{

    // We sent exactly one LL_ACK
    bool acknowledged()
    {
        const auto chars = decodeTx();
        return chars.size() == 1 && chars[0].value == SB_BUS_ACK;
    }

    std::vector<uint8_t> received()
    {
        return std::vector<uint8_t>(bus->telegram, bus->telegram + bus->telegramLen);
    }

} // namespace

TEST_CASE("bus extended frame: to the own address is acknowledged and stored with its length", "[rp2350][bus][lf]")
{
    for (const uint16_t length: {20, 56, 100, 254})
    {
        if (length > SBLIB_MAX_APDU_LENGTH)
        {
            continue;
        }
        INFO("APDU length " << length);
        startBus();
        const auto frame = extendedFrame(OwnAddress, false, length);
        receive(frame);
        CHECK(acknowledged());
        REQUIRE(bus->telegramReceived());
        CHECK(bus->telegramLen == frame.size());
        CHECK(bus->telegramLen == 9 + length);
        CHECK(received() == frame);
    }
}

TEST_CASE("bus extended frame: group address of the address table is acknowledged, others not", "[rp2350][bus][lf]")
{
    SECTION("known group address")
    {
        startBus();
        receive(extendedFrame(GroupAddress, true, 20));
        CHECK(acknowledged());
        CHECK(bus->telegramReceived());
    }
    SECTION("unknown group address")
    {
        startBus();
        receive(extendedFrame(GroupAddress + 1, true, 20));
        CHECK_FALSE(bus->telegramReceived());
        CHECK(decodeTx().empty());
    }
    SECTION("broadcast")
    {
        startBus();
        receive(extendedFrame(0, true, 20));
        CHECK(acknowledged());
        CHECK(bus->telegramReceived());
    }
    SECTION("other physical address")
    {
        startBus();
        receive(extendedFrame(OwnAddress + 1, false, 20));
        CHECK_FALSE(bus->telegramReceived());
        CHECK(decodeTx().empty());
    }
}

#   if SBLIB_MAX_APDU_LENGTH < 254
TEST_CASE("bus extended frame: a frame above the buffer size is discarded without acknowledge", "[rp2350][bus][lf]")
{
    for (const uint16_t length: {SBLIB_MAX_APDU_LENGTH + 1, 254})
    {
        INFO("APDU length " << length);
        startBus();
        receive(extendedFrame(OwnAddress, false, length));
        CHECK_FALSE(bus->telegramReceived());
        CHECK(decodeTx().empty()); // no ACK, no NACK, no BUSY
        CHECK((bus->rx_error & RX_LENGTH_ERROR) != 0);
    }
}
#   endif

TEST_CASE("bus extended frame: invalid frames are discarded without any acknowledge", "[rp2350][bus][lf]")
{
    startBus();
    SECTION("wrong checksum (like a standard frame)")
    {
        auto frame = extendedFrame(OwnAddress, false, 20);
        frame.back() ^= 0x01;
        receive(frame);
    }
    SECTION("extended frame format not 0000b")
    {
        receive(extendedFrame(OwnAddress, false, 20, 0x01));
    }
    SECTION("length 255 (escape)")
    {
        auto frame = extendedFrame(OwnAddress, false, 20);
        frame[6] = 0xff;
        frame.pop_back();
        receive(withChecksum(frame));
    }
    SECTION("length field does not match the received octets")
    {
        auto frame = extendedFrame(OwnAddress, false, 20);
        frame[6] = 21;
        frame.pop_back();
        receive(withChecksum(frame));
    }
    CHECK_FALSE(bus->telegramReceived());
    CHECK(decodeTx().empty());
}

TEST_CASE("bus extended frame: a standard frame is received as before", "[rp2350][bus][lf]")
{
    startBus();
    const auto frame = withChecksum({0xB0, 0x11, 0x02, OwnAddress >> 8, OwnAddress & 0xff, 0x60, 0x80});
    receive(frame);
    CHECK(acknowledged());
    REQUIRE(bus->telegramReceived());
    CHECK(received() == frame);
}

// ------------------------------------------------------------------ sending

namespace
{

    // Larger than any TelegramBufferSize: a frame above the buffer size must not overflow the test buffer
    std::vector<uint8_t> extendedSendBuffer;

    // The L_Data_Extended frame we send to destination, as expected on the bus (own address, checksum)
    std::vector<uint8_t> ownExtendedFrame(const uint16_t destination, const bool group, const uint16_t apduLength)
    {
        auto frame = extendedFrame(destination, group, apduLength);
        frame.pop_back();
        frame[2] = OwnAddress >> 8;
        frame[3] = OwnAddress & 0xff;
        return withChecksum(frame);
    }

    // Build the frame with the LPDU helpers and queue it with Bus::sendTelegram(), returns the expected bytes
    std::vector<uint8_t> sendExtended(const uint16_t destination, const bool group, const uint16_t apduLength)
    {
        extendedSendBuffer.assign(300, 0);
        uint8_t* tel = extendedSendBuffer.data();
        const uint8_t tpci = lpduInitExtendedFrame(tel, PRIORITY_LOW, 0, destination, group, HopCount,
                                                   static_cast<uint8_t>(apduLength));
        tel[tpci] = 0x00;
        for (uint16_t i = 0; i < apduLength; ++i)
        {
            tel[tpci + 1 + i] = static_cast<uint8_t>(0x80 + i);
        }
        bus->sendTelegram(tel, static_cast<uint16_t>(tpci + 1 + apduLength));
        return ownExtendedFrame(destination, group, apduLength);
    }

    // The frame with the repeat flag cleared (repeated frame) and the checksum corrected
    std::vector<uint8_t> repeatedFrame(std::vector<uint8_t> frame)
    {
        frame[0] &= ~SB_TEL_REPEAT_FLAG;
        frame.pop_back();
        return withChecksum(frame);
    }

    // Maximum time to send a frame of this size
    uint64_t frameUs(const std::vector<uint8_t>& frame)
    {
        return frame.size() * CharTimeUs + 20000;
    }

    // Our frame starting at character first was answered by the peer in the acknowledge slot: time from
    // the end of the stop bit of the reply to the start of our next frame
    int64_t gapAfterReply(const std::vector<SentChar>& chars, const size_t first, const size_t frameSize)
    {
        const uint64_t replyEndUs = endOfStopBit(endOfStopBit(chars[first + frameSize - 1].startUs) + AckDelayUs);
        return static_cast<int64_t>(chars[first + frameSize].startUs) - static_cast<int64_t>(replyEndUs);
    }

    // Random extra delay before sending plus the end of frame detection (as test_bus_t9.cpp)
    constexpr int64_t GapMarginUs = RANDOMIZE_MODULUS + MAX_INTER_CHAR_TIME + 10;
    constexpr int64_t GapEarlyUs = 5;

    // Two shorter frames and the longest frame of this build
    std::vector<uint16_t> sendLengths()
    {
        std::vector<uint16_t> lengths;
        for (const uint16_t length: {20, 100, 254})
        {
            if (length <= SBLIB_MAX_APDU_LENGTH)
            {
                lengths.push_back(length);
            }
        }
        if (lengths.back() != SBLIB_MAX_APDU_LENGTH)
        {
            lengths.push_back(SBLIB_MAX_APDU_LENGTH);
        }
        return lengths;
    }

} // namespace

TEST_CASE("bus extended frame send: bytes and timing as built, the acknowledge finishes", "[rp2350][bus][lf]")
{
    for (const uint16_t length: sendLengths())
    {
        DYNAMIC_SECTION("APDU length " << length)
        {
            startBus();
            const auto expected = sendExtended(GroupAddress, true, length);
            REQUIRE(expected.size() == 9u + length);
            auto chars = runUntilSent(expected.size(), frameUs(expected));
            REQUIRE(chars.size() == expected.size());
            CHECK(bus->sendingFrame());

            // CTRL, CTRLE, SA = own address, DA, LG, TPCI, data, FCS; 13 bit times per character,
            // 104 us bit raster and 35 us pulses up to the last character
            checkFrame(chars, 0, expected);
            CHECK(chars[0].value == 0x3C);
            CHECK(chars[1].value == (0x80 | HopCount << 4));
            CHECK(chars[2].value == OwnAddress >> 8);
            CHECK(chars[3].value == (OwnAddress & 0xff));
            CHECK(chars[6].value == length);

            // Acknowledge in the acknowledge slot 15 bit times after the frame
            remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {SB_BUS_ACK});
            sim::advance(50000);
            chars = decodeTx();
            CHECK(chars.size() == expected.size()); // sent once
            CHECK(callback.finished == 1);
            CHECK(callback.lastSuccessful);
            CHECK_FALSE(bus->sendingFrame());
        }
    }
}

TEST_CASE("bus extended frame send: without acknowledge repeated like a standard frame, then failed",
          "[rp2350][bus][lf]")
{
    startBus();
    const auto expected = sendExtended(OwnAddress + 1, false, SBLIB_MAX_APDU_LENGTH);
    sim::advance((NACK_RETRY_DEFAULT + 1) * frameUs(expected) + 100000);

    const auto again = repeatedFrame(expected);
    const auto chars = decodeTx();
    REQUIRE(chars.size() == (NACK_RETRY_DEFAULT + 1) * expected.size());
    checkFrame(chars, 0, expected);
    for (size_t k = 1; k <= NACK_RETRY_DEFAULT; ++k)
    {
        INFO("repetition " << k);
        const size_t first = k * expected.size();
        checkFrame(chars, first, again); // repeat flag cleared, checksum corrected
        // at least 50 bit times after the frame (as the standard frame in test_bus.cpp)
        CHECK(chars[first].startUs - endOfStopBit(chars[first - 1].startUs) >= 50 * BitTime);
    }
    CHECK(callback.finished == 1);
    CHECK_FALSE(callback.lastSuccessful);
    CHECK_FALSE(bus->sendingFrame());
}

TEST_CASE("bus extended frame send: NACK and BUSY repeat like a standard frame", "[rp2350][bus][lf]")
{
    for (const uint8_t reply: {SB_BUS_NACK, SB_BUS_BUSY})
    {
        DYNAMIC_SECTION("reply 0x" << std::hex << static_cast<int>(reply))
        {
            startBus();
            const auto expected = sendExtended(GroupAddress, true, SBLIB_MAX_APDU_LENGTH);
            auto chars = runUntilSent(expected.size(), frameUs(expected));
            REQUIRE(chars.size() == expected.size());
            remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {reply});

            chars = runUntilSent(2 * expected.size(), frameUs(expected) + 50000);
            REQUIRE(chars.size() == 2 * expected.size());
            checkFrame(chars, expected.size(), repeatedFrame(expected));
            int64_t wait = SEND_WAIT_TIME; // NACK: 50 bit times
            if (reply == SB_BUS_BUSY)
            {
                wait = BUSY_WAIT_150BIT; // BUSY: 150 bit times
            }
            const auto gap = gapAfterReply(chars, 0, expected.size());
            CHECK(gap >= wait - GapEarlyUs);
            CHECK(gap <= wait + GapMarginUs);
            CHECK(callback.finished == 0);

            remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {SB_BUS_ACK});
            sim::advance(50000);
            CHECK(decodeTx().size() == 2 * expected.size());
            CHECK(callback.finished == 1);
            CHECK(callback.lastSuccessful);
            CHECK_FALSE(bus->sendingFrame());
        }
    }
}

TEST_CASE("bus extended frame send: a collision in the long part stops sending, the frame is sent again",
          "[rp2350][bus][lf]")
{
    // The peer (a second device with our address) sends the same frame up to octet 30 (behind the 23 octets
    // of a standard frame), there its data octet has a 0 bit in place of our lowest 1 bit. It sends each
    // character at the start of ours: remoteSend() uses 1352 us per character, bus.cpp 1353 us
    // (CharTimeUs), which would drift apart by 30 us up to octet 30.
    const uint16_t length = SBLIB_MAX_APDU_LENGTH;
    constexpr size_t CollisionOctet = 30;

    startBus();
    auto expected = sendExtended(GroupAddress, true, length);
    sim::advance(20000);
    const auto first = decodeTx();
    REQUIRE_FALSE(first.empty());
    const uint64_t startUs = first.front().startUs; // the simulation is deterministic

    auto remote = expected;
    remote.pop_back();
    remote[CollisionOctet] = static_cast<uint8_t>(remote[CollisionOctet] & (remote[CollisionOctet] - 1));
    remote = withChecksum(remote);

    startBus();
    expected = sendExtended(GroupAddress, true, length);
    for (size_t n = 0; n < remote.size(); ++n)
    {
        remoteSend(startUs + n * CharTimeUs, {remote[n]});
    }
    const auto chars = runUntilSent(CollisionOctet + 2 + expected.size(), 2 * frameUs(expected) + 50000);

    // We received the peer's frame and acknowledged it
    REQUIRE(bus->telegramReceived());
    CHECK(std::vector<uint8_t>(bus->telegram, bus->telegram + bus->telegramLen) == remote);
    REQUIRE(chars.size() == CollisionOctet + 2 + expected.size());
    for (size_t n = 0; n < CollisionOctet; ++n)
    {
        INFO("character " << n);
        CHECK(chars[n].value == expected[n]);
    }
    CHECK(chars[CollisionOctet + 1].value == SB_BUS_ACK);

    // Sent again after 50 bit times, the repeat flag is not changed by a collision (KNX 3/2/2 2.4.1)
    const size_t again = CollisionOctet + 2;
    checkFrame(chars, again, expected);
    CHECK(chars[again].startUs >= endOfStopBit(chars[again - 1].startUs) + 50 * BitTime);

    remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {SB_BUS_ACK});
    sim::advance(50000);
    CHECK(callback.finished == 1);
    CHECK(callback.lastSuccessful);
    CHECK_FALSE(bus->sendingFrame());
}

#   if SBLIB_MAX_APDU_LENGTH < 254
TEST_CASE("bus extended frame send: a frame above the buffer size is not sent, the error is reported",
          "[rp2350][bus][lf]")
{
    startBus();
#      if defined(DEBUG)
    CHECK_THROWS_AS(sendExtended(GroupAddress, true, SBLIB_MAX_APDU_LENGTH + 1), std::runtime_error);
#      else
    sendExtended(GroupAddress, true, SBLIB_MAX_APDU_LENGTH + 1);
    CHECK(callback.finished == 1);
    CHECK_FALSE(callback.lastSuccessful);
#      endif
    CHECK_FALSE(bus->sendingFrame());
    sim::advance(100000);
    CHECK(decodeTx().empty());
}
#   endif

TEST_CASE("bus extended frame send: a standard frame is sent as before", "[rp2350][bus][lf]")
{
    startBus();
    const auto expected = send(groupWrite());
    const auto chars = runUntilSent(expected.size());
    REQUIRE(chars.size() == expected.size());
    checkFrame(chars, 0, expected);
    CHECK(chars[1].value == OwnAddress >> 8);
    CHECK(chars[5].value == 0xE1);
    remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {SB_BUS_ACK});
    sim::advance(50000);
    CHECK(callback.finished == 1);
    CHECK(callback.lastSuccessful);
}

#else

TEST_CASE("bus extended frame: ignored with the default maximum APDU length 15", "[rp2350][bus][lf]")
{
    startBus();
    receive(extendedFrame(OwnAddress, false, 10));
    CHECK_FALSE(bus->telegramReceived());
    CHECK(decodeTx().empty());
}

#endif
