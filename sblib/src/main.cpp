/******************************************************************************
 * @file   main.cpp
 * @brief  The Selfbus library's main implementation.
 *
 * @author Stefan Taferner <stefan.taferner@gmx.at> Copyright (c) 2014
 * @author Darthyson <darth@maptrack.de> Copyright (c) 2021
 *
 * @bug No known bugs.
 ******************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */

#include <sblib/main.h>

#include <sblib/interrupt.h>
#include <sblib/timer.h>

#include <sblib/eib/bcu_base.h>

#if defined(SBLIB_PLATFORM_RP2350)
#   include <hardware/clocks.h>
#endif

#if defined(SBLIB_PLATFORM_LPC11XX)
/**
 * @brief Initializes the library.
 *        - Configures and starts the system timer to call SysTick_Handler once every 1 millisecond.
 */
static inline void lib_setup()
{
    SysTick_Config(SystemCoreClock / 1000);
}
#elif defined(SBLIB_PLATFORM_RP2350)
/**
 * @brief Initializes the library.
 *        Clocks and the system timer are initialized by the Pico SDK runtime before main(),
 *        millis() needs no interrupt.
 */
static inline void lib_setup()
{
    SystemCoreClock = clock_get_hz(clk_sys);
}
#endif

/**
 * @brief The main of the Selfbus library.
 *        Calls setup(), loop() and optional loop_noapp() from the application.
 *
 * @return will never return
 */
int main()
{
    lib_setup();
    BcuBase* bcu = setup();

    while (1)
    {
        bcu->loop();
        if (bcu->applicationRunning())
            loop();
        else
            loop_noapp();
    }
}
