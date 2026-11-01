/*
 *  bus_test_support.h - Helpers of the KNX bus host tests (test_bus.cpp, test_bus_t9.cpp):
 *                       bus start on the simulated line, decoding of the TX pin, sending of
 *                       other devices. Implemented in test_bus.cpp (white box access to the
 *                       timer emulation state).
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_TEST_RP2350_BUS_TEST_SUPPORT_H_
#define SBLIB_TEST_RP2350_BUS_TEST_SUPPORT_H_

#include <sblib/eib/bus.h>
#include <sblib/eib/bcu_const.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace bustest
{

    constexpr unsigned BitTime = 104;       // bus.cpp BIT_TIME
    constexpr unsigned PulseTime = 35;      // bus.cpp BIT_PULSE_TIME
    constexpr unsigned CharBits = 13;       // start, 8 data, parity, stop, 2 pause
    constexpr uint16_t OwnAddress = 0x1165; // 1.1.101
    constexpr uint16_t GroupAddress = 100;  // 0/0/100

    constexpr unsigned CharTimeUs = 10 * BitTime + 313; // bus.cpp: stop bit + BIT_TIMES_DELAY(3) to next start bit
    constexpr unsigned AckDelayUs = 1563;               // BIT_TIMES_DELAY(15) after the end of the stop bit

    class TestCallback : public CallbackBus
    {
    public:
        void finishedSendingTelegram(const bool successful) override
        {
            ++finished;
            lastSuccessful = successful;
        }
        [[nodiscard]] uint8_t getLayerStatus() const override
        {
            return BCU_STATUS_LINK_LAYER | BCU_STATUS_TRANSPORT_LAYER;
        }

        int finished = 0;
        bool lastSuccessful = false;
    };

    extern TestCallback callback;
    extern std::unique_ptr<Bus> bus;

    // A decoded character sent by us (from the TX pin)
    struct SentChar
    {
        uint64_t startUs; // start of the start bit pulse
        uint8_t value;
        bool parityOk;
        int maxBitOffsetUs; // largest deviation of a pulse from the 104 us raster
        std::vector<unsigned> widths;
    };

    struct Pulse
    {
        uint64_t startUs;
        unsigned widthUs;
    };

    std::vector<Pulse> txPulses();
    // Decode characters from TX pulses (a pulse within +-20 us of the bit raster is a 0 bit)
    std::vector<SentChar> decodePulses(const std::vector<Pulse>& pulses);
    // Decode the characters on the TX pin
    std::vector<SentChar> decodeTx();
    // Another device sends the bytes, first start bit at startUs, 13 bit times per character
    void remoteSend(uint64_t startUs, const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> withChecksum(std::vector<uint8_t> tel);
    // Fresh simulation, bus started and idle
    void startBus(uint32_t echoDelayUs = 2);
    void printChars(const char* title, const std::vector<SentChar>& chars);

    // A group telegram (0/0/100, GroupValueWrite 1) without checksum; sender is set by the bus
    std::vector<uint8_t> groupWrite();
    // Queue a telegram for sending, returns the expected bytes incl. sender and checksum
    std::vector<uint8_t> send(const std::vector<uint8_t>& tel);
    // Run until we sent at least count characters (or maxUs passed)
    std::vector<SentChar> runUntilSent(size_t count, uint64_t maxUs = 100000);
    uint64_t endOfStopBit(uint64_t charStartUs);
    void checkFrame(const std::vector<SentChar>& chars, size_t first, const std::vector<uint8_t>& expected);

} // namespace bustest

#endif /* SBLIB_TEST_RP2350_BUS_TEST_SUPPORT_H_ */
