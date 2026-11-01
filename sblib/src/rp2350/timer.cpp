/*
 *  timer.cpp - Emulation of the LPC11xx timers (Timer class), RP2350 implementation.
 *
 *  The timer value is derived from the 1 MHz system timer (TIMER0): value = now - start,
 *  masked to 16 or 32 bit. Match and capture behave like the LPC11xx timers as far as the
 *  KNX bus (src/eib/bus.cpp) uses them:
 *
 *  - Match channels with INTERRUPT, RESET, STOP: one dedicated hardware alarm per timer is
 *    programmed to the next due match. The alarm interrupt sets the match flags, applies
 *    RESET/STOP and calls the interrupt handler of the timer (e.g. TIMER16_1_IRQHandler).
 *    No SDK alarm pool and no SDK alarm callback is used: the alarm interrupts come directly
 *    from the vector table to the code in RAM, the alarms are programmed by register.
 *  - A match event happens when the timer value reaches the match value. A match value that
 *    is already passed fires as soon as possible (the LPC11xx would wait for the counter wrap).
 *    After the event the channel matches again only when the counter reaches the match value
 *    the next time: after a RESET, after the 16 bit wrap (65536 us later), or after match(),
 *    value(), restart(). 32 bit timers: no wrap emulated (71 minutes).
 *  - RESET as on the LPC11xx: the timer holds the match value for one microsecond and is 0
 *    in the next one, so a RESET cycle lasts match value + 1 microseconds.
 *  - PWM of the bus timer (timer16_1): MAT0 starts the pulse (PIN_EIB_TX HIGH), the cycle
 *    reset by MAT2 ends it (PIN_EIB_TX LOW one microsecond after the MAT2 event, so the pulse
 *    lasts MAT2 + 1 - MAT0). MAT0 is one-shot: after it fired, the next match(MAT0, value)
 *    re-arms it; the match value stays readable as on the LPC11xx.
 *    The TX edges are switched like by the PWM hardware of the LPC11xx: a second hardware alarm
 *    of the bus timer fires PwmEdgeLeadUs before the next edge, its interrupt waits for the exact
 *    edge time and switches TX. It has a higher priority than the alarm (match processing) and
 *    capture interrupts of the bus timer, so a TX edge is exact also while they run the timer
 *    interrupt handler of the bus (bus.cpp, in flash). The match processing then follows in the
 *    alarm interrupt of the timer.
 *    Sentinels as used by the bus: MAT0 >= 0xffff and MAT2 >= 0xfffe are disabled.
 *  - Capture CAP0 of timer16_1: falling edge of PIN_EIB_RX via a raw GPIO interrupt handler.
 *    As with the LPC11xx the capture value is latched with FALLING_EDGE also without INTERRUPT;
 *    the capture flag and the interrupt handler need INTERRUPT. Every edge is captured as on the
 *    LPC11xx, there is no spike filter (bus.cpp checks short pulses itself). For diagnostics
 *    timerEmulationStats() counts edges closer than 8 us (system time) to the previous one.
 *  - A flag set while the timer interrupt is disabled calls the handler as soon as
 *    interrupts() enables it again, like a pending NVIC interrupt.
 *  - With the CMake option SBLIB_RP2350_PIO_BUS (default ON): a PIO state machine
 *    detects the falling edges of PIN_EIB_RX and DMA copies the system time (TIMERAWL) of each edge.
 *    The capture value is the timer value at that time, as latched by the LPC11xx capture hardware,
 *    independent of the interrupt latency. The DMA interrupt then sets the capture flag and calls the
 *    interrupt handler of the timer.
 *
 *  Timer logic taken over from the RP2354 bring-up (theport), the scheduling with the SDK
 *  alarm pool replaced by a dedicated hardware alarm.
 *
 *  Not emulated: prescaler (always 1 MHz), counterMode(), match pin actions CLEAR/SET/TOGGLE
 *  except the bus PWM, capture on other pins and timers.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "sblib/timer.h"
#include "sblib/interrupt.h"
#include "sblib/io_pin_names.h"
#include "sblib/utils.h"

#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
#if defined(SBLIB_RP2350_PIO_BUS)
#   include <hardware/clocks.h>
#   include <hardware/dma.h>
#   include <hardware/pio.h>
#   include <hardware/pio_instructions.h>
#endif

// Time critical interrupt code runs from RAM (no flash cache misses); host simulation: normal code
#if defined(__not_in_flash_func)
#   define TIMER_RAM_FUNC(name) __not_in_flash_func(name)
#else
#   define TIMER_RAM_FUNC(name) name
#endif

// Interrupt handlers of the timers, defined by the user of a timer, e.g. the KNX bus with
// BUS_TIMER_INTERRUPT_HANDLER(TIMER16_1_IRQHandler, ...). Weak, not every timer has one.
extern "C" void TIMER16_0_IRQHandler() __attribute__((weak));
extern "C" void TIMER16_1_IRQHandler() __attribute__((weak));
extern "C" void TIMER32_0_IRQHandler() __attribute__((weak));
extern "C" void TIMER32_1_IRQHandler() __attribute__((weak));

Timer timer16_0(TIMER16_0);
Timer timer16_1(TIMER16_1);
Timer timer32_0(TIMER32_0);
Timer timer32_1(TIMER32_1);

namespace
{
    constexpr int TimerCount = 4;
    constexpr int MatchChannels = 4;
    constexpr int CaptureChannels = 2;
    constexpr int PwmStartChannel = TIMER_MATCH_MAT0; // bus PWM: pulse start (TX HIGH)
    constexpr int PwmEndChannel = TIMER_MATCH_MAT2;   // bus PWM: pulse end (TX LOW)
    constexpr int MatchActions = INTERRUPT | RESET | STOP | CLEAR | SET | TOGGLE;
    constexpr unsigned int PulseEndBusyWaitUs = 4; // bus PWM: wait in the interrupt for a pulse end this close
    // Bus PWM: the alarm of a TX edge fires this much earlier, the interrupt waits for the exact edge
    // time and switches TX. Covers interrupt entry and short phases with disabled interrupts.
    constexpr unsigned int PwmEdgeLeadUs = 4;
    // Hardware priority of the alarm and capture interrupts of the bus timer: one of the 16 levels
    // below its TX edge interrupt (which has the priority set with setIRQPriority())
    constexpr uint8_t BelowTxEdgePriority = 0x10;

    // Match deadline: register value (compared with the timer value) or absolute time
    enum class DeadlineState : uint8_t
    {
        Register,
        Pending,
        Fired
    };

    struct TimerState
    {
        bool initialized = false;
        bool running = false;
        bool irqEnabled = false;
        int alarmNum = -1;
        InterruptPriority priority = InterruptPriority::medium;

        unsigned int prescaler = 0;
        uint32_t startUs = 0;          // system time at timer value 0
        unsigned int stoppedValue = 0; // timer value while stopped
        uint32_t stoppedCycle = 0;     // counter cycle (16 bit wraps) when stopped
        bool holdActive = false;       // after a RESET: the microsecond before startUs reads holdValue
        unsigned int holdValue = 0;    // match value of the RESET (the LPC11xx holds it for one tick)

        unsigned int matchValue[MatchChannels] = {0xffff, 0xffff, 0xffff, 0xffff};
        int matchModeValue[MatchChannels] = {};
        uint32_t deadlineUs[MatchChannels] = {};
        DeadlineState deadlineState[MatchChannels] = {};
        bool matchFired[MatchChannels] = {}; // register match fired in counter cycle firedCycle
        uint32_t firedCycle[MatchChannels] = {};
        bool pwmEnabled[MatchChannels] = {};
        bool matchLevel[MatchChannels] = {};
        bool pulseEndPending = false; // bus PWM: pulse ends at the counter reset after the MAT2 event
        uint32_t pulseEndUs = 0;
        uint32_t pulseStartUs = 0;     // bus PWM: actual start of the pulse (TX HIGH)
        unsigned int pulseWidthUs = 0; // bus PWM: nominal width MAT2 + 1 - MAT0

        unsigned int captureValue[CaptureChannels] = {};
        int captureModeValue[CaptureChannels] = {};
#if defined(SBLIB_RP2350_TIMER_STATS)
        uint32_t lastCaptureUs = 0; // system time of the previous capture edge (diagnostics only)
        bool hasCapture = false;
#endif
#if defined(SBLIB_RP2350_PIO_BUS)
        // Last restart of the counter (RESET, value(), restart()): capture of an edge before it
        uint32_t cycleChangeUs = 0; // system time from which startUs is valid
        uint32_t prevStartUs = 0;   // startUs before
        uint32_t pwmArmedUs = 0;    // system time of the last match(MAT0, value)
#endif

        volatile uint32_t flags = 0;
    };

    TimerState timers[TimerCount];

    // Bus PWM: the next TX edge, switched by txEdgeIrq() at its exact time. Planned by scheduleNextAlarm()
    // with disabled interrupts; a switched edge is taken over by the match processing (alarmCallback()).
    struct TxEdge
    {
        int alarmNum = -1;
        volatile bool armed = false; // edge planned, alarm programmed to alarmUs
        volatile bool rising = false;
        volatile uint32_t edgeUs = 0;
        volatile uint32_t alarmUs = 0;
        volatile bool switched = false; // edge switched, not taken over yet
        volatile bool switchedRising = false;
        volatile uint32_t switchedUs = 0;
    };

    TxEdge txEdge;

#if defined(SBLIB_RP2350_PIO_BUS)
    // KNX bus by PIO: PIO0, because digital_pin.cpp gives PIN_EIB_TX the function of PIO0
    PIO const BusPio = pio0;
    constexpr uint RxDmaIrq = DMA_IRQ_0;

    /**
     * RX by PIO and DMA: the state machine pushes one word per falling edge of PIN_EIB_RX. DMA channel
     * drainChannel takes it from the RX FIFO and triggers stampChannel, which copies TIMERAWL to edgeUs and
     * triggers drainChannel again. The end of stampChannel raises RxDmaIrq.
     */
    struct PioRx
    {
        int sm = -1;
        int programOffset = -1;
        int drainChannel = -1;
        int stampChannel = -1;
        uint32_t drainSink = 0;       // the words of the RX FIFO (not used)
        volatile uint32_t edgeUs = 0; // system time of the last falling edge, written by DMA
        bool hasEdge = false;         // lastEdgeUs is valid
        uint32_t lastEdgeUs = 0;      // the last edge time processed
    };

    PioRx pioRx;

    // RX program: one word into the RX FIFO per falling edge of the input pin
    constexpr uint RxProgramLength = 3;
    uint16_t rxProgramInstructions[RxProgramLength];
    pio_program_t rxProgram = {};

    /**
     * TX by a state machine with its own copy of the system time: X = -TIMERAWL, decremented once per
     * microsecond (TxCyclesPerUs cycles at clk_sys / divider). ISR holds the time of the next edge, OSR the one
     * after it (encoded -t, 0 = none): a pulse start with its end. When X reaches ISR, the state machine toggles
     * PIN_EIB_TX, pushes -t as confirmation and continues with OSR. The emulation gives it pairs of edge times.
     */
    struct PioTxEdge
    {
        uint32_t edgeUs = 0;   // time of the edge
        uint32_t wantedUs = 0; // time the emulation wanted (edgeUs is later for a late target)
        bool rising = false;
    };

    struct PioTx
    {
        int sm = -1;
        int programOffset = -1;
        PioTxEdge planned[2]; // edges given to the state machine, in time order
        uint8_t plannedCount = 0;
        bool high = false; // TX level of the state machine after the last edge taken over
    };

    PioTx pioTx;

    constexpr uint32_t TxCyclesPerUs = 10;
    constexpr uint32_t TxSmHz = 1000000 * TxCyclesPerUs;
    // An edge time must be this far ahead: the state machine reads its TX FIFO once per microsecond
    constexpr int32_t PioTargetLeadUs = 3;
    // The state machine switches an edge about 0.5 us into its microsecond: wait this long for the confirmation
    constexpr int32_t PioConfirmWaitUs = 3;
    constexpr int PioSyncAttempts = 5;
    // A pulse end planned with its start: at most this long after it (longer: planned after the start)
    constexpr uint32_t PioMaxPlannedPulseUs = 200;

    constexpr uint TxProgramLength = 17;
    constexpr uint TxProgramStart = 4; // loop, the wrap target
    constexpr uint TxProgramWrap = 12; // tick, the end of the loop
    uint16_t txProgramInstructions[TxProgramLength];
    pio_program_t txProgram = {};
