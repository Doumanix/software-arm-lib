/*
 * Host tests of the unchanged RP flash implementation.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "flash_sim.h"
#include <rp2350/iap.cpp>
#include <algorithm>
#include <array>
#include <catch.hpp>

namespace
{
    struct FlashFixture
    {
        alignas(4) std::array<uint8_t, 8192> data;
        FlashFixture()
        {
            sim::reset();
            sim::resetFlash();
            data.fill(0x5a);
        }
        uint8_t* start = FLASH + 8192;
        uint8_t* end = FLASH + 16384;
    };
} // namespace
TEST_CASE_METHOD(FlashFixture, "IAP storage boundaries and whole range", "[iap]")
{
    REQUIRE(iapProgram(start, data.data(), 256) == IAP_SUCCESS);
    REQUIRE(iapProgram(end - 256, data.data(), 256) == IAP_SUCCESS);
    REQUIRE(iapProgram(start - 256, data.data(), 256) == IAP_DST_ADDR_NOT_MAPPED);
    REQUIRE(iapProgram(end, data.data(), 256) == IAP_DST_ADDR_NOT_MAPPED);
    REQUIRE(iapProgram(end - 256, data.data(), 512) == IAP_DST_ADDR_NOT_MAPPED);
    REQUIRE(iapEraseSectorRange(2, 3) == IAP_SUCCESS);
    REQUIRE(iapProgram(start, data.data(), data.size()) == IAP_SUCCESS);
    REQUIRE(std::equal(data.begin(), data.end(), start));
    REQUIRE(sim::programCalls == 3);
}
TEST_CASE_METHOD(FlashFixture, "IAP rejects alignment count and flash source", "[iap]")
{
    REQUIRE(iapProgram(start + 1, data.data(), 256) == IAP_DST_ADDR_ERROR);
    REQUIRE(iapProgram(start, data.data() + 1, 256) == IAP_SRC_ADDR_ERROR);
    REQUIRE(iapProgram(start, data.data(), 0) == IAP_COUNT_ERROR);
    REQUIRE(iapProgram(start, data.data(), 257) == IAP_COUNT_ERROR);
    REQUIRE(iapProgram(start, FLASH, 256) == IAP_SRC_ADDR_ERROR);
    REQUIRE(iapProgram(start, end - 256, 256) == IAP_SRC_ADDR_ERROR);
    REQUIRE(sim::programCalls == 0);
    REQUIRE(sim::interruptsEnabled());
}
TEST_CASE_METHOD(FlashFixture, "IAP erase boundaries reject firmware and reversed ranges", "[iap]")
{
    REQUIRE(iapEraseSector(1) == IAP_INVALID_SECTOR);
    REQUIRE(iapEraseSector(4) == IAP_INVALID_SECTOR);
    REQUIRE(iapEraseSectorRange(3, 2) == IAP_INVALID_SECTOR);
    REQUIRE(iapEraseSectorRange(2, 4) == IAP_INVALID_SECTOR);
    REQUIRE(iapErasePage(31) == IAP_INVALID_SECTOR);
    REQUIRE(iapErasePage(64) == IAP_INVALID_SECTOR);
    REQUIRE(iapErasePageRange(33, 32) == IAP_INVALID_SECTOR);
    REQUIRE(sim::eraseCalls == 0);
}
TEST_CASE_METHOD(FlashFixture, "IAP page RMW preserves every neighbour across two sectors", "[iap]")
{
    for (size_t i = 0; i < data.size(); ++i)
    {
        data[i] = static_cast<uint8_t>(i / 256);
    }
    REQUIRE(iapProgram(start, data.data(), data.size()) == IAP_SUCCESS);
    REQUIRE(iapErasePageRange(47, 48) == IAP_SUCCESS);
    auto expected = data;
    std::fill(expected.begin() + 15 * 256, expected.begin() + 17 * 256, 0xff);
    REQUIRE(std::equal(expected.begin(), expected.end(), start));
    REQUIRE(sim::eraseCalls == 2);
    REQUIRE(sim::programCalls == 31);
    REQUIRE(iapErasePage(32) == IAP_SUCCESS);
    std::fill(expected.begin(), expected.begin() + 256, 0xff);
    REQUIRE(std::equal(expected.begin(), expected.end(), start));
    REQUIRE(iapErasePage(63) == IAP_SUCCESS);
    std::fill(expected.end() - 256, expected.end(), 0xff);
    REQUIRE(std::equal(expected.begin(), expected.end(), start));
}
TEST_CASE_METHOD(FlashFixture, "IAP reports erase and program verification failures", "[iap]")
{
    sim::failProgram = true;
    REQUIRE(iapProgram(start, data.data(), 256) == IAP_COMPARE_ERROR);
    REQUIRE(sim::interruptsEnabled());
    sim::failErase = true;
    REQUIRE(iapEraseSector(2) == IAP_SECTOR_NOT_BLANK);
    REQUIRE(iapErasePage(32) == IAP_SECTOR_NOT_BLANK);
    REQUIRE(sim::interruptsEnabled());
    sim::failErase = false;
    REQUIRE(iapErasePage(32) == IAP_SUCCESS);
    REQUIRE(iapProgram(start + 256, data.data(), 256) == IAP_COMPARE_ERROR);
    REQUIRE(iapErasePage(32) == IAP_COMPARE_ERROR);
}
TEST_CASE_METHOD(FlashFixture, "IAP preserves enabled and disabled IRQ state including failures", "[iap]")
{
    for (const bool enabled: {true, false})
    {
        sim::resetFlash();
        if (!enabled)
        {
            disable_interrupts();
        }
        REQUIRE(iapEraseSector(2) == IAP_SUCCESS);
        REQUIRE(sim::interruptsEnabled() == enabled);
        REQUIRE(iapProgram(start, data.data(), 256) == IAP_SUCCESS);
        REQUIRE(sim::interruptsEnabled() == enabled);
        REQUIRE(iapErasePage(33) == IAP_SUCCESS);
        REQUIRE(sim::interruptsEnabled() == enabled);
        sim::failProgram = true;
        REQUIRE(iapProgram(start, data.data(), 256) == IAP_COMPARE_ERROR);
        REQUIRE(sim::interruptsEnabled() == enabled);
        sim::failErase = true;
        REQUIRE(iapEraseSector(2) == IAP_SECTOR_NOT_BLANK);
        REQUIRE(sim::interruptsEnabled() == enabled);
    }
}
TEST_CASE_METHOD(FlashFixture, "IAP UserEeprom erase then program replaces old data", "[iap]")
{
    REQUIRE(iapEraseSector(2) == IAP_SUCCESS);
    data.fill(0);
    for (uint32_t offset = 0; offset < 4096; offset += 1024)
    {
        REQUIRE(iapProgram(start + offset, data.data() + offset, 1024) == IAP_SUCCESS);
    }
    data.fill(0xa5);
    REQUIRE(iapProgram(start, data.data(), 4096) == IAP_COMPARE_ERROR);
    REQUIRE(start[0] == 0); // the flash model cannot program zero bits back to one
    REQUIRE(iapEraseSector(2) == IAP_SUCCESS);
    for (uint32_t offset = 0; offset < 4096; offset += 1024)
    {
        REQUIRE(iapProgram(start + offset, data.data() + offset, 1024) == IAP_SUCCESS);
    }
    REQUIRE(std::equal(data.begin(), data.begin() + 4096, start));
    REQUIRE(std::all_of(FLASH, start, [](const uint8_t v) { return v == 0xff; }));
}
TEST_CASE_METHOD(FlashFixture, "IAP address conversions and device identity", "[iap]")
{
    REQUIRE(iapFlashSize() == 16384);
    REQUIRE(iapAddressOfSector(2) == start);
    REQUIRE(iapAddressOfPage(63) == end - 256);
    REQUIRE(iapSectorOfAddress(end - 1) == 3);
    REQUIRE(iapPageOfAddress(end - 1) == 63);
    unsigned partId = 0;
    REQUIRE(iapReadPartID(&partId) == IAP_SUCCESS);
    REQUIRE(partId == 0x2350);
    uint8_t uid[IAP_UID_LENGTH];
    REQUIRE(iapReadUID(uid) == IAP_SUCCESS);
    for (unsigned i = 0; i < IAP_UID_LENGTH; ++i)
    {
        uint8_t expected = 0;
        if (i < 8)
        {
            expected = i + 1;
        }
        REQUIRE(uid[i] == expected);
    }
}
