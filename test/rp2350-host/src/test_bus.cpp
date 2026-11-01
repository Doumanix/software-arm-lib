/*
 *  test_bus.cpp - Host tests of the KNX TP1 bus (sblib src/eib/bus.cpp, unchanged) on top of the
 *                 RP2350 timer emulation (src/rp2350/timer.cpp) and the Pico SDK simulation.
 *
 *  The simulated bus (sim::connectBus): PIN_EIB_TX HIGH pulls the bus low after an echo delay,
 *  PIN_EIB_RX reads the bus, another device sends with sim::remoteLow(). The tests check the
 *  bit timing of sent frames, reception with acknowledge, retries and the own echo.
 *
 *  KNX TP1 character: start bit (0), 8 data bits LSB first, even parity, stop bit (1), then at
 *  least 2 bit times pause; a 0 bit is a 35 us pulse at the start of its 104 us bit time.
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <rp2350/timer.cpp> // white box: fresh emulation state for every test

#include "bus_test_support.h"

#include <sblib/eib/bus.h>
#include <sblib/interrupt.h>
#include <sblib/eib/addr_tables.h>
#include <sblib/eib/bcu_const.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <catch.hpp> // If possible, include catch.hpp as last header

namespace bustest
{

    class TestAddrTables : public AddrTables
    {
    public:
        int indexOfAddr(const int addr) override
        {
            if (addr == GroupAddress)
            {
                return 1;
            }
            return -1;
        }
        uint8_t* addrTable() override { return nullptr; }
        uint8_t* assocTable() override { return nullptr; }
    };

    TestAddrTables addrTables;
    TestCallback callback;
    std::unique_ptr<Bus> bus;

    std::vector<Pulse> txPulses()
    {
        std::vector<Pulse> result;
        const auto& log = sim::pinLog(PIN_EIB_TX);
        for (size_t i = 0; i + 1 < log.size(); ++i)
        {
            if (log[i].level && !log[i + 1].level)
            {
                result.push_back({log[i].timeUs, static_cast<unsigned int>(log[i + 1].timeUs - log[i].timeUs)});
            }
        }
        return result;
    }

    std::vector<SentChar> decodePulses(const std::vector<Pulse>& pulses)
    {
        std::vector<SentChar> chars;
        size_t i = 0;
        while (i < pulses.size())
        {
            SentChar c{pulses[i].startUs, 0, false, 0, {pulses[i].widthUs}};
            int ones = 0;
            bool parity = true;
            size_t j = i + 1;
            for (unsigned bit = 1; bit <= 10; ++bit)
            {
                const int64_t expected = static_cast<int64_t>(c.startUs) + bit * BitTime;
                bool zero = false;
                if (j < pulses.size() && std::llabs(static_cast<int64_t>(pulses[j].startUs) - expected) <= 20)
                {
                    zero = true;
                    c.maxBitOffsetUs =
                            std::max(c.maxBitOffsetUs,
                                     static_cast<int>(std::llabs(static_cast<int64_t>(pulses[j].startUs) - expected)));
                    c.widths.push_back(pulses[j].widthUs);
                    ++j;
                }
                if (bit <= 8 && !zero)
                {
                    c.value |= static_cast<uint8_t>(1u << (bit - 1));
                    ++ones;
                }
                if (bit == 9)
                {
                    if (zero)
                    {
                        parity = (ones % 2 == 0);
                    }
                    else
                    {
                        parity = (ones % 2 == 1);
                    }
                }
                if (bit == 10 && zero)
                {
                    parity = false; // stop bit must be 1
                }
            }
            c.parityOk = parity;
            chars.push_back(c);
            i = j;
        }
        return chars;
    }

    std::vector<SentChar> decodeTx()
    {
        return decodePulses(txPulses());
    }

    void remoteSend(const uint64_t startUs, const std::vector<uint8_t>& bytes)
    {
        for (size_t n = 0; n < bytes.size(); ++n)
        {
            const uint64_t charStart = startUs + n * CharBits * BitTime;
            const uint8_t b = bytes[n];
            int ones = 0;
            sim::remoteLow(charStart, PulseTime); // start bit
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                if (b & (1u << bit))
                {
                    ++ones;
                }
                else
                {
                    sim::remoteLow(charStart + (bit + 1) * BitTime, PulseTime);
                }
            }
            if (ones % 2 == 0) // even parity: parity bit 0
            {
                sim::remoteLow(charStart + 9 * BitTime, PulseTime);
            }
        }
    }

    std::vector<uint8_t> withChecksum(std::vector<uint8_t> tel)
    {
        uint8_t checksum = 0xff;
        for (const auto b: tel)
        {
            checksum ^= b;
        }
        tel.push_back(checksum);
        return tel;
    }

    void startBus(const uint32_t echoDelayUs)
    {
        sim::reset(1000000);
        for (auto& st: timers)
        {
            st = TimerState{};
        }
        for (auto& t: timerOfAlarm)
        {
            t = -1;
        }
        sim::setBusyWaitAdvance(true);
        sim::connectBus(PIN_EIB_TX, PIN_EIB_RX, echoDelayUs);
        callback = TestCallback{};
        bus = std::make_unique<Bus>(&addrTables, timer16_1, PIN_EIB_RX, PIN_EIB_TX, CAP0, TIMER_MATCH_MAT0, &callback);
        bus->begin(OwnAddress);
        sim::advance(20000); // 42 bit times INIT + 50 bit times wait -> IDLE
    }

    void printChars(const char* title, const std::vector<SentChar>& chars)
    {
        std::printf("%s\n", title);
        for (size_t n = 0; n < chars.size(); ++n)
        {
            long long deltaUs = 0;
            if (n)
            {
                deltaUs = static_cast<long long>(chars[n].startUs - chars[n - 1].startUs);
            }
            std::printf("  #%zu t=%llu dt=%lld 0x%02x parity=%d maxOffset=%d widths:", n,
                        static_cast<unsigned long long>(chars[n].startUs), deltaUs, chars[n].value, chars[n].parityOk,
                        chars[n].maxBitOffsetUs);
            for (const auto w: chars[n].widths)
            {
                std::printf(" %u", w);
            }
            std::printf("\n");
        }
    }

} // namespace bustest

using namespace bustest;

namespace
{

    // Duration of the timer interrupt handler of the bus before and after bus.cpp (busy wait), like
    // bus.cpp running from the flash with cache misses (up to about 40 us on the test board)
    uint32_t handlerBeforeUs = 0;
    uint32_t handlerAfterUs = 0;
    uint32_t captureHandlerAfterUs = 0; // additionally for capture events (own echo)

    void busyWaitUs(const uint32_t us)
    {
        const uint32_t endUs = static_cast<uint32_t>(timer_hw->timerawl) + us;
        while (static_cast<int32_t>(endUs - static_cast<uint32_t>(timer_hw->timerawl)) > 0)
        {
            // the handler is busy
        }
    }

} // namespace

extern "C" void TIMER16_1_IRQHandler()
{
    if (bus)
    {
        const bool capture = timer16_1.flag(CAP0);
        busyWaitUs(handlerBeforeUs);
        bus->timerInterruptHandler();
        busyWaitUs(handlerAfterUs);
        if (capture)
        {
            busyWaitUs(captureHandlerAfterUs);
        }
    }
}
extern "C" void TIMER16_0_IRQHandler()
{}
extern "C" void TIMER32_0_IRQHandler()
{}
extern "C" void TIMER32_1_IRQHandler()
{}

// The pins are configured by the timer emulation (pinMode of the bus pins has no own function)
void pinMode(uint32_t, uint32_t)
{}

uint32_t SystemCoreClock = SYS_CLK_HZ;

// sblib/utils.h: TX pin that fatalError() switches off (fatalError() throws in the tests)
void setKNX_TX_Pin(int)
{}

unsigned int millis()
{
    return static_cast<unsigned int>(sim::now() / 1000);
}

namespace bustest
{

    std::vector<uint8_t> groupWrite()
    {
        return {0xBC, 0x00, 0x00, 0x00, GroupAddress, 0xE1, 0x00, 0x81};
    }

    std::vector<uint8_t> sendBuffer;

    std::vector<uint8_t> send(const std::vector<uint8_t>& tel)
    {
        sendBuffer = tel;
        sendBuffer.resize(32, 0);
        bus->sendTelegram(sendBuffer.data(), static_cast<uint16_t>(tel.size()));
        std::vector<uint8_t> expected = tel;
        expected[1] = OwnAddress >> 8;
        expected[2] = OwnAddress & 0xff;
        return withChecksum(expected);
    }

    std::vector<SentChar> runUntilSent(const size_t count, const uint64_t maxUs)
    {
        const uint64_t end = sim::now() + maxUs;
        while (sim::now() < end)
        {
            sim::advance(200);
            if (decodeTx().size() >= count)
            {
                sim::advance(11 * BitTime); // complete the last character
                return decodeTx();
            }
        }
        return decodeTx();
    }

    uint64_t endOfStopBit(const uint64_t charStartUs)
    {
        return charStartUs + 11 * BitTime;
    }

    void checkFrame(const std::vector<SentChar>& chars, const size_t first, const std::vector<uint8_t>& expected)
    {
        REQUIRE(chars.size() >= first + expected.size());
        for (size_t n = 0; n < expected.size(); ++n)
        {
            const SentChar& c = chars[first + n];
            INFO("character " << n);
            CHECK(c.value == expected[n]);
            CHECK(c.parityOk);
            CHECK(c.maxBitOffsetUs == 0);
            for (const auto w: c.widths)
            {
                CHECK(w == PulseTime);
            }
            if (n > 0)
            {
                CHECK(c.startUs - chars[first + n - 1].startUs == CharTimeUs);
            }
        }
    }

} // namespace bustest

TEST_CASE("bus: sends a telegram on the 104 us bit raster with 35 us pulses", "[rp2350][bus]")
{
    startBus();
    const auto expected = send(groupWrite());
    const auto chars = runUntilSent(expected.size());
    checkFrame(chars, 0, expected);
}

TEST_CASE("bus: an acknowledged telegram is sent once and reported as successful", "[rp2350][bus]")
{
    for (const uint32_t echoDelay: {0u, 2u, 5u})
    {
        DYNAMIC_SECTION("own echo after " << echoDelay << " us")
        {
            startBus(echoDelay);
            const auto expected = send(groupWrite());
            auto chars = runUntilSent(expected.size());
            REQUIRE(chars.size() == expected.size());
            remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {0xCC});
            sim::advance(50000);

            chars = decodeTx();
            CHECK(chars.size() == expected.size()); // no repetition: the own echo was no collision
            checkFrame(chars, 0, expected);
            CHECK(callback.finished == 1);
            CHECK(callback.lastSuccessful);
            CHECK_FALSE(bus->sendingFrame());
        }
    }
}

TEST_CASE("bus: without acknowledge the telegram is repeated 3 times, then reported as failed", "[rp2350][bus]")
{
    startBus();
    const auto expected = send(groupWrite());
    sim::advance(200000);

    auto repeated = expected;
    repeated[0] &= ~SB_TEL_REPEAT_FLAG; // repeat flag cleared = repeated frame
    repeated.pop_back();
    repeated = withChecksum(repeated);

    const auto chars = decodeTx();
    REQUIRE(chars.size() == 4 * expected.size());
    checkFrame(chars, 0, expected);
    for (size_t k = 1; k < 4; ++k)
    {
        INFO("repetition " << k);
        checkFrame(chars, k * expected.size(), repeated);
        // the repetition starts at least 50 bit times after the acknowledge window
        CHECK(chars[k * expected.size()].startUs - endOfStopBit(chars[k * expected.size() - 1].startUs) >=
              50 * BitTime);
    }
    CHECK(callback.finished == 1);
    CHECK_FALSE(callback.lastSuccessful);
}

TEST_CASE("bus: receives a telegram for the own address and acknowledges after 15 bit times", "[rp2350][bus]")
{
    startBus();
    const auto rx = withChecksum({0xB0, 0x11, 0x02, OwnAddress >> 8, OwnAddress & 0xff, 0x60, 0x80});
    const uint64_t t0 = sim::now() + 1000;
    remoteSend(t0, rx);
    sim::advance(50000);

    REQUIRE(bus->telegramReceived());
    REQUIRE(bus->telegramLen == rx.size());
    CHECK(std::vector<uint8_t>(bus->telegram, bus->telegram + rx.size()) == rx);

    const auto chars = decodeTx();
    REQUIRE(chars.size() == 1);
    CHECK(chars[0].value == 0xCC);
    const auto lastCharStart = t0 + (rx.size() - 1) * CharBits * BitTime;
    const auto ackDelay = static_cast<int64_t>(chars[0].startUs) - static_cast<int64_t>(endOfStopBit(lastCharStart));
    INFO("acknowledge " << ackDelay << " us after the stop bit");
    CHECK(std::llabs(ackDelay - AckDelayUs) <= 3);
    CHECK(chars[0].widths.size() == 6); // 0xCC: start bit, data bits 0, 1, 4, 5, parity
    for (const auto w: chars[0].widths)
    {
        CHECK(w == PulseTime);
    }
}

TEST_CASE("bus: acknowledges a group telegram of the address table, ignores others", "[rp2350][bus]")
{
    SECTION("known group address")
    {
        startBus();
        remoteSend(sim::now() + 1000, withChecksum({0xBC, 0x11, 0x02, 0x00, GroupAddress, 0xE1, 0x00, 0x80}));
        sim::advance(50000);
        CHECK(bus->telegramReceived());
        const auto chars = decodeTx();
        REQUIRE(chars.size() == 1);
        CHECK(chars[0].value == 0xCC);
    }
    SECTION("unknown group address")
    {
        startBus();
        remoteSend(sim::now() + 1000, withChecksum({0xBC, 0x11, 0x02, 0x00, GroupAddress + 1, 0xE1, 0x00, 0x80}));
        sim::advance(50000);
        CHECK_FALSE(bus->telegramReceived());
        CHECK(decodeTx().empty());
    }
    SECTION("other physical address")
    {
        startBus();
        remoteSend(sim::now() + 1000, withChecksum({0xB0, 0x11, 0x02, 0x11, 0x66, 0x60, 0x80}));
        sim::advance(50000);
        CHECK_FALSE(bus->telegramReceived());
        CHECK(decodeTx().empty());
    }
}

TEST_CASE("bus: a telegram with a wrong checksum is neither taken nor acknowledged", "[rp2350][bus]")
{
    startBus();
    auto rx = withChecksum({0xB0, 0x11, 0x02, OwnAddress >> 8, OwnAddress & 0xff, 0x60, 0x80});
    rx.back() ^= 0x01;
    remoteSend(sim::now() + 1000, rx);
    sim::advance(50000);
    CHECK_FALSE(bus->telegramReceived());
    CHECK(decodeTx().empty());
}

TEST_CASE("bus: late timer interrupts keep the frame and the pulse width", "[rp2350][bus]")
{
    // Up to PwmEdgeLeadUs the TX edge alarms come early enough: exact raster; above, at most the rest
    for (const uint32_t latency: {1u, 3u, 4u, 8u})
    {
        DYNAMIC_SECTION("alarm interrupt latency " << latency << " us")
        {
            startBus();
            sim::setAlarmLatency(latency);
            const auto expected = send(groupWrite());
            auto chars = runUntilSent(expected.size());
            REQUIRE(chars.size() == expected.size());
            remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {0xCC});
            sim::advance(50000);

            chars = decodeTx();
            REQUIRE(chars.size() == expected.size());
            for (size_t n = 0; n < expected.size(); ++n)
            {
                INFO("character " << n);
                CHECK(chars[n].value == expected[n]);
                int late = 0;
                if (latency > PwmEdgeLeadUs)
                {
                    late = static_cast<int>(latency - PwmEdgeLeadUs);
                }
                CHECK(chars[n].maxBitOffsetUs <= late);
                for (const auto w: chars[n].widths)
                {
                    CHECK(w == PulseTime);
                }
            }
            CHECK(callback.lastSuccessful);
        }
    }
}

TEST_CASE("bus: a slow timer interrupt handler (bus.cpp from flash) keeps the frame and the pulse width",
          "[rp2350][bus]")
{
    // The TX edge interrupt interrupts the handler, the match processing follows it.
    // Up to 40 us per handler call in total (test board: at most 30 us in the alarm interrupt, 38 us in the
    // RX edge interrupt). With the RX edge interrupt above the TX edge this failed from 40 us on; from 60 us on bus.cpp misses its
    // next match also with the TX edge interrupt (the handler runs into the next bit).
    const std::vector<uint8_t> longTelegram = {0xBC, 0x00, 0x00, 0x00, 0x01, 0x64, 0x62, 0x41, 0x01, 0x0E, 0xE0};
    for (const uint32_t before: {0u, 10u, 30u})
    {
        for (const uint32_t after: {0u, 10u, 30u})
        {
            if (before + after > 40)
            {
                continue;
            }
            for (const bool isLong: {false, true})
            {
                DYNAMIC_SECTION("handler " << before << " + " << after << " us, long " << isLong)
                {
                    startBus();
                    handlerBeforeUs = before;
                    handlerAfterUs = after;
                    std::vector<uint8_t> expected;
                    if (isLong)
                    {
                        expected = send(longTelegram);
                    }
                    else
                    {
                        expected = send(groupWrite());
                    }
                    const auto chars = runUntilSent(expected.size());
                    remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {0xCC});
                    sim::advance(50000);
                    handlerBeforeUs = 0;
                    handlerAfterUs = 0;

                    const auto all = decodeTx();
                    CHECK(all.size() == expected.size()); // sent once
                    checkFrame(all, 0, expected);
                    CHECK(callback.lastSuccessful);
                }
            }
        }
    }
}

TEST_CASE("bus: a reflection after the pulse end during a long echo interrupt is no collision", "[rp2350][bus]")
{
    // The own echo of a 0 bit runs the handler longer than the pulse (38 us on the test board). The TX edge
    // interrupt ends the pulse in time; a reflection 1 us after the pulse end must still be ignored by bus.cpp
    // (REFLECTION_IGNORE_DELAY), i.e. its capture value must be relative to the counter reset at the pulse end.
    const std::vector<uint8_t> longTelegram = {0xBC, 0x00, 0x00, 0x00, 0x01, 0x64, 0x62, 0x41, 0x01, 0x0E, 0xE0};
    for (const uint32_t echoUs: {0u, 20u, 34u, 36u, 38u, 40u})
    {
        DYNAMIC_SECTION("echo handler " << echoUs << " us")
        {
            startBus();
            captureHandlerAfterUs = echoUs;
            sim::onPinChange(PIN_EIB_TX,
                             [](const bool level)
                             {
                                 if (!level)
                                 {
                                     sim::atHardware(sim::now() + 1, [] { sim::fallingEdge(PIN_EIB_RX); });
                                 }
                             });
            const auto expected = send(longTelegram);
            const auto chars = runUntilSent(expected.size());
            remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {0xCC});
            sim::advance(50000);
            captureHandlerAfterUs = 0;

            const auto all = decodeTx();
            CHECK(all.size() == expected.size()); // sent once
            checkFrame(all, 0, expected);
            CHECK(callback.lastSuccessful);
        }
    }
}

TEST_CASE("bus explore: send and receive timing", "[.explore]")
{
    startBus();
    uint8_t tel[16] = {0xBC, 0x00, 0x00, 0x00, GroupAddress, 0xE1, 0x00, 0x81};
    bus->sendTelegram(tel, 8);
    sim::advance(200000);
    printChars("send without ACK:", decodeTx());
    std::printf("finished=%d ok=%d\n", callback.finished, callback.lastSuccessful);

    startBus();
    const auto rx = withChecksum({0xB0, 0x11, 0x02, 0x11, 0x65, 0x60, 0x80});
    const uint64_t t0 = sim::now() + 1000;
    remoteSend(t0, rx);
    sim::advance(50000);
    const uint64_t endOfStop = t0 + (rx.size() - 1) * CharBits * BitTime + 11 * BitTime;
    printChars("receive, our ACK:", decodeTx());
    std::printf("end of last stop bit t=%llu, telegramReceived=%d len=%d\n", static_cast<unsigned long long>(endOfStop),
                bus->telegramReceived(), bus->telegramLen);
}

TEST_CASE("Pause of an idle bus never waits for another IRQ", "[bus][pause]")
{
    using namespace bustest;
    startBus();
    unsigned waits = 0;
    sim::setInterruptBoundaryHook(
            [&](const sim::InterruptBoundary boundary)
            {
                if (boundary == sim::InterruptBoundary::BeforeWait)
                {
                    ++waits;
                }
            });
    const uint64_t start = sim::now();
    bus->pause();
    REQUIRE(waits == 0);
    REQUIRE(sim::now() == start);
    REQUIRE(sim::interruptsEnabled());
    bus->resume();
    sim::advance(20000);
    bus->pause();
    REQUIRE(waits == 0);
}

TEST_CASE("Pause waits with PRIMASK until a real timer IRQ makes the bus pausable", "[bus][pause]")
{
    using namespace bustest;
    using Boundary = sim::InterruptBoundary;
    const auto injection = GENERATE(Boundary::BeforeDisable, Boundary::Disabled, Boundary::BeforeWait);
    startBus();
    bus->resume(); // INIT cannot pause; the real timer callback changes it after the timeout.
    bool injected = false;
    unsigned waits = 0;
    unsigned handlersBeforeWait = 0;
    const unsigned initialIrqs = sim::alarmIrqCount();
    sim::setInterruptBoundaryHook(
            [&](const Boundary boundary)
            {
                if (!injected && boundary == injection)
                {
                    injected = true;
                    sim::advance(5000); // real alarm source, pending until PRIMASK is cleared
                    handlersBeforeWait = sim::alarmIrqCount() - initialIrqs;
                }
                if (boundary == Boundary::BeforeWait)
                {
                    ++waits;
                    REQUIRE_FALSE(sim::interruptsEnabled());
                    REQUIRE(waits <= 2); // also bounds a broken polling implementation
                }
            });
    bus->pause();
    REQUIRE(injected);
    if (injection == Boundary::BeforeDisable)
    {
        REQUIRE(handlersBeforeWait == 1);
        REQUIRE(waits == 0);
    }
    else
    {
        REQUIRE(handlersBeforeWait == 0);
        REQUIRE(waits == 1);
    }
    REQUIRE(sim::alarmIrqCount() > initialIrqs);
    REQUIRE(sim::interruptsEnabled());
}

TEST_CASE("Pause wakes for future and unrelated IRQs and rechecks the real bus condition", "[bus][pause]")
{
    using namespace bustest;
    const bool unrelatedIrq = GENERATE(false, true);
    startBus();
    bus->resume();
    static unsigned unrelatedCalls = 0;
    unrelatedCalls = 0;
    irq_set_exclusive_handler(TIMER1_IRQ_3, [] { ++unrelatedCalls; });
    irq_set_enabled(TIMER1_IRQ_3, true);
    if (unrelatedIrq)
    {
        sim::atHardware(sim::now() + 10, [] { irq_set_pending(TIMER1_IRQ_3); });
    }
    unsigned waits = 0;
    sim::setInterruptBoundaryHook(
            [&](const sim::InterruptBoundary boundary)
            {
                if (boundary == sim::InterruptBoundary::BeforeWait)
                {
                    ++waits;
                    REQUIRE_FALSE(sim::interruptsEnabled());
                    REQUIRE(waits <= 3);
                }
            });
    bus->pause();
    unsigned expectedWaits = 1;
    unsigned expectedUnrelatedCalls = 0;
    if (unrelatedIrq)
    {
        expectedWaits = 2;
        expectedUnrelatedCalls = 1;
    }
    REQUIRE(waits == expectedWaits);
    REQUIRE(unrelatedCalls == expectedUnrelatedCalls);
    REQUIRE(sim::interruptsEnabled());
}

TEST_CASE("Pause with no enabled wake source is bounded by the simulator", "[bus][pause]")
{
    using namespace bustest;
    startBus();
    bus->resume();
    for (uint num = 0; num < IRQ_COUNT; ++num)
    {
        irq_set_enabled(num, false);
    }
    unsigned waits = 0;
    sim::setInterruptBoundaryHook(
            [&](const sim::InterruptBoundary boundary)
            {
                if (boundary == sim::InterruptBoundary::BeforeWait)
                {
                    REQUIRE_FALSE(sim::interruptsEnabled());
                    REQUIRE(++waits <= 2);
                }
            });
    REQUIRE_THROWS_WITH(bus->pause(), "pico_sim: WFI without wake IRQ within 10000 us");
    // A real WFI would remain asleep; this exception is a host-test escape only.
    sim::setInterruptBoundaryHook({});
    enable_interrupts();
}

TEST_CASE("Pause preserves the enqueued telegram across pause and resume", "[bus][pause]")
{
    using namespace bustest;
    startBus();
    send(groupWrite());
    unsigned waits = 0;
    sim::setInterruptBoundaryHook(
            [&](const sim::InterruptBoundary boundary)
            {
                if (boundary == sim::InterruptBoundary::BeforeWait)
                {
                    REQUIRE_FALSE(sim::interruptsEnabled());
                    REQUIRE(++waits < 100);
                }
            });
    bus->pause(false);
    REQUIRE(waits == 0);
    REQUIRE(callback.finished == 0);
    bus->resume();
    sim::advance(200000);
    REQUIRE(callback.finished == 1);
    REQUIRE(sim::interruptsEnabled());
}

TEST_CASE("Pause can wait for an enqueued telegram to finish", "[bus][pause]")
{
    using namespace bustest;
    startBus();
    send(groupWrite());
    unsigned waits = 0;
    sim::setInterruptBoundaryHook(
            [&](const sim::InterruptBoundary boundary)
            {
                if (boundary == sim::InterruptBoundary::BeforeWait)
                {
                    REQUIRE_FALSE(sim::interruptsEnabled());
                    REQUIRE(++waits < 2000);
                }
            });
    bus->pause(true);
    REQUIRE(waits > 0);
    REQUIRE(callback.finished == 1);
    REQUIRE(sim::interruptsEnabled());
}

TEST_CASE("Pause rechecks its condition when WFI returns without an IRQ", "[bus][pause]")
{
    using namespace bustest;
    startBus();
    bus->resume();
    sim::setWfiSpuriousReturns(2);
    unsigned waits = 0;
    sim::setInterruptBoundaryHook(
            [&](const sim::InterruptBoundary boundary)
            {
                if (boundary == sim::InterruptBoundary::BeforeWait)
                {
                    REQUIRE_FALSE(sim::interruptsEnabled());
                    REQUIRE(++waits <= 3);
                }
            });
    bus->pause();
    REQUIRE(waits == 3);
    REQUIRE(sim::interruptsEnabled());
}

TEST_CASE("Masked wait leaves a pending IRQ for the caller to dispatch", "[bus][pause]")
{
    sim::reset();
    static unsigned calls = 0;
    calls = 0;
    irq_set_exclusive_handler(TIMER1_IRQ_3, [] { ++calls; });
    irq_set_enabled(TIMER1_IRQ_3, true);
    noInterrupts();
    irq_set_pending(TIMER1_IRQ_3);
    const uint64_t start = sim::now();
    waitForInterruptMasked();
    REQUIRE(sim::now() == start);
    REQUIRE_FALSE(sim::interruptsEnabled());
    REQUIRE(calls == 0);
    interrupts();
    REQUIRE(calls == 1);
    REQUIRE(sim::interruptsEnabled());
}

TEST_CASE("NVIC-disabled pending IRQ does not wake the masked wait model", "[bus][pause]")
{
    sim::reset();
    noInterrupts();
    irq_set_pending(TIMER1_IRQ_3); // NVIC enable bit deliberately remains clear
    REQUIRE_THROWS_WITH(waitForInterruptMasked(), "pico_sim: WFI without wake IRQ within 10000 us");
    REQUIRE_FALSE(sim::interruptsEnabled());
    interrupts();
}

namespace
{

    // White-box access is enabled only for this host-test translation unit. The original
    // bus state machine and timer emulation run unchanged; no WFI implementation is substituted.
    constexpr unsigned WakeStateCount = Bus::SEND_WAIT_FOR_RX_ACK + 1;
    constexpr uint32_t MaxBusWakeUs = 16000; // includes the 150-bit BUSY wait (15625 us)

    struct WakeCoverage
    {
        bool seen[WakeStateCount] = {};
        bool checked[WakeStateCount] = {};
        uint64_t samples = 0;
        uint32_t maxDeadlineUs = 0;

        void inspect()
        {
            const auto state = bus->state;
            const unsigned index = static_cast<unsigned>(state);
            REQUIRE(index < WakeStateCount);
            seen[index] = true;
            if (bus->canPause(true))
            {
                return;
            }

            const auto& st = timers[TIMER16_1];
            REQUIRE(st.alarmNum >= 0);
            const unsigned alarm = static_cast<unsigned>(st.alarmNum);
            const uint32_t mask = 1u << alarm;
            const uint32_t now = static_cast<uint32_t>(sim::now());
            const uint32_t delta = static_cast<uint32_t>(timer_hw->alarm[alarm]) - now;
            const bool pending = (static_cast<uint32_t>(timer_hw->ints) & mask) != 0;
            const bool finite = pending || (sim::alarmArmed(alarm) && delta > 0 && delta <= MaxBusWakeUs);
            const bool enabled = st.irqEnabled && irq_is_enabled(hardware_alarm_get_irq_num(alarm)) &&
                                 (static_cast<uint32_t>(timer_hw->inte) & mask) != 0;
            const bool matchInterrupt = (bus->timer.matchMode(bus->timeChannel) & INTERRUPT) != 0;
            INFO("state=" << index << " now=" << sim::now() << " delta=" << delta << " pending=" << pending);
            REQUIRE(enabled);
            REQUIRE(matchInterrupt);
            REQUIRE(finite);
            checked[index] = true;
            ++samples;
            if (!pending)
            {
                maxDeadlineUs = std::max(maxDeadlineUs, delta);
            }
        }

        void run(const uint64_t durationUs)
        {
            const uint64_t end = sim::now() + durationUs;
            while (sim::now() < end)
            {
                inspect();
                sim::advance(1);
            }
            inspect();
        }
    };

    void receiveForWakeTest()
    {
        const auto telegram = withChecksum({0xB0, 0x11, 0x02, 0x11, 0x65, 0x60, 0x80});
        remoteSend(sim::now() + 100, telegram);
    }

    void waitForWakeState(const Bus::State wanted, const uint64_t maxUs)
    {
        const uint64_t end = sim::now() + maxUs;
        while (bus->state != wanted && sim::now() < end)
        {
            sim::advance(1);
        }
        REQUIRE(bus->state == wanted);
        REQUIRE_FALSE(sim::insideInterrupt());
    }

} // namespace

TEST_CASE("bus wake: reachable non-pausable states have an enabled finite match alarm",
          "[rp2350][bus][wake]")
{
    WakeCoverage coverage;

    startBus();
    bus->resume();
    coverage.run(12000);

    startBus();
    send(groupWrite());
    coverage.run(150000); // no peer: ACK timeout, repetitions, retry limit
    CHECK(callback.finished == 1);

    startBus();
    receiveForWakeTest();
    coverage.run(25000); // own ACK and return to IDLE
    CHECK(bus->telegramReceived());

    startBus();
    const auto expected = send(groupWrite());
    const auto chars = runUntilSent(expected.size());
    REQUIRE(chars.size() == expected.size());
    remoteSend(endOfStopBit(chars.back().startUs) + AckDelayUs, {SB_BUS_BUSY});
    coverage.run(90000); // BUSY backoff and later no-ACK repetitions

    for (const auto state: {Bus::INIT, Bus::RECV_WAIT_FOR_STARTBIT_OR_TELEND, Bus::RECV_BITS_OF_BYTE,
                            Bus::RECV_WAIT_FOR_ACK_TX_START, Bus::WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE,
                            Bus::SEND_START_BIT, Bus::SEND_BIT_0, Bus::SEND_BITS_OF_BYTE, Bus::SEND_END_OF_TX,
                            Bus::SEND_WAIT_FOR_RX_ACK_WINDOW, Bus::SEND_WAIT_FOR_RX_ACK})
    {
        INFO("missing state " << static_cast<unsigned>(state));
        CHECK(coverage.checked[static_cast<unsigned>(state)]);
    }
    // These states are assigned and left by fallthrough/goto within one ISR. A Thread-mode
    // pause cannot observe them after the handler returned; do not manufacture them in a fixture.
    CHECK_FALSE(coverage.seen[Bus::INIT_RX_FOR_RECEIVING_NEW_TEL]);
    CHECK_FALSE(coverage.seen[Bus::SEND_END_OF_BYTE]);
    CHECK(coverage.maxDeadlineUs > 10000); // includes the actual BUSY wait, not just normal retries
    std::printf("wake coverage: samples=%llu maxMatchAlarmDeltaUs=%u\n",
                static_cast<unsigned long long>(coverage.samples), coverage.maxDeadlineUs);
}

TEST_CASE("bus wake: match IRQ becomes pending with PRIMASK set in each observable wait state",
          "[rp2350][bus][wake]")
{
    for (const auto wanted: {Bus::INIT, Bus::RECV_WAIT_FOR_STARTBIT_OR_TELEND, Bus::RECV_BITS_OF_BYTE,
                             Bus::RECV_WAIT_FOR_ACK_TX_START, Bus::WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE,
                             Bus::SEND_START_BIT, Bus::SEND_BIT_0, Bus::SEND_BITS_OF_BYTE, Bus::SEND_END_OF_TX,
                             Bus::SEND_WAIT_FOR_RX_ACK_WINDOW, Bus::SEND_WAIT_FOR_RX_ACK})
    {
        DYNAMIC_SECTION("state " << static_cast<unsigned>(wanted))
        {
            startBus();
            if (wanted == Bus::INIT)
            {
                bus->resume();
            }
            else if (wanted == Bus::RECV_WAIT_FOR_STARTBIT_OR_TELEND || wanted == Bus::RECV_BITS_OF_BYTE ||
                     wanted == Bus::RECV_WAIT_FOR_ACK_TX_START)
            {
                receiveForWakeTest();
            }
            else
            {
                send(groupWrite());
            }
            waitForWakeState(wanted, 50000);
            REQUIRE_FALSE(bus->canPause(true));
            WakeCoverage{}.inspect();

            const auto state = bus->state;
            const auto telegram = bus->sendCurTelegram;
            const bool repeat = bus->repeatTelegram;
            const uint32_t saved = save_and_disable_interrupts();
            const unsigned alarm = static_cast<unsigned>(timers[TIMER16_1].alarmNum);
            const uint32_t delta = static_cast<uint32_t>(timer_hw->alarm[alarm]) -
                                   static_cast<uint32_t>(sim::now());
            REQUIRE(delta > 0);
            REQUIRE(delta <= MaxBusWakeUs);
            sim::advance(delta);
            CHECK_FALSE(sim::interruptsEnabled());
            CHECK(irq_is_enabled(hardware_alarm_get_irq_num(alarm)));
            CHECK((static_cast<uint32_t>(timer_hw->ints) & (1u << alarm)) != 0);
            CHECK(bus->state == state);
            CHECK(bus->sendCurTelegram == telegram);
            CHECK(bus->repeatTelegram == repeat);

            // Reading time/capture/match cannot drain PIO/DMA or execute a delayed bus state transition.
            for (unsigned i = 0; i < 64; ++i)
            {
                (void)bus->timer.value();
                (void)bus->timer.capture(bus->captureChannel);
                (void)bus->timer.match(bus->timeChannel);
                (void)bus->canPause(true);
            }
            CHECK(bus->state == state);
            CHECK(bus->sendCurTelegram == telegram);
            CHECK(bus->repeatTelegram == repeat);
            restore_interrupts(saved);
            CHECK(sim::interruptsEnabled());
        }
    }
}

TEST_CASE("bus wake: thread entry points change fields and arm the next interrupt",
          "[rp2350][bus][wake]")
{
    startBus();
    REQUIRE(bus->state == Bus::IDLE);
    CHECK(bus->canPause(true));
    CHECK_FALSE(sim::insideInterrupt());
    send(groupWrite());
    CHECK(bus->sendCurTelegram != nullptr);
    CHECK(bus->state == Bus::WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE);
    CHECK(bus->canPause(false));
    CHECK_FALSE(bus->canPause(true));
    WakeCoverage{}.inspect();

    // Begin/resume and queued-send are legitimate Thread-mode writers. Within pause's
    // waiting loop no such writer runs on the same core; progress comes from interrupts.
    startBus();
    bus->resume();
    CHECK(bus->state == Bus::INIT);
    CHECK_FALSE(bus->canPause(false));
    WakeCoverage{}.inspect();
}

TEST_CASE("bus wake: a paused INIT is not an active wait state", "[rp2350][bus][wake]")
{
    startBus();
    bus->pause();
    CHECK(bus->state == Bus::INIT);
    CHECK_FALSE(bus->canPause(false));
    CHECK_FALSE((bus->timer.matchMode(bus->timeChannel) & INTERRUPT) != 0);
    CHECK_FALSE((bus->timer.captureMode(bus->captureChannel) & INTERRUPT) != 0);
    sim::advance(70000);
    CHECK(bus->state == Bus::INIT);
    CHECK_FALSE(bus->canPause(false));
    // A second pause would not make progress with NOP either. Resume is the active-state entry.
    bus->resume();
    CHECK((bus->timer.matchMode(bus->timeChannel) & INTERRUPT) != 0);
    WakeCoverage{}.inspect();
}