#endif

#if defined(SBLIB_RP2350_TIMER_STATS)
    volatile TimerEmulationStats stats = {};
#   define STAT_INC(field) (++stats.field)
#else
#   define STAT_INC(field) ((void)0)
#endif
    int8_t timerOfAlarm[NUM_GENERIC_TIMERS * NUM_ALARMS] = {-1, -1, -1, -1, -1, -1, -1, -1};

    inline uint32_t nowUs()
    {
        return timer_hw->timerawl;
    }

    // Hardware alarms by register (TIMER0): no SDK functions in flash and no spin lock in the interrupt path

    // Program an alarm to targetUs. A time already reached raises its interrupt at once.
    // Returns true in this case.
    bool TIMER_RAM_FUNC(armAlarm)(const int alarmNum, const uint32_t targetUs)
    {
        const uint32_t mask = 1u << alarmNum;
        timer_hw->intr = mask;                // clear an old alarm event
        timer_hw->alarm[alarmNum] = targetUs; // writing arms the alarm
        if (static_cast<int32_t>(targetUs - nowUs()) > 0 || !(timer_hw->armed & mask))
        {
            return false;
        }
        // The time passed while programming: the alarm would fire only after the 32 bit wrap
        timer_hw->armed = mask;
        hw_set_bits(&timer_hw->intf, mask);
        return true;
    }

    void TIMER_RAM_FUNC(cancelAlarm)(const int alarmNum)
    {
        const uint32_t mask = 1u << alarmNum;
        timer_hw->armed = mask;
        hw_clear_bits(&timer_hw->intf, mask);
        timer_hw->intr = mask;
    }

    // Raise the interrupt of an alarm now (the interrupt handlers check what is due)
    void TIMER_RAM_FUNC(forceAlarm)(const int alarmNum)
    {
        hw_set_bits(&timer_hw->intf, 1u << alarmNum);
    }

    void TIMER_RAM_FUNC(acknowledgeAlarm)(const int alarmNum)
    {
        const uint32_t mask = 1u << alarmNum;
        hw_clear_bits(&timer_hw->intf, mask);
        timer_hw->intr = mask;
    }

    inline uint32_t timerMask(const uint8_t timerNum)
    {
        uint32_t mask = 0xffffu;
        if (timerNum >= TIMER32_0)
        {
            mask = 0xffffffffu;
        }
        return mask;
    }

    /**
     * Position of the counter: the timer value, or -1 in the microsecond after a RESET event
     * (the timer still reads the match value, the next tick is 0).
     */
    inline int64_t counterPos(const uint8_t timerNum, const uint32_t now)
    {
        const TimerState& st = timers[timerNum];
        if (!st.running)
        {
            return st.stoppedValue;
        }
        const uint32_t elapsed = now - st.startUs;
        if (st.holdActive && elapsed == 0xffffffffu)
        {
            return -1;
        }
        return elapsed & timerMask(timerNum);
    }

    inline unsigned int currentValue(const uint8_t timerNum, const uint32_t now)
    {
        const int64_t pos = counterPos(timerNum, now);
        if (pos >= 0)
        {
            return static_cast<unsigned int>(pos);
        }
        return timers[timerNum].holdValue;
    }

    // Counter cycle of a running 16 bit timer (number of wraps since startUs)
    inline uint32_t counterCycle(const TimerState& st, const uint32_t now)
    {
        return (now - st.startUs) >> 16;
    }

    // Timer value at the time atUs (not before startUs)
    inline unsigned int valueAt(const uint8_t timerNum, const uint32_t atUs)
    {
        return (atUs - timers[timerNum].startUs) & timerMask(timerNum);
    }

    inline bool hasBusPins(const uint8_t timerNum)
    {
        return timerNum == TIMER16_1;
    }

    // The counter starts a new cycle (RESET, value(), restart()): all match channels match again.
    void rearmMatches(TimerState& st)
    {
        for (int ch = 0; ch < MatchChannels; ++ch)
        {
            st.matchFired[ch] = false;
            if (st.deadlineState[ch] == DeadlineState::Fired)
            {
                st.deadlineState[ch] = DeadlineState::Register;
            }
        }
    }

#if defined(SBLIB_RP2350_PIO_BUS)
    // The counter starts a new cycle at changeUs (RESET, value(), restart()): keep the old start for the
    // capture of an edge before (captureValueAt())
    void TIMER_RAM_FUNC(noteCycleChange)(TimerState& st, const uint32_t changeUs)
    {
        st.prevStartUs = st.startUs;
        st.cycleChangeUs = changeUs;
    }
#endif

    void TIMER_RAM_FUNC(callIrqHandler)(const uint8_t timerNum)
    {
        void (*handler)() = nullptr;
        switch (timerNum)
        {
            case TIMER16_0:
                handler = TIMER16_0_IRQHandler;
                break;
            case TIMER16_1:
                handler = TIMER16_1_IRQHandler;
                break;
            case TIMER32_0:
                handler = TIMER32_0_IRQHandler;
                break;
            case TIMER32_1:
                handler = TIMER32_1_IRQHandler;
                break;
            default:
                break;
        }
        if (handler)
        {
            STAT_INC(handlerCalls);
            handler();
        }
    }

    bool TIMER_RAM_FUNC(matchActive)(const uint8_t timerNum, const int ch)
    {
        const TimerState& st = timers[timerNum];
        if (st.deadlineState[ch] != DeadlineState::Register)
        {
            return st.deadlineState[ch] == DeadlineState::Pending;
        }
        if ((ch == PwmEndChannel && st.matchValue[ch] >= 0xfffeu) ||
            (ch == PwmStartChannel && st.matchValue[ch] >= 0xffffu))
        {
            return false;
        }
        // A fired match of a 32 bit timer waits for match(), value(), restart() or a RESET
        return !(st.matchFired[ch] && timerNum >= TIMER32_0);
    }

    // Register match armed: not fired in the current counter cycle
    bool TIMER_RAM_FUNC(matchArmed)(const uint8_t timerNum, const int ch, const uint32_t now)
    {
        const TimerState& st = timers[timerNum];
        return !st.matchFired[ch] || (timerNum < TIMER32_0 && counterCycle(st, now) != st.firedCycle[ch]);
    }

    bool TIMER_RAM_FUNC(matchDue)(const uint8_t timerNum, const int ch, const int64_t pos, const uint32_t now)
    {
        const TimerState& st = timers[timerNum];
        if (st.deadlineState[ch] == DeadlineState::Pending)
        {
            return static_cast<int32_t>(now - st.deadlineUs[ch]) >= 0;
        }
        return matchArmed(timerNum, ch, now) && pos >= static_cast<int64_t>(st.matchValue[ch]);
    }

    unsigned int TIMER_RAM_FUNC(matchDelay)(const uint8_t timerNum, const int ch, const int64_t pos, const uint32_t now)
    {
        const TimerState& st = timers[timerNum];
        if (st.deadlineState[ch] == DeadlineState::Pending)
        {
            const int32_t remaining = static_cast<int32_t>(st.deadlineUs[ch] - now);
            unsigned int delay = 1u;
            if (remaining > 0)
            {
                delay = static_cast<unsigned int>(remaining);
            }
            return delay;
        }
        const int64_t mv = st.matchValue[ch];
        if (matchArmed(timerNum, ch, now))
        {
            unsigned int delay = 1u;
            if (mv > pos)
            {
                delay = static_cast<unsigned int>(mv - pos);
            }
            return delay;
        }
        // Fired in this cycle (16 bit): next match when the counter reaches the value after the wrap
        return static_cast<unsigned int>(static_cast<int64_t>(timerMask(timerNum)) + 1 - pos + mv);
    }

    void TIMER_RAM_FUNC(driveTx)(const bool pulse)
    {
        gpio_put(PIN_EIB_TX, pulse);
    }

    // Bus PWM: drop the planned or switched TX edge (the match processing switches TX itself)
    void TIMER_RAM_FUNC(cancelTxEdge)()
    {
        if (txEdge.alarmNum < 0)
        {
            return;
        }
        const uint32_t irqState = save_and_disable_interrupts();
        txEdge.armed = false;
        txEdge.switched = false;
        cancelAlarm(txEdge.alarmNum);
        restore_interrupts(irqState);
    }

    void TIMER_RAM_FUNC(endPulse)(const uint8_t timerNum)
    {
        TimerState& st = timers[timerNum];
        st.pulseEndPending = false;
        if (hasBusPins(timerNum))
        {
            cancelTxEdge();
            driveTx(false);
        }
        st.matchLevel[PwmStartChannel] = false;
    }

    // Time of the next match event of a channel: due (passed) or in the future
    uint32_t TIMER_RAM_FUNC(matchEventUs)(const uint8_t timerNum, const int ch, const int64_t pos, const uint32_t now)
    {
        const TimerState& st = timers[timerNum];
        if (st.deadlineState[ch] == DeadlineState::Pending)
        {
            return st.deadlineUs[ch];
        }
        if (matchArmed(timerNum, ch, now) && pos >= static_cast<int64_t>(st.matchValue[ch]))
        {
            return now - static_cast<uint32_t>(pos - static_cast<int64_t>(st.matchValue[ch]));
        }
        return now + matchDelay(timerNum, ch, pos, now);
    }

