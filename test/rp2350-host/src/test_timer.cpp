/*
 *  test_timer.cpp - Host unit tests of the RP2350 emulation of the LPC11xx timers
 *                   (sblib/src/rp2350/timer.cpp).
 *
 *  The emulation source is included directly (white box), so every test starts with a fresh
 *  internal state. The Pico SDK is simulated by sim/pico_sim.cpp (time, alarms, GPIO IRQs).
 *
 *  Expected behaviour is the one of the LPC11xx timers as used by src/eib/bus.cpp
 *  (UM10398 chapter 18, 16-bit counter/timer CT16B0/1), with the timer counting microseconds:
 *  - a match event happens when TC == MR; with RESET the TC holds MR for one tick and is 0
 *    in the next tick, so a RESET cycle has MR + 1 ticks (bus.cpp: match(timeChannel, time - 1))
 *  - the PWM output of the bus (MAT0 -> TX HIGH) ends with the cycle reset by MAT2,
 *    so the pulse lasts MR2 + 1 - MR0 ticks (bus.cpp: match(pwm, t), match(time, t + 35 - 1))
 *  Deliberate deviations of the emulation are named "deviation" in the test name.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <rp2350/timer.cpp> // the unit under test, incl. its anonymous namespace

#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>
#if defined(SBLIB_RP2350_PIO_BUS)
#   include "pio_cycle_model.h"
#endif

#include <catch.hpp> // If possible, include catch.hpp as last header

namespace
{

    struct HandlerCall
    {
        uint8_t timerNum;
        uint64_t timeUs;
        int flags;
        unsigned int value;
    };

    std::vector<HandlerCall> calls;
    std::function<void(uint8_t)> handlerHook;

    const Timer* const timerObjects[TimerCount] = {&timer16_0, &timer16_1, &timer32_0, &timer32_1};

    void onTimerIrq(const uint8_t timerNum)
    {
        const Timer& t = *timerObjects[timerNum];
        calls.push_back({timerNum, sim::now(), t.flags(), t.value()});
        if (handlerHook)
        {
            handlerHook(timerNum);
        }
        else
        {
            t.resetFlags(); // as an interrupt handler does it
        }
    }

    // Fresh simulation and emulation state for a test
    uint64_t fresh(const uint64_t startUs = 1000000)
    {
        sim::reset(startUs);
        sim::setBusyWaitAdvance(true); // the pulse end waits up to a few microseconds in the interrupt
        for (auto& st: timers)
        {
            st = TimerState{};
        }
        for (auto& t: timerOfAlarm)
        {
            t = -1;
        }
        calls.clear();
        handlerHook = nullptr;
        // Counters of the emulation (volatile, no assignment of the whole struct) and diagnostics state
        std::memset(const_cast<TimerEmulationStats*>(&stats), 0, sizeof(stats));
        txDiag = TxDiagnostics{};
        txEdge = TxEdge{};
#if defined(SBLIB_RP2350_PIO_BUS)
        pioRx = PioRx{};
        pioTx = PioTx{};
#endif
        return startUs;
    }

    // Times of the output level changes of the TX pin
    std::vector<uint64_t> txEdges(const bool level)
    {
        std::vector<uint64_t> result;
        for (const auto& e: sim::pinLog(PIN_EIB_TX))
        {
            if (e.level == level)
            {
                result.push_back(e.timeUs);
            }
        }
        return result;
    }

    constexpr unsigned BitPulseTime = 35; // bus.cpp BIT_PULSE_TIME

} // namespace

extern "C" void TIMER16_0_IRQHandler()
{
    onTimerIrq(TIMER16_0);
}
extern "C" void TIMER16_1_IRQHandler()
{
    onTimerIrq(TIMER16_1);
}
extern "C" void TIMER32_0_IRQHandler()
{
    onTimerIrq(TIMER32_0);
}
extern "C" void TIMER32_1_IRQHandler()
{
    onTimerIrq(TIMER32_1);
}

// ---------------------------------------------------------------------------------------------
// Counter
// ---------------------------------------------------------------------------------------------

TEST_CASE("RP2350 timer: value counts microseconds and wraps at 16 bit", "[rp2350][timer]")
{
    fresh();
    timer16_0.begin();
    timer16_0.start();
    sim::advance(1234);
    REQUIRE(timer16_0.value() == 1234);
    sim::advance(65536 - 1234 + 5);
    REQUIRE(timer16_0.value() == 5);

    timer32_0.begin();
    timer32_0.start();
    sim::advance(70000);
    REQUIRE(timer32_0.value() == 70000);
}

TEST_CASE("RP2350 timer: value is continuous over the 32 bit wrap of TIMERAWL", "[rp2350][timer]")
{
    fresh(0xffffff00u);
    timer16_0.begin();
    timer16_0.start();
    timer32_0.begin();
    timer32_0.start();
    sim::advance(0x200);
    REQUIRE(sim_timer_hw.timerawl == 0x100u);
    REQUIRE(timer16_0.value() == 0x200);
    REQUIRE(timer32_0.value() == 0x200);
}

TEST_CASE("RP2350 timer: stop/start keep the value, value(), reset() and restart()", "[rp2350][timer]")
{
    fresh();
    timer16_0.begin();
    REQUIRE(timer16_0.value() == 0); // not started yet
    sim::advance(10);
    REQUIRE(timer16_0.value() == 0);

    timer16_0.start();
    sim::advance(100);
    timer16_0.stop();
    sim::advance(50);
    REQUIRE(timer16_0.value() == 100);
    timer16_0.start();
    sim::advance(10);
    REQUIRE(timer16_0.value() == 110);

    timer16_0.value(500);
    sim::advance(5);
    REQUIRE(timer16_0.value() == 505);

    timer16_0.restart();
    sim::advance(7);
    REQUIRE(timer16_0.value() == 7);

    timer16_0.reset();
    REQUIRE(timer16_0.value() == 0);
    sim::advance(3);
    REQUIRE(timer16_0.value() == 3);
}

// ---------------------------------------------------------------------------------------------
// Match
// ---------------------------------------------------------------------------------------------

TEST_CASE("RP2350 timer: match with INTERRUPT calls the handler at the match time", "[rp2350][timer]")
{
    const auto t0 = fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer16_0.match(TIMER_MATCH_MAT1, 500);
    timer16_0.interrupts();
    timer16_0.start();

    sim::advance(499);
    REQUIRE(calls.empty());
    REQUIRE_FALSE(timer16_0.flag(TIMER_MATCH_MAT1));

    sim::advance(1);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].timerNum == TIMER16_0);
    CHECK(calls[0].timeUs == t0 + 500);
    CHECK(calls[0].flags == Timer::flagMask(TIMER_MATCH_MAT1));
    CHECK(timer16_0.match(TIMER_MATCH_MAT1) == 500);
}

// An earlier version of the emulation re-fired every microsecond until the match value changed.
TEST_CASE("RP2350 timer: match without RESET fires once per counter cycle (TC == MR)", "[rp2350][timer]")
{
    // LPC11xx: the next match event is when the 16 bit counter reaches MR again (65536 us later).
    // bus.cpp uses INTERRUPT without RESET e.g. for MAX_INTER_CHAR_TIME and the ACK timeout.
    const auto t0 = fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer16_0.match(TIMER_MATCH_MAT1, 500);
    timer16_0.interrupts();
    timer16_0.start();

    sim::advance(1000);
    CHECK(calls.size() == 1);
    CHECK(sim::alarmIrqCount() <= 2);

    sim::advance(65536);
    REQUIRE(calls.size() >= 2);
    CHECK(calls[1].timeUs == t0 + 500 + 65536);
}

TEST_CASE("RP2350 timer: deviation - a match value already passed fires at once", "[rp2350][timer]")
{
    // LPC11xx: no match until the counter wraps. The emulation fires as soon as possible, so a
    // match value set too late (e.g. timer.match(pwm, timer.value() + 1) delayed by an
    // interrupt) does not get lost for a whole counter cycle.
    const auto t0 = fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer16_0.interrupts();
    timer16_0.start();
    sim::advance(300);
    timer16_0.match(TIMER_MATCH_MAT1, 200);
    sim::advance(1);
    REQUIRE(calls.size() >= 1);
    CHECK(calls[0].timeUs == t0 + 301);
}

// An earlier version of the emulation restarted at 0 when TC == MR (cycle MR instead of MR + 1).
// Also covered by the bus PWM tests below.
TEST_CASE("RP2350 timer: RESET cycle has MR + 1 microseconds", "[rp2350][timer]")
{
    const auto t0 = fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT2, INTERRUPT | RESET);
    timer16_0.match(TIMER_MATCH_MAT2, 99); // bus.cpp style: period 100 us
    timer16_0.interrupts();
    timer16_0.start();

    sim::advance(99);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].timeUs == t0 + 99); // match event at TC == MR
    sim::advance(1);
    CHECK(timer16_0.value() == 0); // TC == 0 one tick after the match
    sim::advance(10);
    CHECK(timer16_0.value() == 10);

    sim::runUntil(t0 + 99 + 10 * 100);
    REQUIRE(calls.size() == 11);
    for (unsigned k = 0; k < calls.size(); ++k)
    {
        INFO("cycle " << k);
        CHECK(calls[k].timeUs == t0 + 99 + 100 * k);
    }
}

TEST_CASE("RP2350 timer: STOP stops the counter at the match value", "[rp2350][timer]")
{
    const auto t0 = fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT | STOP);
    timer16_0.match(TIMER_MATCH_MAT1, 300);
    timer16_0.interrupts();
    timer16_0.start();

    sim::advance(1000);
    CHECK(timer16_0.value() == 300);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].timeUs == t0 + 300);
}

TEST_CASE("RP2350 timer: RESET holds the match value for one microsecond (value and capture)", "[rp2350][timer]")
{
    fresh();
    unsigned int captureInIrq = 0;
    handlerHook = [&](uint8_t)
    {
        if (timer16_1.flag(TIMER_MATCH_MAT2) && calls.size() == 1)
        {
            sim::fallingEdge(PIN_EIB_RX); // edge in the same microsecond as the match event
        }
        if (timer16_1.flag(CAP0))
        {
            captureInIrq = timer16_1.capture(CAP0);
        }
        timer16_1.resetFlags();
    };
    timer16_1.begin();
    const auto t0 = sim::now(); // with PIO, begin() takes some microseconds (start of the TX state machine)
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.matchMode(TIMER_MATCH_MAT2, INTERRUPT | RESET);
    timer16_1.match(TIMER_MATCH_MAT2, 99);
    timer16_1.interrupts();
    timer16_1.start();

    sim::advance(99);
    REQUIRE(calls.size() == 2); // match event, then the capture
    CHECK(calls[0].timeUs == t0 + 99);
    CHECK(calls[0].value == 99); // TC == MR in the interrupt
    CHECK(captureInIrq == 99);
    CHECK(timer16_1.value() == 99);
    sim::advance(1);
    CHECK(timer16_1.value() == 0);
}

TEST_CASE("RP2350 timer: a fired match without RESET is re-armed by match() and value()", "[rp2350][timer]")
{
    const auto t0 = fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer16_0.match(TIMER_MATCH_MAT1, 100);
    timer16_0.interrupts();
    timer16_0.start();

    sim::advance(150);
    REQUIRE(calls.size() == 1);
    timer16_0.match(TIMER_MATCH_MAT1, 200);
    sim::advance(100);
    REQUIRE(calls.size() == 2);
    CHECK(calls[1].timeUs == t0 + 200);

    timer16_0.value(0); // at t0 + 250
    sim::advance(300);
    REQUIRE(calls.size() == 3);
    CHECK(calls[2].timeUs == t0 + 450);
}

TEST_CASE("RP2350 timer: after STOP, start() does not match again in the same counter cycle", "[rp2350][timer]")
{
    fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT | STOP);
    timer16_0.match(TIMER_MATCH_MAT1, 300);
    timer16_0.interrupts();
    timer16_0.start();
    sim::advance(1000);
    REQUIRE(calls.size() == 1);

    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    const auto restartTime = sim::now();
    timer16_0.start();
    sim::advance(1000);
    CHECK(timer16_0.value() == 1300);
    CHECK(calls.size() == 1);

    sim::runUntil(restartTime + 65536); // the counter reaches 300 again after the wrap
    REQUIRE(calls.size() == 2);
    CHECK(calls[1].timeUs == restartTime + 65536);
}

TEST_CASE("RP2350 timer: 32 bit timer, a fired match waits for match()", "[rp2350][timer]")
{
    const auto t0 = fresh();
    timer32_0.begin();
    timer32_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer32_0.match(TIMER_MATCH_MAT1, 100);
    timer32_0.interrupts();
    timer32_0.start();
    sim::advance(200000);
    REQUIRE(calls.size() == 1);
    CHECK(sim::alarmIrqCount() == 1);

    timer32_0.match(TIMER_MATCH_MAT1, 300000);
    sim::advance(200000);
    REQUIRE(calls.size() == 2);
    CHECK(calls[1].timeUs == t0 + 300000);
}

TEST_CASE("RP2350 timer: match on a stopped timer does not fire", "[rp2350][timer]")
{
    fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer16_0.match(TIMER_MATCH_MAT1, 10);
    timer16_0.interrupts();
    sim::advance(1000);
    CHECK(calls.empty());
    CHECK(sim::alarmIrqCount() == 0);
}

// ---------------------------------------------------------------------------------------------
// Interrupt enable
// ---------------------------------------------------------------------------------------------

TEST_CASE("RP2350 timer: flags set while the timer interrupt is disabled run the handler on interrupts()",
          "[rp2350][timer]")
{
    const auto t0 = fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer16_0.match(TIMER_MATCH_MAT1, 50);
    timer16_0.noInterrupts();
    timer16_0.start();

    sim::advance(60);
    CHECK(calls.empty());
    CHECK(timer16_0.flag(TIMER_MATCH_MAT1));

    timer16_0.interrupts(); // like a pending NVIC interrupt: runs at once
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].timeUs == t0 + 60);
    CHECK(calls[0].flags == Timer::flagMask(TIMER_MATCH_MAT1));
}

TEST_CASE("RP2350 timer: a match during a global interrupt lock runs after the lock", "[rp2350][timer]")
{
    const auto t0 = fresh();
    timer16_0.begin();
    timer16_0.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer16_0.match(TIMER_MATCH_MAT1, 50);
    timer16_0.interrupts();
    timer16_0.start();

    noInterrupts();
    sim::advance(80);
    CHECK(calls.empty());
    interrupts();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].timeUs == t0 + 80);
}

TEST_CASE("RP2350 timer: resetFlag() clears only the given flag", "[rp2350][timer]")
{
    fresh();
    timer16_1.begin();
    timer16_1.matchMode(TIMER_MATCH_MAT2, INTERRUPT);
    timer16_1.match(TIMER_MATCH_MAT2, 20);
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.noInterrupts();
    timer16_1.start();
    sim::advance(10);
    sim::fallingEdge(PIN_EIB_RX);
    sim::advance(20);
    REQUIRE(timer16_1.flag(CAP0));
    REQUIRE(timer16_1.flag(TIMER_MATCH_MAT2));

    timer16_1.resetFlag(CAP0);
    CHECK_FALSE(timer16_1.flag(CAP0));
    CHECK(timer16_1.flag(TIMER_MATCH_MAT2));
    timer16_1.resetFlag(TIMER_MATCH_MAT2);
    CHECK(timer16_1.flags() == 0);
}

// ---------------------------------------------------------------------------------------------
// Bus PWM (timer16_1: MAT0 pulse start, MAT2 pulse end with RESET)
// ---------------------------------------------------------------------------------------------

namespace
{

    // Configure timer16_1 like Bus::begin() and the first bit of a telegram (bus.cpp sendNextTelegram)
    uint64_t startBusPulse(const unsigned pulseStart)
    {
        timer16_1.setIRQPriority(InterruptPriority::highest);
        timer16_1.begin();
        timer16_1.pwmEnable(TIMER_MATCH_MAT0);
        timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
        timer16_1.interrupts();
        timer16_1.restart();
        const auto t0 = sim::now();
        timer16_1.match(TIMER_MATCH_MAT0, pulseStart);
        timer16_1.match(TIMER_MATCH_MAT2, pulseStart + BitPulseTime - 1);
        timer16_1.matchMode(TIMER_MATCH_MAT2, RESET | INTERRUPT);
        return t0;
    }

} // namespace

TEST_CASE("RP2350 timer: bus PWM pulse lasts MR2 + 1 - MR0 (35 us)", "[rp2350][timer][bus]") // R11
{
    fresh();
    const auto t0 = startBusPulse(104);
    sim::advance(300);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 1); // MAT0 is one-shot until the next match(MAT0, ...)
    REQUIRE(low.size() == 1);
    CHECK(high[0] == t0 + 104);
    CHECK(low[0] - high[0] == BitPulseTime);
    CHECK_FALSE(sim::pinLevel(PIN_EIB_TX));

    // the match value stays readable (bus.cpp compares the own echo with match(pwmChannel))
    CHECK(timer16_1.match(TIMER_MATCH_MAT0) == 104);
    REQUIRE(calls.size() >= 1);
#if defined(SBLIB_RP2350_PIO_BUS)
    // MAT2 event at TC == MR2: the state machine ends the pulse one microsecond later, the interrupt calls the
    // handler at once. The handler reads TC == MR2 (held for the reset tick), as on the LPC11xx.
    CHECK(calls[0].timeUs == t0 + 104 + BitPulseTime - 1);
    CHECK(calls[0].value == 104 + BitPulseTime - 1);
#else
    // MAT2 event at TC == MR2; the interrupt first ends the pulse one microsecond later (busy
    // wait), then calls the handler. The handler reads the timer (0 after the reset tick).
    CHECK(calls[0].timeUs == t0 + 104 + BitPulseTime);
    CHECK(calls[0].value == 0);
#endif
}

TEST_CASE("RP2350 timer: bus PWM pulse keeps 35 us with alarm interrupt latency", "[rp2350][timer][bus]")
{
    // The TX edge alarms fire PwmEdgeLeadUs early: up to this latency the edges are exact,
    // above it the pulse starts late by the rest, but still lasts 35 us.
    for (const uint32_t latency: {1u, 2u, 4u, 5u, 10u, 20u})
    {
        DYNAMIC_SECTION("latency " << latency << " us")
        {
            fresh();
            sim::setAlarmLatency(latency);
            const auto t0 = startBusPulse(104);
            sim::advance(300);
            const auto high = txEdges(true);
            const auto low = txEdges(false);
            REQUIRE(high.size() == 1);
            REQUIRE(low.size() == 1);
            uint32_t late = 0;
#if !defined(SBLIB_RP2350_PIO_BUS) // PIO: the state machine switches both edges at their time
            if (latency > PwmEdgeLeadUs)
            {
                late = latency - PwmEdgeLeadUs;
            }
#endif
            CHECK(high[0] == t0 + 104 + late);
            CHECK(low[0] - high[0] == BitPulseTime);
        }
    }
}

TEST_CASE("RP2350 timer: bus PWM late interrupt keeps the pulse width", "[rp2350][timer][bus]") // R11
{
    // Pulse start and pulse end are due in the same (late) alarm interrupt
    fresh();
    const auto t0 = startBusPulse(104);
    noInterrupts();
    sim::advance(104 + 40);
    interrupts();
    sim::advance(200);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 1);
    REQUIRE(low.size() == 1);
#if defined(SBLIB_RP2350_PIO_BUS)
    CHECK(high[0] == t0 + 104); // the state machine switches the planned pulse during the lock
#else
    CHECK(high[0] == t0 + 144);
#endif
    CHECK(low[0] - high[0] == BitPulseTime);
}

TEST_CASE("RP2350 timer: bus PWM sentinels 0xffff (MAT0) and 0xfffe (MAT2) are off", "[rp2350][timer][bus]")
{
    fresh();
    timer16_1.begin();
    timer16_1.pwmEnable(TIMER_MATCH_MAT0);
    timer16_1.matchMode(TIMER_MATCH_MAT2, RESET);
    timer16_1.match(TIMER_MATCH_MAT2, 0xfffe);
    timer16_1.match(TIMER_MATCH_MAT0, 0xffff);
    timer16_1.interrupts();
    timer16_1.restart();
    sim::advance(200000);
    CHECK(sim::pinLog(PIN_EIB_TX).empty());
    CHECK(calls.empty());
    CHECK(sim::alarmIrqCount() == 0);
}

TEST_CASE("RP2350 timer: bus PWM next bit re-arms MAT0", "[rp2350][timer][bus]") // R11
{
    // After the first pulse the handler programs the next bit, as bus.cpp does in SEND_BIT_0
    fresh();
    handlerHook = [](uint8_t)
    {
        timer16_1.resetFlags();
        if (calls.size() == 1)
        {
            timer16_1.match(TIMER_MATCH_MAT0, 104 - BitPulseTime);
            timer16_1.match(TIMER_MATCH_MAT2, 104 - 1);
        }
    };
    const auto t0 = startBusPulse(104);
    sim::advance(400);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 2);
    REQUIRE(low.size() == 2);
    // first pulse 104..139: MAT2 event at TC == 138, TC = 0 (new cycle, TX LOW) at 139;
    // second pulse 139 + 69 .. 139 + 104
    CHECK(high[0] == t0 + 104);
    CHECK(low[0] == t0 + 139);
    CHECK(high[1] == t0 + 139 + 69);
    CHECK(low[1] == t0 + 139 + 104);
}

// ---------------------------------------------------------------------------------------------
// Bus PWM: TX edges and long interrupts (R16, order 051), diagnostics counters of the TX edges
// ---------------------------------------------------------------------------------------------

namespace
{

    // The timer interrupt handler takes durationUs for a capture event (busy wait), like bus.cpp
    // from the flash with a cold cache; for a match it only resets the flags
    void slowCaptureHandler(const uint32_t durationUs)
    {
        handlerHook = [durationUs](uint8_t)
        {
            const bool capture = timer16_1.flag(CAP0);
            timer16_1.resetFlags();
            if (capture)
            {
                const uint32_t endUs = nowUs() + durationUs;
                while (static_cast<int32_t>(endUs - nowUs()) > 0)
                {
                    // the handler is busy
                }
            }
        };
    }

} // namespace

TEST_CASE("RP2350 timer: bus PWM edge counters of an exact pulse", "[rp2350][timer][bus]")
{
    fresh();
    startBusPulse(104);
    sim::advance(300);

    REQUIRE(txEdges(true).size() == 1);
    REQUIRE(txEdges(false).size() == 1);
    CHECK(stats.txRiseLateUs[0] == 1);
    CHECK(stats.txFallLateUs[0] == 1);
    CHECK(stats.txFirstFallLateUs[0] == 1);
    CHECK(stats.txWidthUs[BitPulseTime - 33] == 1);
    CHECK(stats.txFirstWidthUs[BitPulseTime - 33] == 1);
    CHECK(stats.lateEdgesScheduledLate == 0);
    CHECK(stats.lateEdgesAfterRx == 0);
    CHECK(stats.lateEdgesOther == 0);
#if defined(SBLIB_RP2350_PIO_BUS)
    CHECK(stats.pioTxConfirmed == 2);
    CHECK(stats.pioTxLateTargets == 0);
    CHECK(stats.pioTxResyncs == 0);
#else
    CHECK(stats.edgeIrqLatencyUs[0] == 2);
#endif
}

TEST_CASE("RP2350 timer: bus PWM edge counters with alarm interrupt latency", "[rp2350][timer][bus]")
{
    // 6 us latency, 2 us more than PwmEdgeLeadUs: the pulse starts 2 us late (cause: interrupt entry),
    // the end alarm has the same latency and ends the pulse 35 us after the actual start
    fresh();
    sim::setAlarmLatency(6);
    const auto t0 = startBusPulse(104);
    sim::advance(300);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 1);
    REQUIRE(low.size() == 1);
#if defined(SBLIB_RP2350_PIO_BUS)
    // The state machine switches both edges at their time, independent of the alarm interrupt
    CHECK(high[0] == t0 + 104);
    CHECK(low[0] - high[0] == BitPulseTime);
    CHECK(stats.txRiseLateUs[0] == 1);
    CHECK(stats.txFallLateUs[0] == 1);
    CHECK(stats.txWidthUs[BitPulseTime - 33] == 1);
    CHECK(stats.lateEdgesOther == 0);
    CHECK(stats.pioTxConfirmed == 2);
#else
    CHECK(high[0] == t0 + 106);
    CHECK(low[0] - high[0] == BitPulseTime);
    CHECK(stats.txRiseLateUs[2] == 1);
    CHECK(stats.txFallLateUs[0] == 1);
    CHECK(stats.txWidthUs[BitPulseTime - 33] == 1);
    CHECK(stats.lateEdgesOther == 1);
    CHECK(stats.lateEdgesAfterRx == 0);
    CHECK(stats.edgeIrqLatencyUs[6] == 2);
#endif
}

TEST_CASE("RP2350 timer: bus PWM edge counters - pulse end during a long RX edge interrupt", "[rp2350][timer][bus]")
{
    // The own echo of the pulse start runs the timer interrupt handler for 40 us. The TX edge interrupt
    // (higher priority) interrupts it and ends the pulse at its exact time.
    fresh();
    slowCaptureHandler(40);
    const auto t0 = startBusPulse(104);
    sim::at(t0 + 105, [] { sim::fallingEdge(PIN_EIB_RX); }); // own echo
    sim::advance(300);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 1);
    REQUIRE(low.size() == 1);
    CHECK(high[0] == t0 + 104);
    CHECK(low[0] == t0 + 139);
    CHECK(stats.txFallLateUs[0] == 1);
    CHECK(stats.txFirstFallLateUs[0] == 1);
    CHECK(stats.txFirstWidthUs[BitPulseTime - 33] == 1);
    CHECK(stats.lateEdgesAfterRx == 0);
    CHECK(stats.lateEdgesScheduledLate == 0);
    CHECK(stats.lateEdgesOther == 0);
    CHECK(stats.txEdgeFallbacks == 0);
    CHECK(stats.rxIrqUs[5] == 1); // 32..63 us
    CHECK(stats.rxIrqMaxUs == 40);
#if !defined(SBLIB_RP2350_PIO_BUS) // PIO: the echo can come before the match processing of the pulse start
    CHECK(stats.rxIrqFirstEchoMaxUs == 40);
#endif
}

TEST_CASE("RP2350 timer: bus PWM - a long RX edge interrupt does not delay the pulse end (R16)", "[rp2350][timer][bus]")
{
    // LPC11xx: the PWM output is hardware, the pulse ends after 35 us whatever interrupt runs.
    // R16 (order 051, fixed): the TX edge interrupt has a higher priority than the RX edge interrupt.
    for (const uint32_t handlerUs: {20u, 33u, 40u, 80u})
    {
        DYNAMIC_SECTION("handler " << handlerUs << " us")
        {
            fresh();
            slowCaptureHandler(handlerUs);
            const auto t0 = startBusPulse(104);
            sim::at(t0 + 105, [] { sim::fallingEdge(PIN_EIB_RX); }); // own echo
            sim::advance(400);

            const auto high = txEdges(true);
            const auto low = txEdges(false);
            REQUIRE(high.size() == 1);
            REQUIRE(low.size() == 1);
            CHECK(high[0] == t0 + 104);
            CHECK(low[0] - high[0] == BitPulseTime);
        }
    }
}

TEST_CASE("RP2350 timer: bus PWM - the match processing after the pulse end keeps the RESET time",
          "[rp2350][timer][bus]")
{
    // The RX edge interrupt (40 us) delays the alarm interrupt of the pulse end (MAT2 with RESET and
    // INTERRUPT) until t0 + 145. The counter was reset at the MAT2 event + 1 (t0 + 139), as on the LPC11xx,
    // so the handler reads 6.
    fresh();
    slowCaptureHandler(40);
    const auto t0 = startBusPulse(104);
    sim::at(t0 + 105, [] { sim::fallingEdge(PIN_EIB_RX); }); // own echo
    sim::advance(300);

    REQUIRE(calls.size() >= 2); // capture (echo), MAT2 (then MAT2 again every RESET cycle)
    CHECK(calls[0].timeUs == t0 + 105);
    CHECK(calls[1].timeUs == t0 + 145);
    CHECK(calls[1].value == 6);
}

TEST_CASE("RP2350 timer: bus PWM - a long alarm interrupt handler does not delay the pulse end", "[rp2350][timer][bus]")
{
    // MAT1 with INTERRUPT during the pulse: its handler (bus.cpp in the alarm interrupt) takes 40 us
    fresh();
    const auto t0 = startBusPulse(104);
    timer16_1.match(TIMER_MATCH_MAT1, 110);
    timer16_1.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    handlerHook = [](uint8_t)
    {
        const bool mat1 = timer16_1.flag(TIMER_MATCH_MAT1);
        timer16_1.resetFlags();
        if (mat1)
        {
            const uint32_t endUs = nowUs() + 40;
            while (static_cast<int32_t>(endUs - nowUs()) > 0)
            {
                // the handler is busy
            }
        }
    };
    sim::advance(300);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 1);
    REQUIRE(low.size() == 1);
    CHECK(high[0] == t0 + 104);
    CHECK(low[0] == t0 + 139);
    CHECK(stats.txEdgeFallbacks == 0);
}

#if !defined(SBLIB_RP2350_PIO_BUS)
TEST_CASE("RP2350 timer: bus PWM - an old TX edge interrupt does not switch a moved edge", "[rp2350][timer][bus]")
{
    // The TX edge alarm fires (t0 + 100) while the interrupts are disabled, then the bus moves the pulse
    // start to 200. The NVIC still runs the edge interrupt once after the lock: it must wait for the new edge.
    fresh();
    const auto t0 = startBusPulse(104);
    sim::advance(98);
    noInterrupts();
    sim::advance(3);
    timer16_1.match(TIMER_MATCH_MAT0, 200);
    timer16_1.match(TIMER_MATCH_MAT2, 200 + BitPulseTime - 1);
    interrupts();
    CHECK(txEdges(true).empty());
    sim::advance(300);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 1);
    REQUIRE(low.size() == 1);
    CHECK(high[0] == t0 + 200);
    CHECK(low[0] - high[0] == BitPulseTime);
    CHECK(stats.txEdgeFallbacks == 0);
}
#else
TEST_CASE("RP2350 timer PIO: a planned pulse start moved at least 3 us before it is replaced",
          "[rp2350][timer][bus][pio]")
{
    // The bus moves the pulse start to 200 while the interrupts are disabled, 10 us before the planned start
    fresh();
    const auto t0 = startBusPulse(104);
    sim::advance(90);
    noInterrupts();
    sim::advance(4);
    timer16_1.match(TIMER_MATCH_MAT0, 200);
    timer16_1.match(TIMER_MATCH_MAT2, 200 + BitPulseTime - 1);
    interrupts();
    sim::advance(300);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 1);
    REQUIRE(low.size() == 1);
    CHECK(high[0] == t0 + 200);
    CHECK(low[0] - high[0] == BitPulseTime);
    CHECK(stats.pioTxStrayHigh == 0);
    CHECK(stats.pioTxResyncs == 0);
}

TEST_CASE("RP2350 timer PIO: a pulse start less than 3 us ahead is fixed, the stray pulse ends at once",
          "[rp2350][timer][bus][pio]")
{
    // LPC11xx: a new MR0 applies at once. The state machine reads its FIFO once per microsecond, so an edge
    // less than PioTargetLeadUs ahead stays; the emulation then ends the stray pulse as soon as possible.
    fresh();
    const auto t0 = startBusPulse(104);
    sim::advance(102);
    timer16_1.match(TIMER_MATCH_MAT0, 200);
    timer16_1.match(TIMER_MATCH_MAT2, 200 + BitPulseTime - 1);
    sim::advance(300);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 2);
    REQUIRE(low.size() == 2);
    CHECK(high[0] == t0 + 104);
    CHECK(low[0] - high[0] <= static_cast<uint64_t>(PioTargetLeadUs) + 2);
    CHECK(high[1] == t0 + 200);
    CHECK(low[1] - high[1] == BitPulseTime);
    CHECK(stats.pioTxStrayHigh == 1);
    CHECK(stats.pioTxResyncs == 0);
}
#endif

// ---------------------------------------------------------------------------------------------
// Capture (timer16_1 CAP0 = PIN_EIB_RX)
// ---------------------------------------------------------------------------------------------

TEST_CASE("RP2350 timer: capture latches the value; flag and handler only with INTERRUPT", "[rp2350][timer][capture]")
{
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE);
    timer16_1.interrupts();
    timer16_1.start();

    sim::advance(250);
    sim::fallingEdge(PIN_EIB_RX);
    CHECK(timer16_1.capture(CAP0) == 250);
    CHECK_FALSE(timer16_1.flag(CAP0));
    CHECK(calls.empty());

    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    sim::advance(100);
    sim::fallingEdge(PIN_EIB_RX);
    CHECK(timer16_1.capture(CAP0) == 350);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].flags == Timer::flagMask(CAP0));
}

TEST_CASE("RP2350 timer: capture without FALLING_EDGE ignores edges, old edges are discarded",
          "[rp2350][timer][capture]")
{
    fresh();
    timer16_1.begin();
    timer16_1.interrupts();
    timer16_1.start();
    sim::advance(100);
    sim::fallingEdge(PIN_EIB_RX); // capture not enabled: latched in the GPIO block only
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    CHECK(calls.empty());
    CHECK(timer16_1.capture(CAP0) == 0);
}

TEST_CASE("RP2350 timer: edges closer than 8 us are captured (no spike filter)", "[rp2350][timer][capture]")
{
    // S3 (orders 047/050): as the LPC11xx the emulation captures every edge, bus.cpp checks short
    // pulses itself. The diagnostics counters only count the short distances.
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE);
    timer16_1.start();

    sim::advance(100);
    sim::fallingEdge(PIN_EIB_RX);
    CHECK(timer16_1.capture(CAP0) == 100);
    sim::advance(5);
    sim::fallingEdge(PIN_EIB_RX);
    CHECK(timer16_1.capture(CAP0) == 105);
    sim::advance(3);
    sim::fallingEdge(PIN_EIB_RX);
    CHECK(timer16_1.capture(CAP0) == 108);

    timer16_1.restart(); // a new time base, the distance to the previous edge still counts
    sim::advance(3);
    sim::fallingEdge(PIN_EIB_RX);
    CHECK(timer16_1.capture(CAP0) == 3);

    CHECK(stats.captureEvents == 4);
    CHECK(stats.shortGapCaptures == 3);
}

TEST_CASE("RP2350 timer: an edge one RESET cycle later at a close timer value is captured", "[rp2350][timer][capture]")
{
    // S1 (orders 041/042): the former spike filter compared timer values. With a match RESET every bit
    // time (bus.cpp while sending), an edge 104 us after the last accepted one has the same timer value
    // and was dropped, one at +106 us (value +2) too. Since order 050 there is no filter at all.
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.matchMode(TIMER_MATCH_MAT2, RESET);
    timer16_1.match(TIMER_MATCH_MAT2, 103); // RESET cycle 104 us, one bit time
    timer16_1.interrupts();
    timer16_1.start();

    sim::advance(69);
    sim::fallingEdge(PIN_EIB_RX); // timer value 69
    sim::advance(104);
    REQUIRE(timer16_1.value() == 69);
    sim::fallingEdge(PIN_EIB_RX); // next cycle, timer value 69 again
    sim::advance(106);
    REQUIRE(timer16_1.value() == 71);
    sim::fallingEdge(PIN_EIB_RX); // next cycle, timer value 71

    std::vector<unsigned> captures; // timer values of the capture interrupts
    for (const auto& c: calls)
    {
        if (c.flags & Timer::flagMask(CAP0))
        {
            captures.push_back(c.value);
        }
    }
    CHECK(captures == std::vector<unsigned>{69, 69, 71});
}

TEST_CASE("RP2350 timer: edges closer than 8 us are captured also over a RESET", "[rp2350][timer][capture]")
{
    // An edge 5 us after the previous one, with a match RESET in between (lower timer value)
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.matchMode(TIMER_MATCH_MAT2, RESET);
    timer16_1.match(TIMER_MATCH_MAT2, 103);
    timer16_1.interrupts();
    timer16_1.start();

    sim::advance(101);
    sim::fallingEdge(PIN_EIB_RX); // timer value 101
    sim::advance(5);
    REQUIRE(timer16_1.value() == 2);
    sim::fallingEdge(PIN_EIB_RX); // 5 us later, after the RESET: a spike, captured
    sim::advance(3);
    sim::fallingEdge(PIN_EIB_RX); // 3 us later: captured

    std::vector<unsigned> captures;
    for (const auto& c: calls)
    {
        if (c.flags & Timer::flagMask(CAP0))
        {
            captures.push_back(c.value);
        }
    }
    CHECK(captures == std::vector<unsigned>{101, 2, 5});
    CHECK(stats.shortGapCaptures == 2);
    CHECK(stats.shortGapUs[3] == 1);
    CHECK(stats.shortGapUs[5] == 1);
}

TEST_CASE("RP2350 timer: edges closer than 8 us are counted with their distance", "[rp2350][timer][capture]")
{
    // Diagnostics (SBLIB_RP2350_TIMER_STATS): edges closer than 8 us to the previous one, all captured
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE);
    timer16_1.start();

    sim::advance(100);
    sim::fallingEdge(PIN_EIB_RX); // first edge: no distance
    CHECK(stats.shortGapCaptures == 0);
    sim::advance(5);
    sim::fallingEdge(PIN_EIB_RX); // 5 us: counted
    sim::advance(1);
    sim::fallingEdge(PIN_EIB_RX); // 1 us: counted
    sim::advance(2);
    sim::fallingEdge(PIN_EIB_RX); // 2 us: counted
    sim::fallingEdge(PIN_EIB_RX); // same microsecond: counted (PIO: the same time stamp, one capture)
    sim::advance(8);
    sim::fallingEdge(PIN_EIB_RX); // 8 us: not counted
    sim::advance(7);
    sim::fallingEdge(PIN_EIB_RX); // 7 us: counted
    CHECK(timer16_1.capture(CAP0) == 123);

#if defined(SBLIB_RP2350_PIO_BUS)
    CHECK(stats.captureEvents == 6);
    CHECK(stats.shortGapCaptures == 4);
    CHECK(stats.rxEdgeDuplicates == 1);
    CHECK(stats.minShortGapUs == 1);
    CHECK(stats.shortGapUs[0] == 0);
#else
    CHECK(stats.captureEvents == 7);
    CHECK(stats.shortGapCaptures == 5);
    CHECK(stats.minShortGapUs == 0);
    CHECK(stats.shortGapUs[0] == 1);
#endif
    CHECK(stats.shortGapCapturesInTx == 0);
    CHECK(stats.shortGapUs[1] == 1);
    CHECK(stats.shortGapUs[2] == 1);
    CHECK(stats.shortGapUs[5] == 1);
    CHECK(stats.shortGapUs[6] == 0);
    CHECK(stats.shortGapUs[7] == 1);
}

TEST_CASE("RP2350 timer: edges closer than 8 us during an own TX pulse are counted", "[rp2350][timer][capture]")
{
    // Pulse t0 + 104 .. t0 + 139 (TX HIGH). An edge during the pulse and one after it, 3 and 4 us
    // after the previous edge: only the first one counts as "during an own TX pulse".
    fresh();
    const auto t0 = startBusPulse(104);
    sim::at(t0 + 105, [] { sim::fallingEdge(PIN_EIB_RX); }); // own echo
    sim::at(t0 + 108, [] { sim::fallingEdge(PIN_EIB_RX); }); // during the pulse: counted, in TX
    sim::at(t0 + 150, [] { sim::fallingEdge(PIN_EIB_RX); }); // after the pulse
    sim::at(t0 + 154, [] { sim::fallingEdge(PIN_EIB_RX); }); // after the pulse: counted
    sim::advance(300);

    const auto high = txEdges(true);
    const auto low = txEdges(false);
    REQUIRE(high.size() == 1);
    REQUIRE(low.size() == 1);
    REQUIRE(high[0] == t0 + 104);
    REQUIRE(low[0] == t0 + 139);
    CHECK(stats.captureEvents == 4);
    CHECK(stats.shortGapCaptures == 2);
    CHECK(stats.shortGapCapturesInTx == 1);
    CHECK(stats.minShortGapUs == 3);
    CHECK(stats.shortGapUs[3] == 1);
    CHECK(stats.shortGapUs[4] == 1);
}

TEST_CASE("RP2350 timer: capture during a global interrupt lock is handled after the lock", "[rp2350][timer][capture]")
{
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.interrupts();
    timer16_1.start();
    sim::advance(100);
    noInterrupts();
    sim::fallingEdge(PIN_EIB_RX);
    sim::advance(20);
    CHECK(calls.empty());
    interrupts();
    REQUIRE(calls.size() == 1);
#if defined(SBLIB_RP2350_PIO_BUS)
    // PIO and DMA: the time of the edge, as the LPC11xx capture hardware (R18)
    CHECK(timer16_1.capture(CAP0) == 100);
#else
    // Software capture: the value is the time of the handler, not of the edge (latency, R2)
    CHECK(timer16_1.capture(CAP0) == 120);
#endif
}

// ---------------------------------------------------------------------------------------------
// begin()/end(), priorities
// ---------------------------------------------------------------------------------------------

TEST_CASE("RP2350 timer: begin() claims one alarm per timer, end() releases it", "[rp2350][timer]")
{
    fresh();
    timer16_0.begin();
    timer16_1.begin();
    timer32_0.begin();
    timer32_1.begin();
    CHECK(timers[TIMER16_0].alarmNum != timers[TIMER16_1].alarmNum);
    CHECK(timers[TIMER32_0].alarmNum != timers[TIMER32_1].alarmNum);

    timer16_1.begin(); // second begin() is a no-op
    timer16_1.end();
    CHECK(timers[TIMER16_1].alarmNum == -1);
    timer16_1.begin();
    CHECK(timers[TIMER16_1].alarmNum >= 0);

    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.interrupts();
    timer16_1.start();
    timer16_1.end();
    sim::advance(10);
    sim::fallingEdge(PIN_EIB_RX);
    CHECK(calls.empty());
}

TEST_CASE("RP2350 timer: setIRQPriority() before begin() applies to alarm and GPIO interrupt", "[rp2350][timer]")
{
    fresh();
    timer16_1.setIRQPriority(InterruptPriority::highest);
    timer16_1.begin();
#if defined(SBLIB_RP2350_PIO_BUS)
    // PIO: no TX edge interrupt; alarm and capture interrupt (DMA) with the set priority, the GPIO interrupt
    // is not used
    constexpr uint RxIrq = DMA_IRQ_0;
    constexpr uint8_t BusIrqPriority = 0x00;
    CHECK(txEdge.alarmNum == -1);
    CHECK(sim::irqPriority(IO_IRQ_BANK0) == 0x80);
#else
    // Bus timer: the TX edge interrupt has the set priority, alarm and capture interrupt one hardware
    // level less (order 051)
    constexpr uint RxIrq = IO_IRQ_BANK0;
    constexpr uint8_t BusIrqPriority = 0x10;
    CHECK(sim::irqPriority(hardware_alarm_get_irq_num(txEdge.alarmNum)) == 0x00);
#endif
    CHECK(sim::irqPriority(hardware_alarm_get_irq_num(timers[TIMER16_1].alarmNum)) == BusIrqPriority);
    CHECK(sim::irqPriority(RxIrq) == BusIrqPriority);

    timer16_0.setIRQPriority(InterruptPriority::low);
    timer16_0.begin();
    CHECK(sim::irqPriority(hardware_alarm_get_irq_num(timers[TIMER16_0].alarmNum)) == 0xc0);
    CHECK(sim::irqPriority(RxIrq) == BusIrqPriority); // timer16_0 has no pins
}

#if !defined(SBLIB_RP2350_PIO_BUS)
TEST_CASE("RP2350 timer: the bus timer claims a second alarm for the TX edges, end() releases both", "[rp2350][timer]")
{
    fresh();
    timer16_1.begin();
    REQUIRE(timers[TIMER16_1].alarmNum >= 0);
    REQUIRE(txEdge.alarmNum >= 0);
    CHECK(txEdge.alarmNum != timers[TIMER16_1].alarmNum);
    const int firstAlarm = std::min(timers[TIMER16_1].alarmNum, txEdge.alarmNum);
    const int secondAlarm = std::max(timers[TIMER16_1].alarmNum, txEdge.alarmNum);

    timer16_1.end();
    CHECK(timers[TIMER16_1].alarmNum == -1);
    CHECK(txEdge.alarmNum == -1);
    // both alarms are free again
    CHECK(hardware_alarm_claim_unused(true) == firstAlarm);
    CHECK(hardware_alarm_claim_unused(true) == secondAlarm);
}
#else
TEST_CASE("RP2350 timer PIO: the bus timer claims one alarm and the TX state machine, end() releases them",
          "[rp2350][timer][pio]")
{
    fresh();
    timer16_1.begin();
    REQUIRE(timers[TIMER16_1].alarmNum >= 0);
    CHECK(txEdge.alarmNum == -1);
    REQUIRE(pioTx.sm >= 0);
    const uint sm = static_cast<uint>(pioTx.sm);
    CHECK(sim::pioSmClaimed(sm));
    CHECK(sim::pioSmEnabled(sm));
    CHECK(static_cast<int>(sm) != pioRx.sm);
    CHECK(sim::pioProgramWordsUsed() == TxProgramLength + RxProgramLength);
    CHECK(stats.pioTxSyncAttempts == 1);
    CHECK_FALSE(sim::pinLevel(PIN_EIB_TX));

    timer16_1.end();
    CHECK(pioTx.sm == -1);
    CHECK_FALSE(sim::pioSmClaimed(sm));
    CHECK(sim::pioProgramWordsUsed() == 0);
    // three alarms are free again
    CHECK(hardware_alarm_claim_unused(true) >= 0);
    CHECK(hardware_alarm_claim_unused(true) >= 0);
    CHECK(hardware_alarm_claim_unused(true) >= 0);
}

TEST_CASE("RP2350 timer PIO: begin() stops with fatalError() if clk_sys has no integer divider to 10 MHz",
          "[rp2350][timer][pio]")
{
    fresh();
    sim::setSysClockHz(133000000);
    CHECK_THROWS(timer16_1.begin());
    sim::setSysClockHz(SYS_CLK_HZ);
}
#endif

#if defined(SBLIB_RP2350_PIO_BUS)
// ---------------------------------------------------------------------------------------------
// KNX bus by PIO (SBLIB_RP2350_PIO_BUS, order 052, R18): RX edges by a state machine and DMA
// ---------------------------------------------------------------------------------------------

TEST_CASE("RP2350 timer PIO: begin() claims the RX state machine and two DMA channels, end() releases them",
          "[rp2350][timer][pio]")
{
    fresh();
    timer16_1.begin();
    REQUIRE(pioRx.sm >= 0);
    const uint sm = static_cast<uint>(pioRx.sm);
    const uint drain = static_cast<uint>(pioRx.drainChannel);
    const uint stamp = static_cast<uint>(pioRx.stampChannel);
    CHECK(sim::pioSmClaimed(sm));
    CHECK(sim::pioSmEnabled(sm));
    CHECK(sim::pioProgramWordsUsed() == RxProgramLength + TxProgramLength);
    CHECK(sim::dmaChannelClaimed(drain));
    CHECK(sim::dmaChannelClaimed(stamp));
    CHECK(drain != stamp);

    timer16_1.end();
    CHECK(pioRx.sm == -1);
    CHECK_FALSE(sim::pioSmClaimed(sm));
    CHECK(sim::pioProgramWordsUsed() == 0);
    CHECK_FALSE(sim::dmaChannelClaimed(drain));
    CHECK_FALSE(sim::dmaChannelClaimed(stamp));
}

TEST_CASE("RP2350 timer PIO: the capture value is the timer value of the edge, also with a late interrupt",
          "[rp2350][timer][pio][capture]")
{
    // The timer interrupt handler runs 30 us (match MAT1); an edge 5 us into it is captured with its own time.
    // The software capture of the GPIO interrupt would read the time after the handler (R2, R18).
    fresh();
    unsigned int captured = 0;
    uint64_t captureCallUs = 0;
    handlerHook = [&](uint8_t)
    {
        if (timer16_1.flag(TIMER_MATCH_MAT1))
        {
            const uint64_t endUs = sim::now() + 30;
            while (sim::now() < endUs)
            {
                (void)timer16_1.value(); // busy wait, the simulated time advances
            }
        }
        if (timer16_1.flag(CAP0))
        {
            captured = timer16_1.capture(CAP0);
            captureCallUs = sim::now();
        }
        timer16_1.resetFlags();
    };
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.matchMode(TIMER_MATCH_MAT1, INTERRUPT);
    timer16_1.match(TIMER_MATCH_MAT1, 100);
    timer16_1.interrupts();
    timer16_1.start();
    const auto t0 = sim::now(); // begin() takes some microseconds (start of the TX state machine)
    sim::atHardware(t0 + 105, [] { sim::fallingEdge(PIN_EIB_RX); });

    sim::advance(200);
    CHECK(captured == 105);
    CHECK(captureCallUs >= t0 + 130);
    CHECK(stats.rxEdgeDelayMaxUs >= 25);
    CHECK(stats.rxEdgeIrqs == 1);
}

TEST_CASE("RP2350 timer PIO: an edge before a RESET is captured with the value of the old cycle",
          "[rp2350][timer][pio][capture]")
{
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.matchMode(TIMER_MATCH_MAT2, RESET);
    timer16_1.match(TIMER_MATCH_MAT2, 99);
    timer16_1.interrupts();
    timer16_1.start();

    sim::advance(95);
    noInterrupts();
    sim::fallingEdge(PIN_EIB_RX); // timer value 95
    sim::advance(10);             // RESET at 99, processed after the lock before the edge
    interrupts();
    REQUIRE_FALSE(calls.empty());
    CHECK(calls.back().flags == Timer::flagMask(CAP0));
    CHECK(timer16_1.capture(CAP0) == 95);
    CHECK(timer16_1.value() == 5);
}

TEST_CASE("RP2350 timer PIO: an edge before value() and restart() keeps the value of the old cycle",
          "[rp2350][timer][pio][capture]")
{
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.interrupts();
    timer16_1.start();

    sim::advance(50);
    noInterrupts();
    sim::fallingEdge(PIN_EIB_RX); // timer value 50
    sim::advance(3);
    timer16_1.value(1000);
    interrupts();
    CHECK(timer16_1.capture(CAP0) == 50);

    sim::advance(20);
    noInterrupts();
    sim::fallingEdge(PIN_EIB_RX); // timer value 1020
    sim::advance(2);
    timer16_1.restart();
    sim::advance(2);
    interrupts();
    CHECK(timer16_1.capture(CAP0) == 1020);

    sim::advance(10);
    sim::fallingEdge(PIN_EIB_RX); // new cycle: 12 us after restart()
    CHECK(timer16_1.capture(CAP0) == 12);
}

TEST_CASE("RP2350 timer PIO: several edges before the interrupt, the last one is captured with one handler call",
          "[rp2350][timer][pio][capture]")
{
    fresh();
    timer16_1.begin();
    timer16_1.captureMode(CAP0, FALLING_EDGE | INTERRUPT);
    timer16_1.interrupts();
    timer16_1.start();

    noInterrupts();
    sim::advance(10);
    sim::fallingEdge(PIN_EIB_RX);
    sim::advance(10);
    sim::fallingEdge(PIN_EIB_RX);
    sim::advance(5);
    interrupts();
    REQUIRE(calls.size() == 1);
    CHECK(timer16_1.capture(CAP0) == 20);
}

TEST_CASE("RP2350 timer PIO: MAT0 re-armed during a running pulse does not start a second pulse",
          "[rp2350][timer][bus][pio]")
{
    // bus.cpp SEND_START_BIT syncs to a foreign edge captured 1 us before our pulse start, while our pulse runs
    // already: match(pwm, value + 1) and match(time, captureTime + 34). As on the LPC11xx the MAT0 match does not
    // change the HIGH output. The software capture without PIO reads the edge after our pulse start, so bus.cpp
    // does not sync there.
    fresh();
    const auto t0 = startBusPulse(104);
    sim::advance(104 + 3);
    REQUIRE(sim::pinLevel(PIN_EIB_TX));
    timer16_1.match(TIMER_MATCH_MAT0, timer16_1.value() + 1);
    timer16_1.match(TIMER_MATCH_MAT2, 103 + BitPulseTime - 1);

    sim::advance(300);
    REQUIRE(txEdges(true).size() == 1);
    REQUIRE(txEdges(false).size() == 1);
    CHECK(txEdges(true)[0] == t0 + 104);
    // As on the LPC11xx the pulse ends with the reset by MAT2, 35 us after the foreign edge (order 062)
    CHECK(txEdges(false)[0] == t0 + 103 + BitPulseTime);
}
#endif

#if defined(SBLIB_RP2350_PIO_BUS)
// ---------------------------------------------------------------------------------------------
// The PIO programs themselves, cycle by cycle (pio_cycle_model.h, RP2350 datasheet chapter PIO)
// ---------------------------------------------------------------------------------------------

namespace
{

    constexpr uint32_t CycleT0 = 1000000; // TIMERAWL in the first microsecond of the TX state machine

    /**
     * The TX program, started as by pioTxStartSynchronised(): X = -CycleT0, TX LOW, nothing planned. The
     * microsecond n of the state machine (cycles 10n .. 10n + 9) starts lagCycles after the tick of TIMERAWL.
     */
    struct TxProgramRun
    {
        explicit TxProgramRun(const unsigned lagCycles) :
            sm(txProgramInstructions, TxProgramLength, TxProgramStart, TxProgramWrap, TxProgramStart),
            lag(lagCycles)
        {
            buildTxProgram();
            sm = piomodel::StateMachine(txProgramInstructions, TxProgramLength, TxProgramStart, TxProgramWrap,
                                        TxProgramStart);
            sm.statusTxLessThan(2);
            sm.inputIsOutput = true; // IN base = OUT base = PIN_EIB_TX (pioTxBegin())
            sm.x = 0u - CycleT0;
        }

        // TIMERAWL as the CPU reads it now
        [[nodiscard]] uint32_t timerawl() const
        {
            return CycleT0 + static_cast<uint32_t>((sm.cycle() + lag) / TxCyclesPerUs);
        }

        void runToCycle(const uint64_t cycle)
        {
            while (sm.cycle() < cycle)
            {
                sm.step();
                // X follows TIMERAWL: at the start of the microsecond n it is -(CycleT0 + n)
                if (sm.cycle() % TxCyclesPerUs == 0)
                {
                    const uint32_t n = static_cast<uint32_t>(sm.cycle() / TxCyclesPerUs);
                    REQUIRE(sm.x == 0u - (CycleT0 + n));
                }
            }
        }

        // The CPU writes a pair of edge times (0 = none), like pioTxSend()
        void send(const uint32_t firstUs, const uint32_t secondUs)
        {
            REQUIRE(sm.txFifo.size() <= 2);
            sm.txFifo.push_back(0u - firstUs);
            sm.txFifo.push_back(0u - secondUs);
        }

        // Cycle (counted from 1) in which TX changes when the edge is at the microsecond edgeUs: the 4th one
        static uint64_t edgeCycle(const uint32_t edgeUs)
        {
            return static_cast<uint64_t>(edgeUs - CycleT0) * TxCyclesPerUs + 4;
        }

        piomodel::StateMachine sm;
        unsigned lag;
    };

    // Encoded "none" for TxProgramRun::send()
    constexpr uint32_t NoEdge = 0;

} // namespace

