/*
 *  test_knx_lpdu_frames.cpp - Unit tests of the LPDU helpers for standard and extended frames
 *                             (knx_lpdu.h, KNX 3/2/2 2.2.4 and 2.2.5).
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <sblib/eib/knx_lpdu.h>
#include <sblib/eib/knx_npdu.h>

#include <cstdint>

#include <catch.hpp> // If possible, include catch.hpp as last header

namespace
{

    // Standard frame 1.1.2 -> 0/0/100, group, hop count 6, APDU length 1 (GroupValueWrite 1)
    constexpr uint8_t standardGroup[] = {0xBC, 0x11, 0x02, 0x00, 0x64, 0xE1, 0x00, 0x81};
    // Standard frame 1.1.2 -> 1.1.101, physical, hop count 5, APDU length 15
    constexpr uint8_t standardPhysical[] = {0xB0, 0x11, 0x02, 0x11, 0x65, 0x5F, 0x42};
    // Extended frame 1.1.2 -> 0/0/100, group, hop count 6, EFF 0, APDU length 200
    constexpr uint8_t extendedGroup[] = {0x3C, 0xE0, 0x11, 0x02, 0x00, 0x64, 200, 0x00};
    // Extended frame 1.1.2 -> 1.1.101, physical, hop count 3, EFF 0101b, APDU length 254
    constexpr uint8_t extendedPhysical[] = {0x30, 0x35, 0x11, 0x02, 0x11, 0x65, 254, 0x42};

} // namespace

TEST_CASE("LPDU helpers: standard frame", "[SBLIB][KNX][LPDU]")
{
    CHECK_FALSE(isExtendedFrame(standardGroup));
    CHECK(lpduTpciIndex(standardGroup) == 6);
    CHECK(lpduApduLength(standardGroup) == 1);
    CHECK(standardFrameApduLength(standardGroup) == 1);
    CHECK(lpduLength(standardGroup) == 8);
    CHECK(standardFrameLength(standardGroup) == 8);
    CHECK(lpduSenderAddress(standardGroup) == 0x1102);
    CHECK(lpduDestinationAddress(standardGroup) == 0x0064);
    CHECK(lpduIsGroupAddress(standardGroup));
    CHECK(lpduHopCount(standardGroup) == 6);
    CHECK(lpduExtendedFrameFormat(standardGroup) == 0);

    CHECK(lpduApduLength(standardPhysical) == 15);
    CHECK(lpduLength(standardPhysical) == 22);
    CHECK(lpduDestinationAddress(standardPhysical) == 0x1165);
    CHECK_FALSE(lpduIsGroupAddress(standardPhysical));
    CHECK(lpduHopCount(standardPhysical) == 5);
}

TEST_CASE("LPDU helpers: standard frame values equal the existing functions", "[SBLIB][KNX][LPDU]")
{
    uint8_t telegram[8] = {};
    for (int ctrl = 0x80; ctrl <= 0xff; ++ctrl)
    {
        for (int npci = 0; npci <= 0xff; npci += 0x0b)
        {
            telegram[0] = static_cast<uint8_t>(ctrl);
            telegram[1] = 0x12;
            telegram[2] = 0x34;
            telegram[3] = 0x56;
            telegram[4] = 0x78;
            telegram[5] = static_cast<uint8_t>(npci);
            INFO("ctrl " << ctrl << " npci " << npci);
            CHECK(frameType(telegram) == FRAME_STANDARD);
            CHECK(lpduSenderAddress(telegram) == senderAddress(telegram));
            CHECK(lpduDestinationAddress(telegram) == destinationAddress(telegram));
            CHECK(lpduLength(telegram) == 7 + (telegram[5] & 15));
            CHECK(lpduIsGroupAddress(telegram) == ((telegram[5] & 0x80) != 0));
        }
    }
}

TEST_CASE("LPDU helpers: extended frame", "[SBLIB][KNX][LPDU]")
{
    CHECK(isExtendedFrame(extendedGroup));
    CHECK(frameType(extendedGroup) == FRAME_EXTENDED);
    CHECK(lpduTpciIndex(extendedGroup) == 7);
    CHECK(lpduApduLength(extendedGroup) == 200);
    CHECK(extendedFrameApduLength(extendedGroup) == 200);
    CHECK(lpduLength(extendedGroup) == 208);
    CHECK(extendedFrameLength(extendedGroup) == 208);
    CHECK(lpduSenderAddress(extendedGroup) == 0x1102);
    CHECK(lpduDestinationAddress(extendedGroup) == 0x0064);
    CHECK(lpduIsGroupAddress(extendedGroup));
    CHECK(lpduHopCount(extendedGroup) == 6);
    CHECK(lpduExtendedFrameFormat(extendedGroup) == 0);

    CHECK(lpduApduLength(extendedPhysical) == 254);
    CHECK(lpduLength(extendedPhysical) == 262);
    CHECK(lpduDestinationAddress(extendedPhysical) == 0x1165);
    CHECK_FALSE(lpduIsGroupAddress(extendedPhysical));
    CHECK(lpduHopCount(extendedPhysical) == 3);
    CHECK(lpduExtendedFrameFormat(extendedPhysical) == 0x05);
}

TEST_CASE("LPDU helpers: telegramSize", "[SBLIB][KNX][LPDU]")
{
    CHECK(telegramSize(standardGroup) == 8);
    CHECK(telegramSize(standardPhysical) == 22);
#if SBLIB_MAX_APDU_LENGTH > 15
    CHECK(telegramSize(extendedGroup) == 208);
#endif
}

TEST_CASE("LPDU helpers: build a standard frame header", "[SBLIB][KNX][LPDU]")
{
    uint8_t telegram[8] = {};
    CHECK(lpduInitStandardFrame(telegram, PRIORITY_LOW, 0x1102, 0x0064, true, 6, 1) == 6);
    telegram[6] = 0x00;
    telegram[7] = 0x81;
    for (size_t i = 0; i < sizeof(standardGroup); ++i)
    {
        INFO("octet " << i);
        CHECK(telegram[i] == standardGroup[i]);
    }

    // Same control field as initLpdu() of the existing code
    uint8_t reference[8] = {};
    initLpdu(reference, PRIORITY_SYSTEM, false, FRAME_STANDARD);
    CHECK(lpduInitStandardFrame(telegram, PRIORITY_SYSTEM, 0x1102, 0x1165, false, 5, 15) == 6);
    CHECK(telegram[0] == reference[0]);
    CHECK(telegram[5] == 0x5F);
    CHECK(lpduLength(telegram) == 22);
    CHECK_FALSE(lpduIsGroupAddress(telegram));

    // Setters, the length keeps address type and hop count
    lpduSetSenderAddress(telegram, 0x1165);
    lpduSetDestinationAddress(telegram, 0x0801);
    lpduSetGroupAddress(telegram, true);
    lpduSetApduLength(telegram, 3);
    CHECK(senderAddress(telegram) == 0x1165);
    CHECK(destinationAddress(telegram) == 0x0801);
    CHECK(telegram[5] == 0xD3);
    lpduSetGroupAddress(telegram, false);
    CHECK(telegram[5] == 0x53);
    lpduSetApduLength(telegram, 0x1f); // only 4 bits in a standard frame
    CHECK(telegram[5] == 0x5F);
}

TEST_CASE("LPDU helpers: build an extended frame header", "[SBLIB][KNX][LPDU]")
{
    uint8_t telegram[8] = {};
    CHECK(lpduInitExtendedFrame(telegram, PRIORITY_LOW, 0x1102, 0x0064, true, 6, 200) == 7);
    telegram[7] = 0x00;
    for (size_t i = 0; i < sizeof(extendedGroup); ++i)
    {
        INFO("octet " << i);
        CHECK(telegram[i] == extendedGroup[i]);
    }
    CHECK(frameType(telegram) == FRAME_EXTENDED);
    CHECK_FALSE(isRepeated(telegram));
    CHECK(priority(telegram) == PRIORITY_LOW);

    CHECK(lpduInitExtendedFrame(telegram, PRIORITY_SYSTEM, 0, 0x1165, false, 3, 254) == 7);
    CHECK(telegram[0] == 0x30);
    CHECK(telegram[1] == 0x30); // address type 0, hop count 3, extended frame format 0000b
    CHECK(lpduExtendedFrameFormat(telegram) == 0);
    CHECK(lpduDestinationAddress(telegram) == 0x1165);
    CHECK(lpduLength(telegram) == 262);

    // Setters write octets 2/3 (source), 4/5 (destination), CTRLE (address type) and 6 (length)
    lpduSetSenderAddress(telegram, 0x1165);
    CHECK(telegram[2] == 0x11);
    CHECK(telegram[3] == 0x65);
    CHECK(telegram[1] == 0x30);
    lpduSetDestinationAddress(telegram, 0x0801);
    CHECK(telegram[4] == 0x08);
    CHECK(telegram[5] == 0x01);
    lpduSetGroupAddress(telegram, true);
    CHECK(telegram[1] == 0xB0);
    CHECK(lpduIsGroupAddress(telegram));
    lpduSetApduLength(telegram, 20);
    CHECK(telegram[6] == 20);
    CHECK(telegram[1] == 0xB0);
    CHECK(lpduSenderAddress(telegram) == 0x1165);
    CHECK(lpduDestinationAddress(telegram) == 0x0801);
}
