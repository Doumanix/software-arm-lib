/*
 * rp2350-knx-bcu1 - minimal KNX BCU1 device on the RP2350 (sblib RP2350 port).
 *
 * Uses the main() of the sblib: setup() once, then bcu.loop() and loop() or loop_noapp().
 * No communication objects are used, the device only takes part in the KNX management:
 * device info, physical address (programming button PIN_PROG), memory read/write, download.
 *
 * Identity of out8-bcu1 (Jung 2138.10), so its ETS product can be used.
 * PIN_RUN blinks slowly while the application runs (after an ETS download), fast otherwise.
 * UART1 (TX GPIO8, 115200 8N1) prints the start message and the physical address; with the
 * CMake option SBLIB_RP2350_TIMER_STATS (default ON here) also one diagnostics line per second.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <cstdio>
#include "pico/stdlib.h"

#include <sblib/eibBCU1.h>
#include <sblib/digital_pin.h>
#include <sblib/io_pin_names.h>
#include <sblib/timeout.h>
#include <sblib/timer.h>

constexpr int Manufacturer = 0x04; // Jung
constexpr int DeviceType = 0x2060; // 2138.10, as out8-bcu1
constexpr int AppVersion = 1;

BCU1 bcu = BCU1();

static Timeout blinkTimeout;
static uint16_t lastAddress = 0;

#if defined(SBLIB_RP2350_TIMER_STATS)
/**
 * Diagnostics, once per second on UART1: main loop iterations and timer emulation counters
 * of the last second, bus sending/receive state.
 */
static void printDiagnostics()
{
    static unsigned int loops = 0;
    static unsigned int lastPrint = 0;
    static TimerEmulationStats last = {};
    static uint32_t lastPauseCalls = 0;
    ++loops;
    const unsigned int now = millis();
    if (now - lastPrint < 1000)
    {
        return;
    }

    const volatile TimerEmulationStats& s = timerEmulationStats();
    const TimerEmulationStats cur = {s.alarmIrqs,      s.alarmIrqsWithoutMatch, s.forcedIrqs,
                                     s.shortSchedules, s.captureEvents,         s.handlerCalls};
    const volatile BusPauseStats& p = busPauseStats();
    std::printf("diag t=%u loops=%u alarm=%lu nomatch=%lu forced=%lu short=%lu capture=%lu handler=%lu"
                " sending=%d rx=%d app=%d prog=%d pause=%lu plast=%lu pmax=%lu pit=%lu pst=%u/%u\n",
                now / 1000, loops, static_cast<unsigned long>(cur.alarmIrqs - last.alarmIrqs),
                static_cast<unsigned long>(cur.alarmIrqsWithoutMatch - last.alarmIrqsWithoutMatch),
                static_cast<unsigned long>(cur.forcedIrqs - last.forcedIrqs),
                static_cast<unsigned long>(cur.shortSchedules - last.shortSchedules),
                static_cast<unsigned long>(cur.captureEvents - last.captureEvents),
                static_cast<unsigned long>(cur.handlerCalls - last.handlerCalls), bcu.bus->sendingFrame(),
                bcu.bus->telegramReceived(), bcu.applicationRunning(), bcu.programmingMode(),
                static_cast<unsigned long>(p.calls - lastPauseCalls), static_cast<unsigned long>(p.lastUs),
                static_cast<unsigned long>(p.maxUs), static_cast<unsigned long>(p.maxIterations),
                static_cast<unsigned int>(p.maxEntryState), static_cast<unsigned int>(p.maxStuckState));
    lastPauseCalls = p.calls;
    last = cur;
    loops = 0;
    lastPrint = now;
}
#endif

static void blink(const unsigned int periodMs)
{
#if defined(SBLIB_RP2350_TIMER_STATS)
    printDiagnostics();
#endif

    if (blinkTimeout.expired())
    {
        digitalWrite(PIN_RUN, !digitalRead(PIN_RUN));
        blinkTimeout.start(periodMs);
    }

    if (bcu.ownAddress() != lastAddress)
    {
        lastAddress = bcu.ownAddress();
        std::printf("physical address %u.%u.%u\n", lastAddress >> 12, (lastAddress >> 8) & 0x0f, lastAddress & 0xff);
    }
}

BcuBase* setup()
{
    stdio_init_all();
    std::printf("rp2350-knx-bcu1: sblib %x, manufacturer 0x%02x, device type 0x%04x, version %d\n", SBLIB_VERSION,
                Manufacturer, DeviceType, AppVersion);
#if defined(SBLIB_RP2350_TIMER_STATS)
    BusPauseStats prev;
    if (busPauseStatsPreviousRun(prev))
    {
        std::printf("previous run: pause max=%lu us, iterations=%lu, states %u/%u, calls=%lu\n",
                    static_cast<unsigned long>(prev.maxUs), static_cast<unsigned long>(prev.maxIterations),
                    static_cast<unsigned int>(prev.maxEntryState), static_cast<unsigned int>(prev.maxStuckState),
                    static_cast<unsigned long>(prev.calls));
    }
#endif

    pinMode(PIN_RUN, OUTPUT);
    bcu.begin(Manufacturer, DeviceType, AppVersion);
    blinkTimeout.start(1);
    return &bcu;
}

void loop()
{
    blink(1000);
}

void loop_noapp()
{
    blink(200);
}
