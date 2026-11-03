/*
 *  test_bus_lf.cpp - Host tests of the reception of L_Data_Extended frames by the KNX TP1 bus
 *                    (sblib src/eib/bus.cpp) on the simulated bus line, bit level like test_bus_t9.cpp.
 *
 *  Built with SBLIB_MAX_APDU_LENGTH 15 (default, extended frames are ignored), 56 and 254.
 *  Expectations: KNX 3/2/2 2.2.5 (L_Data_Extended frame), 2.4.2 (no acknowledge for a frame above
 *  the receive capacity, never NACK or BUSY), 3/3/2 2.2.5.
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

#include <cstdint>
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

#else

TEST_CASE("bus extended frame: ignored with the default maximum APDU length 15", "[rp2350][bus][lf]")
{
    startBus();
    receive(extendedFrame(OwnAddress, false, 10));
    CHECK_FALSE(bus->telegramReceived());
    CHECK(decodeTx().empty());
}

#endif
