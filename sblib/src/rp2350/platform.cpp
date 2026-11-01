/*
 *  platform.cpp - Low level hardware specific stuff, RP2350 implementation.
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include "sblib/platform.h"

#include <hardware/watchdog.h>
#include <pico/rand.h>

// System clock in Hz, as the CMSIS variable of the LPC11xx. Set by the sblib main() from
// the clock configuration of the Pico SDK runtime.
uint32_t SystemCoreClock = SYS_CLK_HZ;

void NVIC_SystemReset()
{
    // Reboot by the watchdog, which resets the whole chip incl. peripherals
    watchdog_reboot(0, 0, 0);
    while (true)
    {
    }
}

uint32_t hwRandom()
{
    return get_rand_32();
}