TEST_CASE("RP2350 PIO TX program: 10 cycles per microsecond on every path, X follows TIMERAWL", "[rp2350][pio]")
{
    // A pair written in every cycle position of the microsecond, edges 3 us ahead: every path of the loop
    // (idle, wait, command, edge) runs, X must stay -TIMERAWL (checked in runToCycle())
    for (unsigned offset = 0; offset < TxCyclesPerUs; ++offset)
    {
        TxProgramRun run(1);
        run.runToCycle(50 * TxCyclesPerUs + offset);
        const uint32_t w = run.timerawl();
        run.send(w + 3, w + 3 + BitPulseTime);
        run.runToCycle(200 * TxCyclesPerUs);
        CHECK(run.sm.outputChanges.size() == 2);
    }
}

TEST_CASE("RP2350 PIO TX program: the edges come in the 4th cycle of their microsecond, confirmation -t",
          "[rp2350][pio]")
{
    for (const unsigned lag: {0u, 1u, 2u})
    {
        for (unsigned offset = 0; offset < TxCyclesPerUs; ++offset)
        {
            INFO("lag " << lag << " cycles, written in cycle " << offset << " of the microsecond");
            TxProgramRun run(lag);
            run.runToCycle(20 * TxCyclesPerUs + offset);
            const uint32_t riseUs = run.timerawl() + PioTargetLeadUs;
            const uint32_t fallUs = riseUs + BitPulseTime;
            run.send(riseUs, fallUs);
            run.runToCycle(100 * TxCyclesPerUs);

            REQUIRE(run.sm.outputChanges.size() == 2);
            CHECK(run.sm.outputChanges[0] == TxProgramRun::edgeCycle(riseUs));
            CHECK(run.sm.outputChanges[1] == TxProgramRun::edgeCycle(fallUs));
            CHECK(run.sm.outputChanges[1] - run.sm.outputChanges[0] == BitPulseTime * TxCyclesPerUs);
            CHECK_FALSE(run.sm.outputPin);
            REQUIRE(run.sm.rxFifo.size() == 2);
            CHECK(run.sm.rxFifo[0] == 0u - riseUs);
            CHECK(run.sm.rxFifo[1] == 0u - fallUs);
        }
    }
}

