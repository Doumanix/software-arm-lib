/*
 * cmsis_rp2350.h - CMSIS intrinsic of the LPC1115 source of the bus monitor example for the RP2350.
 *
 * Included before src/app_main.cpp by CMakeLists.txt (-include), so the source stays as it is
 * for the LPC1115.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#ifndef EXAMPLE_BUSMON_CMSIS_RP2350_H_
#define EXAMPLE_BUSMON_CMSIS_RP2350_H_

#include <hardware/sync.h>

/**
 * @brief CMSIS __WFI() on the RP2350: a NOP instead of a sleep, like waitForInterrupt() in
 *        sblib/interrupt.h. The RP2350 port has no periodic interrupt that ends a WFI.
 */
static inline void __WFI()
{
    __nop();
}

#endif /* EXAMPLE_BUSMON_CMSIS_RP2350_H_ */
