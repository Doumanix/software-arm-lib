/*
 *  utils.cpp - Utility functions.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */

#include <sblib/utils.h>

#include <sblib/digital_pin.h>
#include <sblib/platform.h>
#include <sblib/io_pin_names.h>
#if defined(SBLIB_PLATFORM_RP2350)
#   include <hardware/timer.h>
#endif
#include <cstring>

static int fatalErrorPin = PIN_PROG;
static int eibTxPin = PIN_EIB_TX; ///\todo make it universal

void reverseCopy(uint8_t* dest, const uint8_t* src, int len)
{
    src += len - 1;
    while (len > 0)
    {
        *dest++ = *src--;
        --len;
    }
}

void setPinsInSecureModes()
{
    pinMode(fatalErrorPin, OUTPUT);
    pinMode(eibTxPin, INPUT);
}

#if defined(SBLIB_PLATFORM_LPC11XX)
void fatalError()
{
    // We use only low level functions here as a fatalError() could happen
    // anywhere in the lib and we want to ensure that the function works
    SysTick_Config(0x1000000);
    setPinsInSecureModes();
    while (1)
    {
        // Blink the fatalErrorLED
        digitalWrite(fatalErrorPin, (SysTick->VAL & 0x400000) == 0 ? 1 : 0);
    }
}

void HardFault_Handler()
{
    // We use only low level functions here as a HardFault could happen
    // anywhere and we want to ensure that the function works
    SysTick_Config(0x1000000);
    setPinsInSecureModes();
    while (1)
    {
        // Blink the fatalErrorLED
        digitalWrite(fatalErrorPin, (SysTick->VAL & 0x200000) == 0 ? 1 : 0);
    }
}
#elif defined(SBLIB_PLATFORM_RP2350)
// The blink timing uses the free running microsecond counter of the RP2350 timer,
// which works without interrupts. Blink periods are similar to the LPC11xx.

void fatalError()
{
    setPinsInSecureModes();
    while (1)
    {
        // Blink the fatalErrorLED, toggles every 65 ms
        digitalWrite(fatalErrorPin, (time_us_32() & 0x10000) == 0);
    }
}

void HardFault_Handler()
{
    setPinsInSecureModes();
    while (1)
    {
        // Blink the fatalErrorLED, toggles every 33 ms
        digitalWrite(fatalErrorPin, (time_us_32() & 0x8000) == 0);
    }
}

// Exception handler name of the Pico SDK vector table
extern "C" void isr_hardfault() __attribute__((alias("HardFault_Handler")));
#endif

void setFatalErrorPin(const int newPin)
{
    fatalErrorPin = newPin;
}

void setKNX_TX_Pin(const int newTxPin)
{
    eibTxPin = newTxPin;
}