#if defined(SBLIB_RP2350_PIO_BUS)
    /**
     * Bus PWM by PIO: end of a pulse that starts at riseUs, from the end given by MAT2 (event + 1 with RESET).
     * As on the LPC11xx the output falls with the counter reset by MAT2, also if the pulse is then shorter
     * than its nominal width: bus.cpp syncs its start bit to a foreign edge captured just before it
     * (match(time, captureTime + 34)), the pulse ends 35 us after the foreign edge. No minimum width: the
     * state machine starts a pulse at its planned time, not late by interrupt latency.
     * An end at or before the start ends the pulse one microsecond after it, as MR2 = MR0 on the LPC11xx
     * (HIGH at MR0, LOW with the reset one tick later).
     */
    uint32_t TIMER_RAM_FUNC(pioBusPulseEnd)(const uint32_t riseUs, const uint32_t nominalEndUs)
    {
        uint32_t end = nominalEndUs;
        if (static_cast<int32_t>(nominalEndUs - riseUs) <= 0)
        {
            end = riseUs + 1;
        }
        return end;
    }
#endif

    /**
     * Next edge of the bus PWM output (PIN_EIB_TX): the end of the running pulse, or the start of
     * the next one (MAT0). Same rules as alarmCallback(): the pulse ends one microsecond after the
     * MAT2 event with RESET, and lasts at least its nominal width from the actual start (with
     * SBLIB_RP2350_PIO_BUS it ends at the MAT2 event, pioBusPulseEnd()).
     * @param edgeUs  exact time of the edge (the interrupt waits for it)
     * @param alarmUs time the alarm is planned from (minus PwmEdgeLeadUs): the nominal pulse end
     *                (MAT2 event + 1). If the pulse started late because of interrupt latency, the end
     *                alarm has about the same latency, so it arrives in time for the later edge.
     * @return false if no edge is scheduled
     */
    bool TIMER_RAM_FUNC(nextPwmEdge)(const uint8_t timerNum, const uint32_t now, uint32_t& edgeUs, uint32_t& alarmUs,
                                     bool& rising)
    {
        const TimerState& st = timers[timerNum];
        if (!hasBusPins(timerNum))
        {
            return false;
        }

        if (st.matchLevel[PwmStartChannel])
        {
            rising = false;
            if (st.pulseEndPending)
            {
                edgeUs = alarmUs = st.pulseEndUs;
                return true;
            }
            if (!st.running || !(st.matchModeValue[PwmEndChannel] & MatchActions) ||
                !matchActive(timerNum, PwmEndChannel))
            {
                return false;
            }
            const int64_t pos = counterPos(timerNum, now);
            const bool withReset = (st.matchModeValue[PwmEndChannel] & RESET) != 0;
            // with RESET the pulse ends one microsecond after the MAT2 event
            const uint32_t nominalEndUs =
                    matchEventUs(timerNum, PwmEndChannel, pos, now) + static_cast<uint32_t>(withReset);
            alarmUs = nominalEndUs;
#if defined(SBLIB_RP2350_PIO_BUS)
            edgeUs = pioBusPulseEnd(st.pulseStartUs, nominalEndUs);
#else
            const uint32_t minEndUs = st.pulseStartUs + st.pulseWidthUs;
            uint32_t edge = minEndUs;
            if (static_cast<int32_t>(minEndUs - nominalEndUs) <= 0)
            {
                edge = nominalEndUs;
            }
            edgeUs = edge;
#endif
            return true;
        }

        if (st.running && st.pwmEnabled[PwmStartChannel] && matchActive(timerNum, PwmStartChannel))
        {
            rising = true;
            edgeUs = alarmUs = matchEventUs(timerNum, PwmStartChannel, counterPos(timerNum, now), now);
            return true;
        }
        return false;
    }

#if defined(SBLIB_RP2350_TIMER_STATS)
    // Diagnostics of the TX edges (bus PWM) and of the RX edge interrupt, no influence on the timing
    constexpr uint32_t FirstPulseGapUs = 1000; // a TX pulse without a pulse in this time before starts a telegram

    struct TxDiagnostics
    {
        uint32_t riseUs = 0; // last rising TX edge
        uint32_t fallUs = 0; // last falling TX edge
        bool hasFall = false;
        bool firstPulse = false;     // the current pulse is the first one of a telegram
        uint32_t edgeScheduleUs = 0; // when the alarm of the next TX edge was programmed the last time
        uint32_t alarmTargetUs = 0;  // target time of the alarm of the next TX edge
        uint32_t rxExitUs = 0;       // end of the last RX edge interrupt
    };

    TxDiagnostics txDiag;

    inline uint32_t delayClass(const uint32_t fromUs, const uint32_t toUs)
    {
        uint32_t delay = 0;
        if (static_cast<int32_t>(toUs - fromUs) > 0)
        {
            delay = toUs - fromUs;
        }
        if (delay > 7)
        {
            delay = 7;
        }
        return delay;
    }

    // Count a switched TX edge: delay after its exact time, pulse width, cause of a delay
    void TIMER_RAM_FUNC(countTxEdge)(const bool rising, const uint32_t edgeUs, const uint32_t switchUs,
                                     const uint32_t irqEntryUs)
    {
        const uint32_t late = delayClass(edgeUs, switchUs);
        if (rising)
        {
            txDiag.firstPulse = !txDiag.hasFall || switchUs - txDiag.fallUs > FirstPulseGapUs;
            txDiag.riseUs = switchUs;
            ++stats.txRiseLateUs[late];
        }
        else
        {
            const uint32_t widthUs = switchUs - txDiag.riseUs;
            uint32_t widthClass = 0;
            if (widthUs > 33)
            {
                widthClass = widthUs - 33;
            }
            if (widthClass > 7)
            {
                widthClass = 7;
            }
            ++stats.txFallLateUs[late];
            ++stats.txWidthUs[widthClass];
            if (txDiag.firstPulse)
            {
                ++stats.txFirstFallLateUs[late];
                ++stats.txFirstWidthUs[widthClass];
            }
            txDiag.fallUs = switchUs;
            txDiag.hasFall = true;
        }

        if (late == 0)
        {
            return;
        }
        if (static_cast<int32_t>(edgeUs - txDiag.edgeScheduleUs) <= static_cast<int32_t>(PwmEdgeLeadUs))
        {
            ++stats.lateEdgesScheduledLate;
        }
        else if (static_cast<int32_t>(txDiag.rxExitUs - txDiag.alarmTargetUs) >= 0 &&
                 static_cast<int32_t>(irqEntryUs - txDiag.rxExitUs) >= 0)
        {
            ++stats.lateEdgesAfterRx;
        }
        else
        {
            ++stats.lateEdgesOther;
        }
    }

    // Count an RX edge interrupt (capture incl. timer interrupt handler) from entryUs to exitUs
    void TIMER_RAM_FUNC(countRxIrq)(const uint32_t entryUs, const uint32_t exitUs, const bool firstEcho)
    {
        const uint32_t durationUs = exitUs - entryUs;
        ++stats.rxEdgeIrqs;
        stats.irqBusyUs += durationUs;
        uint32_t durationClass = 0;
        uint32_t limitUs = 2;
        while (durationClass < 7 && durationUs >= limitUs)
        {
            ++durationClass;
            limitUs <<= 1;
        }
        ++stats.rxIrqUs[durationClass];
        if (durationUs > stats.rxIrqMaxUs)
        {
            stats.rxIrqMaxUs = durationUs;
        }
        if (firstEcho && durationUs > stats.rxIrqFirstEchoMaxUs)
        {
            stats.rxIrqFirstEchoMaxUs = durationUs;
        }
        txDiag.rxExitUs = exitUs;
    }

#   if defined(SBLIB_RP2350_PIO_BUS)
    // Count the delay from an RX edge (DMA time stamp) to its interrupt
    void TIMER_RAM_FUNC(countRxEdgeDelay)(const uint32_t delayUs)
    {
        uint32_t delayClass = 0;
        uint32_t limitUs = 2;
        while (delayClass < 7 && delayUs >= limitUs)
        {
            ++delayClass;
            limitUs <<= 1;
        }
        ++stats.rxEdgeDelayUs[delayClass];
        if (delayUs > stats.rxEdgeDelayMaxUs)
        {
            stats.rxEdgeDelayMaxUs = delayUs;
        }
    }
#   endif
#endif

    /**
     * Bus PWM: plan the next TX edge with the TX edge alarm, PwmEdgeLeadUs early (txEdgeIrq() waits for the
     * exact time). Called with disabled interrupts.
     * @return true if an edge switched by txEdgeIrq() still needs its match processing (alarmCallback() at once);
     *         then no new edge is planned before it.
     */
    bool TIMER_RAM_FUNC(planTxEdge)(const uint8_t timerNum, const uint32_t now)
    {
        if (txEdge.switched)
        {
            return true;
        }
        txEdge.armed = false;
        cancelAlarm(txEdge.alarmNum);

        uint32_t edgeUs;
        uint32_t alarmUs;
        bool rising;
        if (!nextPwmEdge(timerNum, now, edgeUs, alarmUs, rising))
        {
            return false;
        }
        int32_t remaining = static_cast<int32_t>(alarmUs - now) - static_cast<int32_t>(PwmEdgeLeadUs);
        if (remaining <= 0) // nominal time passed (late pulse start): plan from the edge itself
        {
            remaining = static_cast<int32_t>(edgeUs - now) - static_cast<int32_t>(PwmEdgeLeadUs);
        }
        uint32_t targetUs = now;
        if (remaining > 0)
        {
            targetUs = now + static_cast<uint32_t>(remaining);
        }
        txEdge.edgeUs = edgeUs;
        txEdge.rising = rising;
        txEdge.alarmUs = targetUs;
        txEdge.armed = true;
#if defined(SBLIB_RP2350_TIMER_STATS)
        txDiag.edgeScheduleUs = now;
        txDiag.alarmTargetUs = targetUs;
#endif
        armAlarm(txEdge.alarmNum, targetUs);
        return false;
    }