TEST_CASE("RP2350 PIO TX program: 2 us lead are enough, 1 us is not (PioTargetLeadUs = 3 keeps 1 us margin)",
          "[rp2350][pio]")
{
    unsigned missedWithOneUs = 0;
    for (const unsigned lag: {0u, 1u, 2u})
    {
        for (unsigned offset = 0; offset < TxCyclesPerUs; ++offset)
        {
            for (const uint32_t leadUs: {1u, 2u})
            {
                TxProgramRun run(lag);
                run.runToCycle(20 * TxCyclesPerUs + offset);
                const uint32_t riseUs = run.timerawl() + leadUs;
                run.send(riseUs, riseUs + BitPulseTime);
                run.runToCycle(100 * TxCyclesPerUs);
                const bool exact =
                        run.sm.outputChanges.size() == 2 && run.sm.outputChanges[0] == TxProgramRun::edgeCycle(riseUs);
                if (leadUs == 2)
                {
                    INFO("lag " << lag << ", cycle " << offset);
                    CHECK(exact);
                }
                else if (!exact)
                {
                    ++missedWithOneUs; // X passed the edge time: the state machine would wait 71 minutes
                }
            }
        }
    }
    CHECK(missedWithOneUs > 0);
}

TEST_CASE("RP2350 PIO TX program: a new pair replaces the planned edges, (0, 0) cancels them", "[rp2350][pio]")
{
    SECTION("replace")
    {
        TxProgramRun run(1);
        run.runToCycle(20 * TxCyclesPerUs);
        const uint32_t w = run.timerawl();
        run.send(w + 10, w + 45);
        run.runToCycle(25 * TxCyclesPerUs + 7);
        run.send(w + 20, w + 55);
        run.runToCycle(120 * TxCyclesPerUs);
        REQUIRE(run.sm.outputChanges.size() == 2);
        CHECK(run.sm.outputChanges[0] == TxProgramRun::edgeCycle(w + 20));
        CHECK(run.sm.outputChanges[1] == TxProgramRun::edgeCycle(w + 55));
    }
    SECTION("cancel")
    {
        TxProgramRun run(1);
        run.runToCycle(20 * TxCyclesPerUs);
        const uint32_t w = run.timerawl();
        run.send(w + 10, w + 45);
        run.runToCycle(25 * TxCyclesPerUs + 3);
        run.send(NoEdge, NoEdge);
        run.runToCycle(120 * TxCyclesPerUs);
        CHECK(run.sm.outputChanges.empty());
        CHECK(run.sm.rxFifo.empty());
    }
    SECTION("the pulse end replaced while the pulse runs")
    {
        TxProgramRun run(2);
        run.runToCycle(20 * TxCyclesPerUs);
        const uint32_t w = run.timerawl();
        run.send(w + 10, w + 45);
        run.runToCycle(40 * TxCyclesPerUs + 5); // TX HIGH since w + 10
        REQUIRE(run.sm.outputPin);
        run.send(w + 30, NoEdge);
        run.runToCycle(120 * TxCyclesPerUs);
        REQUIRE(run.sm.outputChanges.size() == 2);
        CHECK(run.sm.outputChanges[1] == TxProgramRun::edgeCycle(w + 30));
        CHECK_FALSE(run.sm.outputPin);
    }
}

