/*
 *  time.cpp - Time functions (millis, delay, delayMicroseconds), RP2350 implementation.
 *
 *  Based on the 64 bit microsecond counter of the RP2350 timer (TIMER0), which the Pico SDK
 *  runtime starts before main(). No interrupt is needed (the LPC11xx uses the SysTick
 *  interrupt), so the functions also work inside interrupt handlers and with disabled
 *  interrupts. delay() busy-waits and does not use the SDK alarm pool.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include "sblib/timer.h"
#include "sblib/utils.h"

#include <hardware/timer.h>

void delay(const unsigned int msec)
{
    busy_wait_us(static_cast<uint64_t>(msec) * 1000);
}

void delayMicroseconds(const unsigned int usec)
{
    if (usec > MAX_DELAY_MICROSECONDS)
    {
        fatalError(); // same limit as with the LPC11xx, use delay() for longer delays
    }

    busy_wait_us_32(usec);
}

unsigned int millis()
{
    // Wraps around after 49.7 days, as with the LPC11xx
    return static_cast<unsigned int>(time_us_64() / 1000);
}

unsigned int elapsed(const unsigned int ref)
{
    return millis() - ref;
}