#if defined(SBLIB_RP2350_PIO_BUS)
    /**
     * TX program. Every path through the loop takes TxCyclesPerUs = 10 cycles, so X counts microseconds:
     *    0 command: pull noblock          ; first edge time (the TX FIFO holds two words)
     *    1        mov isr, osr
     *    2        pull noblock            ; second edge time stays in OSR
     *    3        jmp x--, loop      [1]  ; one microsecond (x = 0 continues at loop as well)
     *    4 loop:  mov y, isr              ; Y = time of the next edge (-t)
     *    5        jmp !y, idle            ; no edge planned
     *    6        jmp x!=y, wait          ; not yet
     *    7        mov pins, ~pins         ; edge: TX toggles in the 4th cycle of the microsecond
     *    8        mov isr, x
     *    9        push noblock            ; confirmation -t
     *   10        mov isr, osr            ; the edge after it (pulse end), if any
     *   11        mov osr, null
     *   12 tick:  jmp x--, loop      [1]  ; one microsecond (end of the wrap)
     *   13 idle:  nop                     ; the same cycles as wait
     *   14 wait:  mov y, status           ; Y = 0 if two words (a pair of edge times) wait in the TX FIFO
     *   15        jmp !y, command
     *   16        jmp tick           [2]
     * A pair replaces the planned edges, (0, 0) cancels them. Jump addresses are relative to the start,
     * pio_add_program() relocates them.
     */
    void buildTxProgram()
    {
        constexpr uint Command = 0;
        constexpr uint Loop = TxProgramStart;
        constexpr uint Tick = TxProgramWrap;
        constexpr uint Idle = 13;
        constexpr uint Wait = 14;
        const uint program[TxProgramLength] = {
                pio_encode_pull(false, false),
                pio_encode_mov(pio_isr, pio_osr),
                pio_encode_pull(false, false),
                pio_encode_jmp_x_dec(Loop) | pio_encode_delay(1),
                pio_encode_mov(pio_y, pio_isr),
                pio_encode_jmp_not_y(Idle),
                pio_encode_jmp_x_ne_y(Wait),
                pio_encode_mov_not(pio_pins, pio_pins),
                pio_encode_mov(pio_isr, pio_x),
                pio_encode_push(false, false),
                pio_encode_mov(pio_isr, pio_osr),
                pio_encode_mov(pio_osr, pio_null),
                pio_encode_jmp_x_dec(Loop) | pio_encode_delay(1),
                pio_encode_nop(),
                pio_encode_mov(pio_y, pio_status),
                pio_encode_jmp_not_y(Command),
                pio_encode_jmp(Tick) | pio_encode_delay(2),
        };
        for (uint i = 0; i < TxProgramLength; ++i)
        {
            txProgramInstructions[i] = static_cast<uint16_t>(program[i]);
        }
        txProgram.instructions = txProgramInstructions;
        txProgram.length = TxProgramLength;
        txProgram.origin = -1;
    }

    // Wait until TIMERAWL changed count times (at least count - 1 microseconds)
    void TIMER_RAM_FUNC(waitTicks)(const uint32_t count)
    {
        uint32_t last = nowUs();
        uint32_t seen = 0;
        while (seen < count)
        {
            const uint32_t t = nowUs();
            if (t != last)
            {
                last = t;
                ++seen;
            }
        }
    }

    // Execute an instruction on the stopped TX state machine
    void TIMER_RAM_FUNC(pioTxExec)(const uint instr)
    {
        pio_sm_exec(BusPio, static_cast<uint>(pioTx.sm), instr);
        waitTicks(2);
    }

    /**
     * Start the TX state machine with X = -TIMERAWL: X is preloaded with -startUs, the state machine starts
     * right after TIMERAWL reached startUs (and its clock divider restarts with it), so its microseconds follow
     * the ticks of TIMERAWL with a fixed delay of the start (clk_sys and the timer tick come from the same crystal).
     * TX is LOW, no edge planned. Called with disabled interrupts.
     * @return true if the start was within the microsecond startUs
     */
    bool TIMER_RAM_FUNC(pioTxStartSynchronised)()
    {
        const uint sm = static_cast<uint>(pioTx.sm);
        STAT_INC(pioTxSyncAttempts);
        pio_sm_set_enabled(BusPio, sm, false);
        pio_sm_clear_fifos(BusPio, sm);
        pio_sm_restart(BusPio, sm);
        pioTxExec(pio_encode_set(pio_pins, 0));
        pioTxExec(pio_encode_mov(pio_isr, pio_null));
        pioTxExec(pio_encode_jmp(static_cast<uint>(pioTx.programOffset) + TxProgramStart));
        const uint32_t startUs = nowUs() + 6;
        pio_sm_put(BusPio, sm, 0u - startUs);
        pioTxExec(pio_encode_pull(false, true));
        pioTxExec(pio_encode_mov(pio_x, pio_osr));
        pioTxExec(pio_encode_mov(pio_osr, pio_null));
        while (static_cast<int32_t>(startUs - nowUs()) > 0)
        {
            // wait for the tick of startUs
        }
        pio_enable_sm_mask_in_sync(BusPio, 1u << sm);
        const bool inTime = (nowUs() == startUs);
        pioTx.plannedCount = 0;
        pioTx.high = false;
        return inTime;
    }

    // Start the TX state machine synchronised to TIMERAWL, several attempts. Called with disabled interrupts.
    void TIMER_RAM_FUNC(pioTxSynchronise)()
    {
        for (int attempt = 0; attempt < PioSyncAttempts; ++attempt)
        {
            if (pioTxStartSynchronised())
            {
                return;
            }
        }
        fatalError(); // TIMERAWL and the state machine cannot be synchronised
    }

    // Give the TX state machine a pair of edge times (encoded -t, 0 = none)
    void TIMER_RAM_FUNC(pioTxSend)(const uint32_t first, const uint32_t second)
    {
        const uint sm = static_cast<uint>(pioTx.sm);
        while (pio_sm_get_tx_fifo_level(BusPio, sm) > 2)
        {
            // the state machine takes one pair per microsecond
        }
        pio_sm_put(BusPio, sm, first);
        pio_sm_put(BusPio, sm, second);
    }

    // Delay of the alarm that takes over a planned edge: one microsecond after it
    uint32_t TIMER_RAM_FUNC(takeOverDelay)(const uint32_t edgeUs, const uint32_t now)
    {
        const int32_t delay = static_cast<int32_t>(edgeUs - now) + 1;
        uint32_t result = 1;
        if (delay > 1)
        {
            result = static_cast<uint32_t>(delay);
        }
        return result;
    }

    // A pulse end is planned in the TX state machine
    bool TIMER_RAM_FUNC(pioTxEndPlanned)()
    {
        for (uint8_t i = 0; i < pioTx.plannedCount; ++i)
        {
            if (!pioTx.planned[i].rising)
            {
                return true;
            }
        }
        return false;
    }

    /**
     * End of a pulse that starts at riseUs, as alarmCallback() and nextPwmEdge() will take it: one microsecond
     * after the MAT2 event with RESET (pioBusPulseEnd()).
     * @return false if MAT2 does not end the pulse (not set yet) or the pulse would last longer than
     *         PioMaxPlannedPulseUs; the end is then planned after the start
     */
    bool TIMER_RAM_FUNC(pioPulseEnd)(const uint8_t timerNum, const uint32_t now, const uint32_t riseUs, uint32_t& endUs)
    {
        const TimerState& st = timers[timerNum];
        if (!st.running || !(st.matchModeValue[PwmEndChannel] & MatchActions) || !matchActive(timerNum, PwmEndChannel))
        {
            return false;
        }
        unsigned int endOffset = 1u;
        if (!(st.matchModeValue[PwmEndChannel] & RESET))
        {
            endOffset = 0u;
        }
        const uint32_t nominalUs = matchEventUs(timerNum, PwmEndChannel, counterPos(timerNum, now), now) + endOffset;
        const uint32_t end = pioBusPulseEnd(riseUs, nominalUs);
        if (end - riseUs > PioMaxPlannedPulseUs)
        {
            return false;
        }
        endUs = end;
        return true;
    }

    /**
     * TX by PIO: give the state machine the next edges of the bus PWM, a pulse start together with its end.
     * Called with disabled interrupts (scheduleNextAlarm()).
     * - The first planned edge is fixed less than PioTargetLeadUs before it, and the one after it with it.
     * - Planned edges are replaced by a new pair; a planned pulse start can be cancelled, a running pulse ends.
     * - TX HIGH without a pulse of the emulation (not expected) ends as soon as possible.
     * @return microseconds until the match processing takes over the first planned edge, 0 if none is planned
     */
    uint32_t TIMER_RAM_FUNC(pioPlanTxEdge)(const uint8_t timerNum, const uint32_t now)
    {
        const TimerState& st = timers[timerNum];
        if (pioTx.plannedCount > 0 && static_cast<int32_t>(pioTx.planned[0].edgeUs - now) < PioTargetLeadUs)
        {
            return takeOverDelay(pioTx.planned[0].edgeUs, now);
        }

        bool wanted = false;
        uint32_t wantedUs = 0;
        bool rising = false;
        uint32_t alarmUs = 0;
        bool stray = false;
        if (pioTx.high && !st.matchLevel[PwmStartChannel])
        {
            stray = true;
            wanted = true;
            wantedUs = now;
        }
        else if (nextPwmEdge(timerNum, now, wantedUs, alarmUs, rising) && rising != pioTx.high)
        {
            wanted = true; // (an edge to the level TX has already, after a resync, is not needed)
        }

        if (!wanted)
        {
            if (pioTx.plannedCount > 0 && pioTx.planned[0].rising)
            {
                pioTxSend(0, 0);
                pioTx.plannedCount = 0;
                STAT_INC(pioTxCancels);
            }
            if (pioTx.plannedCount == 0)
            {
                return 0;
            }
            return takeOverDelay(pioTx.planned[0].edgeUs, now);
        }

        uint32_t firstUs = wantedUs;
        bool late = false;
        const uint32_t sendUs = nowUs();
        // The planned edge became fixed meanwhile (the time advanced while computing): keep it
        if (pioTx.plannedCount > 0 && static_cast<int32_t>(pioTx.planned[0].edgeUs - sendUs) < PioTargetLeadUs)
        {
            return takeOverDelay(pioTx.planned[0].edgeUs, now);
        }
        if (static_cast<int32_t>(firstUs - sendUs) < PioTargetLeadUs)
        {
            firstUs = sendUs + PioTargetLeadUs;
            late = true;
        }
        if (firstUs == 0)
        {
            firstUs = 1; // the encoded time 0 means "no edge"
        }
        uint32_t secondUs = 0;
        const bool withEnd = rising && pioPulseEnd(timerNum, now, firstUs, secondUs);
        if (withEnd && secondUs == 0)
        {
            secondUs = 1;
        }

        const PioTxEdge& planned = pioTx.planned[0];
        const bool sameFirst = pioTx.plannedCount > 0 && planned.rising == rising && planned.wantedUs == wantedUs;
        bool sameSecond = (pioTx.plannedCount == 1 && !withEnd);
        if (pioTx.plannedCount == 2 && withEnd)
        {
            sameSecond = (pioTx.planned[1].edgeUs == secondUs);
        }
        if (sameFirst && sameSecond)
        {
            return takeOverDelay(planned.edgeUs, now);
        }

        uint32_t encodedSecond = 0;
        if (withEnd)
        {
            encodedSecond = 0u - secondUs;
        }
        pioTxSend(0u - firstUs, encodedSecond);
        if (late)
        {
            STAT_INC(pioTxLateTargets);
        }
        if (stray)
        {
            STAT_INC(pioTxStrayHigh);
        }
        STAT_INC(pioTxTargets);
        pioTx.planned[0].edgeUs = firstUs;
        pioTx.planned[0].wantedUs = wantedUs;
        pioTx.planned[0].rising = rising;
        pioTx.plannedCount = 1;
        if (withEnd)
        {
            pioTx.planned[1].edgeUs = secondUs;
            pioTx.planned[1].wantedUs = secondUs;
            pioTx.planned[1].rising = false;
            pioTx.plannedCount = 2;
        }
        return takeOverDelay(firstUs, now);
    }

    /**
     * TX by PIO: take over the first planned edge once its time has come (the state machine switched it then)
     * and check the confirmation of the state machine. A missing or wrong confirmation (synchronisation lost,
     * not expected): TX LOW and the state machine is synchronised again. Called in the alarm interrupt.
     * @return true if an edge was taken over (rising, edgeUs)
     */
    bool TIMER_RAM_FUNC(pioTxTakeOver)(bool& rising, uint32_t& edgeUs)
    {
        const uint32_t entryUs = nowUs();
        if (pioTx.plannedCount == 0 || static_cast<int32_t>(entryUs - pioTx.planned[0].edgeUs) < 0)
        {
            return false;
        }
        const uint sm = static_cast<uint>(pioTx.sm);
        rising = pioTx.planned[0].rising;
        edgeUs = pioTx.planned[0].edgeUs;
        pioTx.planned[0] = pioTx.planned[1];
        --pioTx.plannedCount;

        while (pio_sm_is_rx_fifo_empty(BusPio, sm) && static_cast<int32_t>(nowUs() - edgeUs) < PioConfirmWaitUs)
        {
            // wait for the confirmation of the edge
        }
        bool confirmed = false;
        if (!pio_sm_is_rx_fifo_empty(BusPio, sm))
        {
            confirmed = (pio_sm_get(BusPio, sm) == 0u - edgeUs);
        }
        if (confirmed)
        {
            STAT_INC(pioTxConfirmed);
            pioTx.high = rising;
        }
        else
        {
            STAT_INC(pioTxResyncs);
            const uint32_t irqState = save_and_disable_interrupts();
            pioTxSynchronise(); // TX LOW, nothing planned
            restore_interrupts(irqState);
        }
#   if defined(SBLIB_RP2350_TIMER_STATS)
        countTxEdge(rising, edgeUs, edgeUs, entryUs);
#   endif
        return true;
    }

    // Start the TX state machine (Timer::begin() of the bus timer)
    void pioTxBegin()
    {
        const uint32_t sysHz = clock_get_hz(clk_sys);
        if (sysHz % TxSmHz != 0 || sysHz / TxSmHz > 0xffffu)
        {
            fatalError(); // the state machine needs an integer divider to 10 MHz
        }
        pioTx = PioTx{};
        buildTxProgram();
        pioTx.sm = pio_claim_unused_sm(BusPio, true);
        pioTx.programOffset = pio_add_program(BusPio, &txProgram);
        const uint sm = static_cast<uint>(pioTx.sm);
        const uint offset = static_cast<uint>(pioTx.programOffset);

        pio_sm_config smConfig = pio_get_default_sm_config();
        sm_config_set_wrap(&smConfig, offset + TxProgramStart, offset + TxProgramWrap);
        sm_config_set_out_pins(&smConfig, PIN_EIB_TX, 1);
        sm_config_set_set_pins(&smConfig, PIN_EIB_TX, 1);
        sm_config_set_in_pins(&smConfig, PIN_EIB_TX);
        sm_config_set_mov_status(&smConfig, STATUS_TX_LESSTHAN, 2);
        sm_config_set_clkdiv_int_frac(&smConfig, static_cast<uint16_t>(sysHz / TxSmHz), 0);
        pio_sm_init(BusPio, sm, offset + TxProgramStart, &smConfig);
        pio_sm_set_consecutive_pindirs(BusPio, sm, PIN_EIB_TX, 1, true);
        pio_gpio_init(BusPio, PIN_EIB_TX);

        const uint32_t irqState = save_and_disable_interrupts();
        pioTxSynchronise();
        restore_interrupts(irqState);
    }

    // Stop the TX state machine, TX LOW by SIO (Timer::end() of the bus timer)
    void pioTxEnd()
    {
        if (pioTx.sm < 0)
        {
            return;
        }
        const uint sm = static_cast<uint>(pioTx.sm);
        pio_sm_set_enabled(BusPio, sm, false);
        pioTxExec(pio_encode_set(pio_pins, 0));
        gpio_set_function(PIN_EIB_TX, GPIO_FUNC_SIO);
        pio_remove_program(BusPio, &txProgram, static_cast<uint>(pioTx.programOffset));
        pio_sm_unclaim(BusPio, sm);
        pioTx = PioTx{};
    }