TEST_CASE("RP2350 PIO TX program: one word alone is no command, the pair is taken when complete", "[rp2350][pio]")
{
    TxProgramRun run(1);
    run.runToCycle(20 * TxCyclesPerUs);
    const uint32_t w = run.timerawl();
    run.sm.txFifo.push_back(0u - (w + 10));
    run.runToCycle(40 * TxCyclesPerUs); // the edge time passed: still nothing, the word waits
    CHECK(run.sm.outputChanges.empty());
    CHECK(run.sm.txFifo.size() == 1);
    run.sm.txFifo.clear();
    const uint32_t w2 = run.timerawl();
    run.send(w2 + 5, w2 + 40);
    run.runToCycle(120 * TxCyclesPerUs);
    REQUIRE(run.sm.outputChanges.size() == 2);
    CHECK(run.sm.outputChanges[0] == TxProgramRun::edgeCycle(w2 + 5));
}

TEST_CASE("RP2350 PIO RX program: one word per falling edge of the input pin", "[rp2350][pio]")
{
    buildRxProgram();
    piomodel::StateMachine sm(rxProgramInstructions, RxProgramLength, 0, RxProgramLength - 1, 0);
    sm.inputPin = false; // a low pin at the start is no edge
    for (int i = 0; i < 20; ++i)
    {
        sm.step();
    }
    CHECK(sm.rxFifo.empty());

    std::vector<uint64_t> fallCycles;
    unsigned pushes = 0;
    // 0 bits of 35 us and a spike of one cycle, clk_sys (150 cycles per microsecond)
    const std::vector<std::pair<uint64_t, bool>> levels = {
            {100, true},    {200, false}, {200 + 35 * 150, true}, {200 + 104 * 150, false}, {200 + 139 * 150, true},
            {30000, false}, {30001, true}};
    size_t next = 0;
    while (sm.cycle() < 40000)
    {
        if (next < levels.size() && sm.cycle() == levels[next].first)
        {
            sm.inputPin = levels[next].second;
            if (!levels[next].second)
            {
                fallCycles.push_back(sm.cycle());
            }
            ++next;
        }
        const size_t before = sm.rxFifo.size();
        sm.step();
        if (sm.rxFifo.size() != before)
        {
            ++pushes;
            // pushed within 2 cycles after the edge (plus 2 cycles input synchronisation in the hardware)
            REQUIRE_FALSE(fallCycles.empty());
            CHECK(sm.cycle() - fallCycles.back() <= 2);
        }
        if (sm.rxFifo.size() > 2)
        {
            sm.rxFifo.pop_front(); // DMA drains the FIFO
        }
    }
    CHECK(fallCycles.size() == 3);
    CHECK(pushes == 3);
}
#endif
