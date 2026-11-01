/*
 *  platform_detect.h - Detect the target microcontroller platform.
 *
 *  Defines exactly one of
 *    SBLIB_PLATFORM_LPC11XX  NXP LPC11xx (LPC1115), compiler definition __LPC11XX__
 *                            (also used for the host builds with CPU emulation)
 *    SBLIB_PLATFORM_RP2350   Raspberry Pi RP2350 / RP2354 (Cortex-M33), compiler definition
 *                            PICO_RP2350=1, set by the Pico SDK for all targets linking pico_platform
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_PLATFORM_DETECT_H_
#define SBLIB_PLATFORM_DETECT_H_

#if defined(__LPC11XX__)
#   define SBLIB_PLATFORM_LPC11XX 1
#elif defined(PICO_RP2350) && PICO_RP2350 && defined(__arm__)
#   define SBLIB_PLATFORM_RP2350 1
#else
#   error "Unsupported platform"
#endif

#endif /* SBLIB_PLATFORM_DETECT_H_ */
