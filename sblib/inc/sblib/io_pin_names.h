/*
 *  Copyright (c) 2015 Martin Glueck <martin@mangari.org>
 *  Copyright (c) 2026 Doumanix <doumanix@gmx.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */

#ifndef SBLIB_IO_PIN_NAMES_H_
#define SBLIB_IO_PIN_NAMES_H_

#if defined (__LPC11XX_GNAXBOARD__)
#   include <sblib/hardware/gpio_lpc11xx_gnax.h>
#elif defined(__LPC11XX_FUSED_IO__)
#   include <sblib/hardware/gpio_lpc11xx_fused_io.h>
#elif defined (__LPC11XX__)
#   include <sblib/hardware/gpio_lpc11xx.h>
#elif defined(SBLIB_BOARD_HEADER)
    // RP2350 board header provided by the application (CMake: SBLIB_BOARD=custom, SBLIB_BOARD_HEADER=<file>)
#   include SBLIB_BOARD_HEADER
#elif defined(SBLIB_BOARD_RP2350_PICO2_TESTBOARD)
#   include <sblib/hardware/gpio_rp2350_pico2_testboard.h>
#else
#   error "No board selected. For RP2350 set the CMake variable SBLIB_BOARD."
#endif

#endif /* SBLIB_IO_PIN_NAMES_H_ */