#endif

    // Program the hardware alarm of the timer to the next due match (and the TX edge alarm of the bus timer).
    void TIMER_RAM_FUNC(scheduleNextAlarm)(const uint8_t timerNum)
    {
        TimerState& st = timers[timerNum];
        if (st.alarmNum < 0)
        {
            return;
        }

        const uint32_t irqState = save_and_disable_interrupts();
        cancelAlarm(st.alarmNum);

        const uint32_t now = nowUs();
        unsigned int bestDelta = 0;
        bool found = false;

        if (hasBusPins(timerNum) && txEdge.alarmNum >= 0 && planTxEdge(timerNum, now))
        {
            found = true;
            bestDelta = 1u;
        }
#if defined(SBLIB_RP2350_PIO_BUS)
        if (hasBusPins(timerNum) && pioTx.sm >= 0)
        {
            // The match processing takes over the edge of the state machine one microsecond after it
            const uint32_t takeOverUs = pioPlanTxEdge(timerNum, now);
            if (takeOverUs > 0)
            {
                found = true;
                bestDelta = takeOverUs;
            }
        }
#endif

        if (st.running)
        {
            const int64_t pos = counterPos(timerNum, now);

            for (int ch = 0; ch < MatchChannels; ++ch)
            {
                if (!(st.matchModeValue[ch] & MatchActions) || !matchActive(timerNum, ch))
                {
                    continue;
                }

                const unsigned int delta = matchDelay(timerNum, ch, pos, now);
                if (!found || delta < bestDelta)
                {
                    found = true;
                    bestDelta = delta;
                }
            }
        }

        if (found)
        {
            if (bestDelta == 0)
            {
                bestDelta = 1;
            }
            if (bestDelta <= 2)
            {
                STAT_INC(shortSchedules);
            }
            // If the time already passed, run the interrupt handler as soon as possible
            if (armAlarm(st.alarmNum, now + bestDelta))
            {
                STAT_INC(forcedIrqs);
            }
        }

        restore_interrupts(irqState);
    }

#if !defined(SBLIB_RP2350_PIO_BUS)
    // TX edge interrupt of the bus timer (above its alarm and capture interrupts): switch the planned TX edge
    // at its exact time, then let the alarm interrupt of the timer do the match processing.
    void TIMER_RAM_FUNC(txEdgeIrq)()
    {
        acknowledgeAlarm(txEdge.alarmNum);
        const uint32_t entryUs = nowUs();
        // Not planned anymore, or planned again for a later time (old interrupt): nothing to do now
        if (!txEdge.armed || static_cast<int32_t>(txEdge.alarmUs - entryUs) > 0)
        {
            return;
        }

        const uint32_t edgeUs = txEdge.edgeUs;
        const bool rising = txEdge.rising;
        if (static_cast<int32_t>(edgeUs - entryUs) < 0)
        {
            STAT_INC(lateTxEdges);
        }
        while (static_cast<int32_t>(edgeUs - nowUs()) > 0)
        {
            // wait for the exact edge time
        }
        const uint32_t switchUs = nowUs();
        driveTx(rising);

        txEdge.armed = false;
        txEdge.switchedRising = rising;
        txEdge.switchedUs = switchUs;
        txEdge.switched = true;
        const int matchAlarm = timers[TIMER16_1].alarmNum;
        if (matchAlarm >= 0)
        {
            forceAlarm(matchAlarm);
        }
#   if defined(SBLIB_RP2350_TIMER_STATS)
        ++stats.edgeIrqLatencyUs[delayClass(txEdge.alarmUs, entryUs)];
        countTxEdge(rising, edgeUs, switchUs, entryUs);
        ++stats.txEdgeIrqs; // without the interrupts for an old or cancelled edge
        stats.irqBusyUs += nowUs() - entryUs;
#   endif
    }
