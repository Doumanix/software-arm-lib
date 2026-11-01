/*
 *  platform.h - Low level hardware specific stuff.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *  Copyright (c) 2026 Doumanix <doumanix@gmx.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_PLATFORM_H_
#define SBLIB_PLATFORM_H_

// Per Herb Sutter, the register keyword did not have any effect on C++ programs at all:
//
//     https://www.drdobbs.com/keywords-that-arent-or-comments-by-anoth/184403859
//
// That was back in 2003, but ARM still used the keyword in CMSIS. Nowadays, with C++17
// using register yields the following
//
//     warning: ISO C++17 does not allow 'register' storage class specifier [-Wregister]
//
// As compiler output is indeed exactly the same, regardless of whether register is
// specified or not, it's ok to hide it from the compiler with the preprocessor's help.
#define register // must be declared before the LPC11xx.h include

#include <sblib/platform_detect.h>

#if defined(SBLIB_PLATFORM_LPC11XX)
#   include <LPC11xx.h>
#elif defined(SBLIB_PLATFORM_RP2350)
    // Only the Pico SDK API is used. The CMSIS device header RP2350.h (cmsis_core) must not be
    // included together with the sblib: its peripheral macros PIO0, PIO1, PIO2 collide with
    // the enum Port and it defines its own IRQn_Type.
#   include <hardware/gpio.h>
#   include <hardware/irq.h>
#   include <hardware/sync.h>
    /**
     * @brief Interrupt number type, as with CMSIS on the LPC11xx (values e.g. IO_IRQ_BANK0, TIMER0_IRQ_0)
     */
    using IRQn_Type = irq_num_t;
    // CMSIS name of the NOP intrinsic, as used in the platform independent sources
#   define __NOP() __nop()
    /**
     * @brief System clock in Hz, as the CMSIS variable of the LPC11xx (set in main())
     */
    extern uint32_t SystemCoreClock;
    /**
     * @brief System reset, as the CMSIS function of the LPC11xx (watchdog reboot, see src/rp2350/platform.cpp)
     */
    [[noreturn]] void NVIC_SystemReset();
    /**
     * @brief 32 bit random number from the hardware (TRNG and ROSC via pico_rand), e.g. for a random start delay
     */
    uint32_t hwRandom();
#endif

#include <cstdint>
#include <sblib/ioports.h>

#if defined(SBLIB_PLATFORM_LPC11XX)
/**
 * Low level table of the IO ports
 */
extern LPC_GPIO_TypeDef* const gpioPorts[4];

/**
 * Get a pointer to a low level IO configuration register.
 *
 * @param pin - the IO pin to get the configuration for
 * @return a pointer to the IO configuration register.
 */
uint32_t* ioconPointer(PortPin pin);

/**
 * Get a pointer to a low level IO configuration register.
 *
 * @param port - the IO port to get the configuration for
 * @param pinNum - the number of the pin to get the configuration for
 * @return a pointer to the IO configuration register.
 */
uint32_t* ioconPointer(Port port, uint8_t pinNum);
#endif /* SBLIB_PLATFORM_LPC11XX */

#ifdef IAP_EMULATION
    extern uint8_t FLASH[];
#   define LPC_FLASH_BASE (FLASH)
#endif

#if defined(SBLIB_PLATFORM_RP2350) && !defined(LPC_FLASH_BASE)
    // The flash is memory mapped (XIP) at 0x10000000. The name LPC_FLASH_BASE is kept for compatibility.
#   define LPC_FLASH_BASE XIP_BASE
#endif

#ifndef LPC_FLASH_BASE
#   define LPC_FLASH_BASE 0
#endif


#if defined(FLASH_SECTOR_SIZE) || defined(FLASH_PAGE_SIZE)
    // both define FLASH_SECTOR_SIZE and FLASH_PAGE_SIZE
#   error "Include <hardware/flash.h> (Pico SDK) after <sblib/platform.h>"
#endif

#define FLASH_BASE_ADDRESS   ((uint8_t *)LPC_FLASH_BASE) //!< The base address of the flash

constexpr uint16_t FLASH_SECTOR_SIZE    = 0x1000;              //!< The size of a flash sector in bytes
constexpr uint16_t FLASH_PAGE_SIZE      = 0x100;               //!< The size of a flash page in bytes
constexpr uint16_t FLASH_PAGE_ALIGNMENT = FLASH_PAGE_SIZE - 1; //!< Page alignment which is allowed to flash
constexpr uint8_t FLASH_RAM_BUFFER_ALIGNMENT = 4;              //!< MCU's RAM buffer alignment, which is allowed to flash


#endif /* SBLIB_PLATFORM_H_ */