#endif

    // Hardware alarm interrupt: process the due matches of the timer.
    void TIMER_RAM_FUNC(alarmCallback)(const uint alarmNum)
    {
        const int8_t timerNumOfAlarm = timerOfAlarm[alarmNum];
        if (timerNumOfAlarm < 0)
        {
            return;
        }

        const uint8_t timerNum = static_cast<uint8_t>(timerNumOfAlarm);
        TimerState& st = timers[timerNum];
        STAT_INC(alarmIrqs);
#if defined(SBLIB_RP2350_TIMER_STATS)
        const uint32_t flagsBefore = st.flags;
        bool anyMatch = false;
#endif

        // Bus PWM: take over the TX edge switched by txEdgeIrq(). The match processing below then finds the
        // match due and only does the bookkeeping.
        bool txRiseDone = false;
        uint32_t txRiseUs = 0;
#if defined(SBLIB_RP2350_PIO_BUS)
        // A pulse that ends in this interrupt: its end time (a MAT0 event before it falls into the pulse)
        const bool pulseBefore = st.matchLevel[PwmStartChannel];
        uint32_t pulseEndedUs = 0;
#endif
        if (hasBusPins(timerNum) && txEdge.switched)
        {
            txEdge.switched = false;
            if (txEdge.switchedRising)
            {
                txRiseDone = true;
                txRiseUs = txEdge.switchedUs;
            }
            else
            {
#if defined(SBLIB_RP2350_PIO_BUS)
                pulseEndedUs = txEdge.switchedUs;
#endif
                endPulse(timerNum);
            }
        }
#if defined(SBLIB_RP2350_PIO_BUS)
        // Bus PWM: take over the TX edge switched by the state machine
        bool pioRising = false;
        uint32_t pioEdgeUs = 0;
        if (hasBusPins(timerNum) && pioTxTakeOver(pioRising, pioEdgeUs))
        {
            if (pioRising)
            {
                txRiseDone = true;
                txRiseUs = pioEdgeUs;
            }
            else
            {
                pulseEndedUs = pioEdgeUs;
                endPulse(timerNum);
            }
        }
#endif

        const uint32_t now = nowUs();

        // Bus PWM: the pulse ends with the counter reset one microsecond after the MAT2 event
        // (not expected: the TX edge interrupt ends it)
        if (st.pulseEndPending && static_cast<int32_t>(now - st.pulseEndUs) >= 0)
        {
            STAT_INC(txEdgeFallbacks);
#if defined(SBLIB_RP2350_TIMER_STATS)
            const uint32_t pendingEndUs = st.pulseEndUs;
            const uint32_t fallUs = nowUs();
#endif
#if defined(SBLIB_RP2350_PIO_BUS)
            pulseEndedUs = st.pulseEndUs;
#endif
            endPulse(timerNum);
#if defined(SBLIB_RP2350_TIMER_STATS)
            countTxEdge(false, pendingEndUs, fallUs, now);
#endif
        }

        if (st.running)
        {
            const int64_t pos = counterPos(timerNum, now);

            bool hadReset = false;
            uint32_t resetEventUs = 0;
            unsigned int resetHoldValue = 0;
            bool firedPulseEnd = false;
            uint32_t pulseEndUs = 0;

#if defined(SBLIB_RP2350_PIO_BUS)
            bool pwmDue = st.pwmEnabled[PwmStartChannel] && matchActive(timerNum, PwmStartChannel) &&
                          matchDue(timerNum, PwmStartChannel, pos, now);
            // MAT0 re-armed during a running pulse (bus.cpp syncs to a foreign edge captured just before our
            // pulse start): the output is HIGH already, so as on the LPC11xx the match changes nothing. It is
            // consumed and must not start a second pulse at the pulse end.
            if (pwmDue && !txRiseDone &&
                (st.matchLevel[PwmStartChannel] ||
                 (pulseBefore &&
                  static_cast<int32_t>(matchEventUs(timerNum, PwmStartChannel, pos, now) - pulseEndedUs) < 0)))
            {
                st.pwmEnabled[PwmStartChannel] = false;
                if (st.deadlineState[PwmStartChannel] == DeadlineState::Pending)
                {
                    st.deadlineState[PwmStartChannel] = DeadlineState::Fired;
                }
                pwmDue = false;
            }
            // The state machine switched the pulse start, then bus.cpp re-armed MAT0 within this pulse before this
            // interrupt took the edge over (sync to a foreign edge captured just before our start): as on the
            // LPC11xx the output is HIGH already, the switched edge is the start of this pulse and the match is
            // consumed with it. A MAT0 after the planned pulse end is the next pulse.
            if (!pwmDue && txRiseDone && st.pwmEnabled[PwmStartChannel] && matchActive(timerNum, PwmStartChannel) &&
                static_cast<int32_t>(st.pwmArmedUs - txRiseUs) >= 0 && pioTxEndPlanned() &&
                static_cast<int32_t>(matchEventUs(timerNum, PwmStartChannel, pos, now) - pioTx.planned[0].edgeUs) < 0)
            {
                pwmDue = true;
            }
            if (pwmDue && !txRiseDone && hasBusPins(timerNum))
            {
                // The state machine switches the pulse start at its planned time (PioTargetLeadUs later if MAT0 came
                // late), the match processing follows when it takes over the edge
                pwmDue = false;
            }
#else
            const bool pwmDue = st.pwmEnabled[PwmStartChannel] && matchActive(timerNum, PwmStartChannel) &&
                                matchDue(timerNum, PwmStartChannel, pos, now);
#endif
            const bool pulseEndDue = (st.matchModeValue[PwmEndChannel] & MatchActions) &&
                                     matchActive(timerNum, PwmEndChannel) &&
                                     matchDue(timerNum, PwmEndChannel, pos, now);
            const bool pulseEndWithReset = (st.matchModeValue[PwmEndChannel] & RESET) != 0;

            // Nominal pulse width: MAT2 + 1 - MAT0 with RESET (the pulse ends with the cycle reset)
            unsigned int endOffset = 1u;
            if (!pulseEndWithReset)
            {
                endOffset = 0u;
            }
            unsigned int pulseWidth = 1;
            if (st.matchValue[PwmEndChannel] + endOffset > st.matchValue[PwmStartChannel])
            {
                pulseWidth = st.matchValue[PwmEndChannel] + endOffset - st.matchValue[PwmStartChannel];
            }

            // If pulse start and pulse end are both due in the same interrupt (late interrupt),
            // don't collapse the pulse: the pulse starts now and the MAT2 event moves by the pulse
            // width. The match value of MAT2 stays unchanged.
#if defined(SBLIB_RP2350_PIO_BUS)
            // The state machine started the pulse at its time (txRiseDone): the pulse end is due at its own time
            const bool movePulseEnd = pwmDue && pulseEndDue && !txRiseDone;
            if (movePulseEnd)
#else
            if (pwmDue && pulseEndDue)
#endif
            {
                st.deadlineUs[PwmEndChannel] = now + pulseWidth - endOffset;
                st.deadlineState[PwmEndChannel] = DeadlineState::Pending;
            }

            for (int ch = 0; ch < MatchChannels; ++ch)
            {
                if (!(st.matchModeValue[ch] & MatchActions) || !matchActive(timerNum, ch))
                {
                    continue;
                }
#if defined(SBLIB_RP2350_PIO_BUS)
                if (ch == PwmEndChannel && movePulseEnd)
#else
                if (ch == PwmEndChannel && pwmDue && pulseEndDue)
#endif
                {
                    continue;
                }
                if (!matchDue(timerNum, ch, pos, now))
                {
                    continue;
                }

                st.flags |= Timer::flagMask(static_cast<TimerMatch>(ch));
#if defined(SBLIB_RP2350_TIMER_STATS)
                anyMatch = true;
#endif
                // Time of the match event: the deadline, or when the counter reached the match value
                uint32_t eventUs;
                if (st.deadlineState[ch] == DeadlineState::Pending)
                {
                    st.deadlineState[ch] = DeadlineState::Fired;
                    eventUs = st.deadlineUs[ch];
                }
                else
                {
                    eventUs = now - static_cast<uint32_t>(pos - static_cast<int64_t>(st.matchValue[ch]));
                    // No new event until the counter reaches the match value again
                    st.matchFired[ch] = true;
                    st.firedCycle[ch] = counterCycle(st, now);
                }

                if (ch == PwmEndChannel)
                {
                    firedPulseEnd = true;
                    pulseEndUs = eventUs + endOffset;
                }

                if (st.matchModeValue[ch] & RESET)
                {
                    if (!hadReset || static_cast<int32_t>(eventUs - resetEventUs) < 0)
                    {
                        resetEventUs = eventUs;
                        resetHoldValue = valueAt(timerNum, eventUs);
                    }
                    hadReset = true;
                }

                if (st.matchModeValue[ch] & STOP)
                {
                    st.stoppedValue = valueAt(timerNum, eventUs);
                    st.stoppedCycle = counterCycle(st, now);
                    st.running = false;
                }
            }

            if (pwmDue)
            {
#if defined(SBLIB_RP2350_TIMER_STATS)
                const uint32_t riseEdgeUs = matchEventUs(timerNum, PwmStartChannel, pos, now);
#endif
                // MAT0 is a one-shot pulse start, re-armed by the next match(MAT0, value).
                // The match value is kept as on the LPC11xx: bus.cpp compares the captured echo of
                // our own pulse with timer.match(pwmChannel) to detect collisions. The bring-up set
                // it to 0xffff here, so every own echo looked like a collision (finding K2).
                st.pwmEnabled[PwmStartChannel] = false;
                if (st.deadlineState[PwmStartChannel] == DeadlineState::Pending)
                {
                    st.deadlineState[PwmStartChannel] = DeadlineState::Fired;
                }

                // Actual start of the pulse: when TX went HIGH (TX edge interrupt, or now)
                if (!txRiseDone)
                {
                    txRiseUs = nowUs();
                    if (hasBusPins(timerNum))
                    {
                        // Not expected: the TX edge interrupt did not switch the pulse start
                        STAT_INC(txEdgeFallbacks);
                        cancelTxEdge();
                        driveTx(true);
#if defined(SBLIB_RP2350_TIMER_STATS)
                        countTxEdge(true, riseEdgeUs, txRiseUs, now);
#endif
                    }
                }
                st.matchLevel[PwmStartChannel] = true;
                st.pulseEndPending = false;
                st.pulseStartUs = txRiseUs;
                st.pulseWidthUs = pulseWidth;
            }
            else if (txRiseDone)
            {
                // Not expected: the edge was switched, but the pulse start isn't due. Never leave TX
                // HIGH without a pulse (it would block the bus).
                driveTx(false);
            }

            if (firedPulseEnd && st.matchLevel[PwmStartChannel])
            {
#if defined(SBLIB_RP2350_PIO_BUS)
                if (hasBusPins(timerNum))
                {
                    // The state machine started the pulse in time: it ends at the MAT2 event as on the LPC11xx
                    pulseEndUs = pioBusPulseEnd(st.pulseStartUs, pulseEndUs);
                }
                else if (static_cast<int32_t>(st.pulseStartUs + st.pulseWidthUs - pulseEndUs) > 0)
                {
                    // A late start interrupt must not shorten the pulse: at least the nominal width
                    pulseEndUs = st.pulseStartUs + st.pulseWidthUs;
                }
#else
                // A late start interrupt must not shorten the pulse: at least the nominal width
                const uint32_t minEndUs = st.pulseStartUs + st.pulseWidthUs;
                if (static_cast<int32_t>(minEndUs - pulseEndUs) > 0)
                {
                    pulseEndUs = minEndUs;
                }
#endif
                const int32_t remaining = static_cast<int32_t>(pulseEndUs - now);
#if defined(SBLIB_RP2350_PIO_BUS)
                // The state machine switches a planned pulse end, no busy wait for it
                const bool pioPlannedEnd = hasBusPins(timerNum) && pioTxEndPlanned();
                if (remaining <= static_cast<int32_t>(PulseEndBusyWaitUs) && !pioPlannedEnd)
#else
                if (remaining <= static_cast<int32_t>(PulseEndBusyWaitUs))
#endif
                {
                    // Usually 1 us (the reset tick): wait here, another interrupt would add its latency.
                    // Not expected for the bus pins, the TX edge interrupt ends the pulse.
                    while (static_cast<int32_t>(pulseEndUs - nowUs()) > 0)
                    {
                        // wait for the pulse end
                    }
#if defined(SBLIB_RP2350_TIMER_STATS)
                    const uint32_t fallUs = nowUs();
#endif
                    endPulse(timerNum);
#if defined(SBLIB_RP2350_TIMER_STATS)
                    if (hasBusPins(timerNum))
                    {
                        ++stats.txEdgeFallbacks;
                        countTxEdge(false, pulseEndUs, fallUs, now);
                    }
#endif
                }
                else
                {
                    st.pulseEndPending = true;
                    st.pulseEndUs = pulseEndUs;
                }
            }

            // RESET as on the LPC11xx: the timer holds the match value until the next microsecond,
            // then it is 0. A RESET cycle has match value + 1 microseconds.
            if (hadReset)
            {
                if (st.running)
                {
#if defined(SBLIB_RP2350_PIO_BUS)
                    noteCycleChange(st, resetEventUs + 1);
#endif
                    st.startUs = resetEventUs + 1;
                    st.holdValue = resetHoldValue;
                    st.holdActive = true;
                }
                else
                {
                    st.stoppedValue = 0; // RESET and STOP: stopped at 0
                }
                rearmMatches(st);
            }
#if defined(SBLIB_RP2350_TIMER_STATS)
            anyMatch = anyMatch || pwmDue;
#endif
        }
#if defined(SBLIB_RP2350_TIMER_STATS)
        if (!anyMatch && flagsBefore == st.flags)
        {
            STAT_INC(alarmIrqsWithoutMatch);
        }
#endif

        if (st.irqEnabled && st.flags)
        {
#if defined(SBLIB_RP2350_TIMER_STATS)
            const uint32_t handlerStartUs = nowUs();
#endif
            callIrqHandler(timerNum);
#if defined(SBLIB_RP2350_TIMER_STATS)
            const uint32_t handlerUs = nowUs() - handlerStartUs;
            if (hasBusPins(timerNum) && handlerUs > stats.alarmHandlerMaxUs)
            {
                stats.alarmHandlerMaxUs = handlerUs;
            }
#endif
        }

        scheduleNextAlarm(timerNum);
    }

    // Alarm interrupt of a timer, directly from the vector table (no SDK alarm handler in flash)
    void TIMER_RAM_FUNC(alarmIrq)()
    {
#if defined(SBLIB_RP2350_TIMER_STATS)
        const uint32_t entryUs = nowUs();
#endif
        const uint alarmNum = TIMER_ALARM_NUM_FROM_IRQ(__get_current_exception() - VTABLE_FIRST_IRQ);
        acknowledgeAlarm(static_cast<int>(alarmNum));
        alarmCallback(alarmNum);
#if defined(SBLIB_RP2350_TIMER_STATS)
        stats.irqBusyUs += nowUs() - entryUs;
#endif
    }

    // Claim a hardware alarm and route its interrupt to handler
    int claimAlarm(const irq_handler_t handler)
    {
        const int alarmNum = hardware_alarm_claim_unused(true);
        const uint irqNum = hardware_alarm_get_irq_num(static_cast<uint>(alarmNum));
        cancelAlarm(alarmNum);
        irq_set_exclusive_handler(irqNum, handler);
        hw_set_bits(&timer_hw->inte, 1u << alarmNum);
        irq_set_enabled(irqNum, true);
        return alarmNum;
    }

    void releaseAlarm(const int alarmNum, const irq_handler_t handler)
    {
        const uint irqNum = hardware_alarm_get_irq_num(static_cast<uint>(alarmNum));
        cancelAlarm(alarmNum);
        hw_clear_bits(&timer_hw->inte, 1u << alarmNum);
        irq_set_enabled(irqNum, false);
        irq_remove_handler(irqNum, handler);
        hardware_alarm_unclaim(static_cast<uint>(alarmNum));
    }

#if defined(SBLIB_RP2350_TIMER_STATS)
    constexpr uint32_t ShortGapUs = 8; // capture edges this close to the previous one are counted
    static_assert(sizeof(stats.shortGapUs) / sizeof(stats.shortGapUs[0]) == ShortGapUs,
                  "one counter per distance below ShortGapUs");

    // Diagnostics only, no influence on the capture: count an edge closer than ShortGapUs to the previous one
    void TIMER_RAM_FUNC(countShortGap)(TimerState& st, const uint32_t edgeUs)
    {
        const uint32_t gapUs = edgeUs - st.lastCaptureUs;
        if (st.hasCapture && gapUs < ShortGapUs)
        {
            if (stats.shortGapCaptures == 0 || gapUs < stats.minShortGapUs)
            {
                stats.minShortGapUs = gapUs;
            }
            ++stats.shortGapCaptures;
            if (st.matchLevel[PwmStartChannel])
            {
                ++stats.shortGapCapturesInTx;
            }
            ++stats.shortGapUs[gapUs];
        }
        st.lastCaptureUs = edgeUs;
        st.hasCapture = true;
    }
#endif

#if !defined(SBLIB_RP2350_PIO_BUS)
    // Capture event of a timer (falling edge on the capture pin)
    void TIMER_RAM_FUNC(captureEvent)(const uint8_t timerNum, const int ch)
    {
        TimerState& st = timers[timerNum];
        if (!(st.captureModeValue[ch] & FALLING_EDGE))
        {
            return;
        }

        const uint32_t edgeUs = nowUs();
        st.captureValue[ch] = currentValue(timerNum, edgeUs);
#   if defined(SBLIB_RP2350_TIMER_STATS)
        countShortGap(st, edgeUs);
#   endif
        STAT_INC(captureEvents);
        if (st.captureModeValue[ch] & INTERRUPT)
        {
            st.flags |= Timer::flagMask(static_cast<TimerCapture>(ch));
            if (st.irqEnabled)
            {
                callIrqHandler(timerNum);
            }
        }
    }

    // Raw GPIO interrupt handler for PIN_EIB_RX (capture CAP0 of timer16_1)
    void TIMER_RAM_FUNC(rxEdgeHandler)()
    {
        if (!(gpio_get_irq_event_mask(PIN_EIB_RX) & GPIO_IRQ_EDGE_FALL))
        {
            return;
        }

#   if defined(SBLIB_RP2350_TIMER_STATS)
        const uint32_t entryUs = nowUs();
        const bool firstEcho = timers[TIMER16_1].matchLevel[PwmStartChannel] && txDiag.firstPulse;
#   endif
        gpio_acknowledge_irq(PIN_EIB_RX, GPIO_IRQ_EDGE_FALL);
        captureEvent(TIMER16_1, CAP0);
#   if defined(SBLIB_RP2350_TIMER_STATS)
        countRxIrq(entryUs, nowUs(), firstEcho);
#   endif
    }
#else
    /**
     * Timer value at the time of a past edge, as the LPC11xx capture register latched it: also if the counter
     * started a new cycle (RESET, value(), restart()) between the edge and now.
     */
    unsigned int TIMER_RAM_FUNC(captureValueAt)(const uint8_t timerNum, const uint32_t edgeUs)
    {
        const TimerState& st = timers[timerNum];
        const uint32_t beforeChangeUs = st.cycleChangeUs - edgeUs;
        // Edge up to one 16 bit cycle before the last restart of the counter: value of the cycle before
        if (st.running && beforeChangeUs - 1 < 0x10000u)
        {
            return (edgeUs - st.prevStartUs) & timerMask(timerNum);
        }
        return currentValue(timerNum, edgeUs);
    }

    // Capture event of a timer: falling edge on the capture pin at the system time edgeUs
    void TIMER_RAM_FUNC(captureEventAt)(const uint8_t timerNum, const int ch, const uint32_t edgeUs)
    {
        TimerState& st = timers[timerNum];
        if (!(st.captureModeValue[ch] & FALLING_EDGE))
        {
            return;
        }

        st.captureValue[ch] = captureValueAt(timerNum, edgeUs);
#   if defined(SBLIB_RP2350_TIMER_STATS)
        countShortGap(st, edgeUs);
#   endif
        STAT_INC(captureEvents);
        if (st.captureModeValue[ch] & INTERRUPT)
        {
            st.flags |= Timer::flagMask(static_cast<TimerCapture>(ch));
            if (st.irqEnabled)
            {
                callIrqHandler(timerNum);
            }
        }
    }

    // DMA interrupt of the RX edges (capture CAP0 of timer16_1): the time stamp of the last falling edge
    void TIMER_RAM_FUNC(pioRxIrq)()
    {
#   if defined(SBLIB_RP2350_TIMER_STATS)
        const uint32_t entryUs = nowUs();
#   endif
        dma_channel_acknowledge_irq0(static_cast<uint>(pioRx.stampChannel));
        const uint32_t edgeUs = pioRx.edgeUs;
        // Several edges before this interrupt: only the last one counts, as with the capture register
        if (pioRx.hasEdge && edgeUs == pioRx.lastEdgeUs)
        {
            STAT_INC(rxEdgeDuplicates);
            return;
        }
        pioRx.hasEdge = true;
        pioRx.lastEdgeUs = edgeUs;
#   if defined(SBLIB_RP2350_TIMER_STATS)
        countRxEdgeDelay(entryUs - edgeUs);
        const bool firstEcho = timers[TIMER16_1].matchLevel[PwmStartChannel] && txDiag.firstPulse;
#   endif
        captureEventAt(TIMER16_1, CAP0, edgeUs);
#   if defined(SBLIB_RP2350_TIMER_STATS)
        countRxIrq(entryUs, nowUs(), firstEcho);
#   endif
    }

    void buildRxProgram()
    {
        rxProgramInstructions[0] = static_cast<uint16_t>(pio_encode_wait_pin(true, 0));  // wait for HIGH
        rxProgramInstructions[1] = static_cast<uint16_t>(pio_encode_wait_pin(false, 0)); // falling edge
        rxProgramInstructions[2] = static_cast<uint16_t>(pio_encode_push(false, false)); // one word, no stall
        rxProgram.instructions = rxProgramInstructions;
        rxProgram.length = RxProgramLength;
        rxProgram.origin = -1;
    }

    // Start the RX edge detection: state machine, DMA channels and DMA interrupt
    void pioRxBegin()
    {
        pioRx = PioRx{};
        buildRxProgram();
        pioRx.sm = pio_claim_unused_sm(BusPio, true);
        pioRx.programOffset = pio_add_program(BusPio, &rxProgram);
        const uint sm = static_cast<uint>(pioRx.sm);
        const uint offset = static_cast<uint>(pioRx.programOffset);

        pio_sm_config smConfig = pio_get_default_sm_config();
        sm_config_set_wrap(&smConfig, offset, offset + RxProgramLength - 1);
        sm_config_set_in_pins(&smConfig, PIN_EIB_RX);
        sm_config_set_clkdiv_int_frac(&smConfig, 1, 0);
        pio_sm_init(BusPio, sm, offset, &smConfig);

        pioRx.drainChannel = dma_claim_unused_channel(true);
        pioRx.stampChannel = dma_claim_unused_channel(true);
        const uint drain = static_cast<uint>(pioRx.drainChannel);
        const uint stamp = static_cast<uint>(pioRx.stampChannel);

        dma_channel_config drainConfig = dma_channel_get_default_config(drain);
        channel_config_set_transfer_data_size(&drainConfig, DMA_SIZE_32);
        channel_config_set_read_increment(&drainConfig, false);
        channel_config_set_write_increment(&drainConfig, false);
        channel_config_set_dreq(&drainConfig, pio_get_dreq(BusPio, sm, false));
        channel_config_set_chain_to(&drainConfig, stamp);
        channel_config_set_high_priority(&drainConfig, true);
        dma_channel_configure(drain, &drainConfig, &pioRx.drainSink, &BusPio->rxf[sm], 1, false);

        dma_channel_config stampConfig = dma_channel_get_default_config(stamp);
        channel_config_set_transfer_data_size(&stampConfig, DMA_SIZE_32);
        channel_config_set_read_increment(&stampConfig, false);
        channel_config_set_write_increment(&stampConfig, false);
        channel_config_set_dreq(&stampConfig, DREQ_FORCE);
        channel_config_set_chain_to(&stampConfig, drain);
        channel_config_set_high_priority(&stampConfig, true);
        dma_channel_configure(stamp, &stampConfig, &pioRx.edgeUs, &timer_hw->timerawl, 1, false);

        dma_channel_acknowledge_irq0(stamp);
        dma_channel_set_irq0_enabled(stamp, true);
        irq_set_exclusive_handler(RxDmaIrq, pioRxIrq);
        irq_set_enabled(RxDmaIrq, true);

        dma_channel_start(drain); // waits for the first word of the state machine
        pio_sm_set_enabled(BusPio, sm, true);
    }

    // Stop the RX edge detection and release its resources
    void pioRxEnd()
    {
        if (pioRx.sm < 0)
        {
            return;
        }
        const uint sm = static_cast<uint>(pioRx.sm);
        const uint drain = static_cast<uint>(pioRx.drainChannel);
        const uint stamp = static_cast<uint>(pioRx.stampChannel);

        pio_sm_set_enabled(BusPio, sm, false);
        irq_set_enabled(RxDmaIrq, false);
        irq_remove_handler(RxDmaIrq, pioRxIrq);
        // No chaining anymore before the abort (an aborted channel must not trigger the other one)
        dma_channel_config drainConfig = dma_get_channel_config(drain);
        channel_config_set_chain_to(&drainConfig, drain);
        dma_channel_set_config(drain, &drainConfig, false);
        dma_channel_config stampConfig = dma_get_channel_config(stamp);
        channel_config_set_chain_to(&stampConfig, stamp);
        dma_channel_set_config(stamp, &stampConfig, false);
        dma_channel_cleanup(drain);
        dma_channel_cleanup(stamp);
        dma_channel_unclaim(drain);
        dma_channel_unclaim(stamp);

        pio_remove_program(BusPio, &rxProgram, static_cast<uint>(pioRx.programOffset));
        pio_sm_unclaim(BusPio, sm);
        pioRx = PioRx{};
    }
#endif

    void updateRxEdgeInterrupt(const uint8_t timerNum)
    {
        if (!hasBusPins(timerNum))
        {
            return;
        }

        const TimerState& st = timers[timerNum];
        const bool enable = st.initialized && (st.captureModeValue[CAP0] & FALLING_EDGE);
#if defined(SBLIB_RP2350_PIO_BUS)
        if (enable)
        {
            // discard edges detected before: a pending DMA interrupt of an old edge finds no new edge time
            pioRx.hasEdge = true;
            pioRx.lastEdgeUs = pioRx.edgeUs;
        }
#else
        if (enable)
        {
            gpio_acknowledge_irq(PIN_EIB_RX, GPIO_IRQ_EDGE_FALL); // discard edges detected before
        }
        gpio_set_irq_enabled(PIN_EIB_RX, GPIO_IRQ_EDGE_FALL, enable);
#endif
    }

    void applyPriority(const uint8_t timerNum)
    {
        const TimerState& st = timers[timerNum];
        // Hardware priority as setInterruptPriority(): highest 0x00, high 0x40, medium 0x80, low 0xc0
        uint8_t priority = static_cast<uint8_t>(static_cast<uint8_t>(st.priority) << 6);
        if (hasBusPins(timerNum))
        {
            // The TX edge interrupt interrupts the alarm and capture interrupts of the bus timer, like the
            // PWM hardware of the LPC11xx switches TX independent of the interrupts
            if (txEdge.alarmNum >= 0)
            {
                irq_set_priority(hardware_alarm_get_irq_num(static_cast<uint>(txEdge.alarmNum)), priority);
            }
#if defined(SBLIB_RP2350_PIO_BUS)
            // No TX edge interrupt: alarm and capture (DMA) interrupt with the priority of the timer. The capture
            // interrupt must not interrupt the match interrupt and vice versa.
            irq_set_priority(RxDmaIrq, priority);
#else
            priority += BelowTxEdgePriority;
            // The capture interrupt must not interrupt the match interrupt and vice versa
            irq_set_priority(IO_IRQ_BANK0, priority);
#endif
        }
        if (st.alarmNum >= 0)
        {
            irq_set_priority(hardware_alarm_get_irq_num(static_cast<uint>(st.alarmNum)), priority);
        }
    }

    // Atomic read-modify-write of the flags (also changed in interrupts)
    void modifyFlags(const uint8_t timerNum, const uint32_t clearMask)
    {
        const uint32_t irqState = save_and_disable_interrupts();
        timers[timerNum].flags &= ~clearMask;
        restore_interrupts(irqState);
    }

} // namespace

Timer::Timer(const uint8_t aTimerNum) :
    timerNum(aTimerNum)
{
}

void Timer::begin() const
{
    TimerState& st = timers[timerNum];
    if (st.initialized)
    {
        return;
    }

    // No alarm is armed yet, applyPriority() below comes before the first alarm interrupt
    st.alarmNum = claimAlarm(alarmIrq);
    timerOfAlarm[st.alarmNum] = static_cast<int8_t>(timerNum);

    st.startUs = nowUs();
    st.stoppedValue = 0;
    st.initialized = true;

    if (hasBusPins(timerNum))
    {
        // Output latch LOW (no pulse). The direction is set by pinMode(PIN_EIB_TX, OUTPUT_MATCH).
        driveTx(false);
        txEdge = TxEdge{};
#if defined(SBLIB_RP2350_PIO_BUS)
        pioTxBegin();
        pioRxBegin();
#else
        txEdge.alarmNum = claimAlarm(txEdgeIrq);
        gpio_add_raw_irq_handler(PIN_EIB_RX, rxEdgeHandler);
        irq_set_enabled(IO_IRQ_BANK0, true);
#endif
    }

    applyPriority(timerNum);
}

void Timer::end() const
{
    TimerState& st = timers[timerNum];
    if (!st.initialized)
    {
        return;
    }

    st.running = false;
    st.irqEnabled = false;
    st.initialized = false;
    st.pulseEndPending = false;

    releaseAlarm(st.alarmNum, alarmIrq);
    timerOfAlarm[st.alarmNum] = -1;
    st.alarmNum = -1;

    if (hasBusPins(timerNum))
    {
#if defined(SBLIB_RP2350_PIO_BUS)
        pioTxEnd();
        pioRxEnd();
#else
        cancelTxEdge();
        releaseAlarm(txEdge.alarmNum, txEdgeIrq);
        txEdge.alarmNum = -1;
        updateRxEdgeInterrupt(timerNum);
        gpio_remove_raw_irq_handler(PIN_EIB_RX, rxEdgeHandler);
#endif
        driveTx(false);
    }
}

void Timer::prescaler(const unsigned int factor) const
{
    timers[timerNum].prescaler = factor; // not used, the timer always counts microseconds
}

unsigned int Timer::prescaler() const
{
    return timers[timerNum].prescaler;
}

void Timer::start() const
{
    TimerState& st = timers[timerNum];
    if (!st.running)
    {
        st.startUs = nowUs() - st.stoppedValue;
        st.holdActive = false;
        // keep "fired in this counter cycle" relative to the new start
        for (int ch = 0; ch < MatchChannels; ++ch)
        {
            st.firedCycle[ch] -= st.stoppedCycle;
        }
        st.running = true;
    }
    scheduleNextAlarm(timerNum);
}

void Timer::stop() const
{
    TimerState& st = timers[timerNum];
    if (st.running)
    {
        const uint32_t now = nowUs();
        st.stoppedValue = currentValue(timerNum, now);
        st.stoppedCycle = counterCycle(st, now);
        st.running = false;
    }
    scheduleNextAlarm(timerNum);
}

void Timer::restart() const
{
    TimerState& st = timers[timerNum];
#if defined(SBLIB_RP2350_PIO_BUS)
    noteCycleChange(st, nowUs());
#endif
    st.stoppedValue = 0;
    st.startUs = nowUs();
    st.holdActive = false;
    st.running = true;
    rearmMatches(st);
    scheduleNextAlarm(timerNum);
}

void Timer::reset() const
{
    value(0);
}

unsigned int Timer::value() const
{
    return currentValue(timerNum, nowUs());
}

void Timer::value(const unsigned int val) const
{
    TimerState& st = timers[timerNum];
#if defined(SBLIB_RP2350_PIO_BUS)
    noteCycleChange(st, nowUs());
#endif
    st.stoppedValue = val & timerMask(timerNum);
    st.startUs = nowUs() - st.stoppedValue;
    st.holdActive = false;
    st.stoppedCycle = 0;
    rearmMatches(st);
    scheduleNextAlarm(timerNum);
}

void Timer::interrupts() const
{
    TimerState& st = timers[timerNum];
    st.irqEnabled = true;
    scheduleNextAlarm(timerNum);

    // Flags set while the interrupt was disabled: interrupt now, like a pending NVIC interrupt
    if (st.flags && st.alarmNum >= 0)
    {
        STAT_INC(forcedIrqs);
        forceAlarm(st.alarmNum);
    }
}

void Timer::noInterrupts() const
{
    timers[timerNum].irqEnabled = false;
}

int Timer::flags() const
{
    return static_cast<int>(timers[timerNum].flags);
}

void Timer::resetFlags() const
{
    modifyFlags(timerNum, 0xffffffffu);
}

void Timer::resetFlag(const TimerMatch match) const
{
    modifyFlags(timerNum, flagMask(match));
}

void Timer::resetFlag(const TimerCapture capture) const
{
    modifyFlags(timerNum, flagMask(capture));
}

bool Timer::flag(const TimerMatch match) const
{
    return (timers[timerNum].flags & flagMask(match)) != 0;
}

bool Timer::flag(const TimerCapture capture) const
{
    return (timers[timerNum].flags & flagMask(capture)) != 0;
}

void Timer::matchMode(const int channel, const int mode) const
{
    timers[timerNum].matchModeValue[channel] = mode;
    scheduleNextAlarm(timerNum);
}

int Timer::matchMode(const int channel) const
{
    return timers[timerNum].matchModeValue[channel];
}

void Timer::match(const int channel, const unsigned int value) const
{
    TimerState& st = timers[timerNum];
    st.matchValue[channel] = value;
    st.deadlineState[channel] = DeadlineState::Register;
    st.matchFired[channel] = false;

    // MAT0 (pulse start) is one-shot after a fire; every new match(MAT0, value) re-arms it,
    // 0xffff keeps it off.
    if (channel == PwmStartChannel)
    {
        st.pwmEnabled[channel] = (value < 0xffffu);
#if defined(SBLIB_RP2350_PIO_BUS)
        st.pwmArmedUs = nowUs();
#endif
    }

    scheduleNextAlarm(timerNum);
}

unsigned int Timer::match(const int channel) const
{
    return timers[timerNum].matchValue[channel];
}

void Timer::captureMode(const int channel, const int mode) const
{
    timers[timerNum].captureModeValue[channel] = mode;
    if (channel == CAP0)
    {
        updateRxEdgeInterrupt(timerNum);
    }
}

int Timer::captureMode(const int channel) const
{
    return timers[timerNum].captureModeValue[channel];
}

unsigned int Timer::capture(const int channel) const
{
    return timers[timerNum].captureValue[channel];
}

void Timer::pwmEnable(const int channel) const
{
    timers[timerNum].pwmEnabled[channel] = true;
    scheduleNextAlarm(timerNum);
}

void Timer::pwmDisable(const int channel) const
{
    timers[timerNum].pwmEnabled[channel] = false;
    scheduleNextAlarm(timerNum);
}

void Timer::counterMode([[maybe_unused]] const int mode, [[maybe_unused]] const int clearMode) const
{
    // Counter mode (counting edges) is not emulated
}

void Timer::matchModePinConfig([[maybe_unused]] const int channel, [[maybe_unused]] const int mode) const
{
    // Match pin actions are only emulated for the bus PWM (MAT0/MAT2 of timer16_1)
}

bool Timer::getMatchChannelLevel(const int channel) const
{
    return timers[timerNum].matchLevel[channel];
}

void Timer::setIRQPriority(const InterruptPriority newPriority) const
{
    // Also called before begin() (bus.cpp); then the priority is applied in begin()
    timers[timerNum].priority = newPriority;
    applyPriority(timerNum);
}

#if defined(SBLIB_RP2350_TIMER_STATS)
const volatile TimerEmulationStats& timerEmulationStats()
{
    return stats;
}
#endif
