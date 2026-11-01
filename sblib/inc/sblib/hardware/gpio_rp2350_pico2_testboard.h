/*
 *  gpio_rp2350_pico2_testboard.h - Pin mapping of the RP2350 test setup:
 *  Raspberry Pi Pico 2 (RP2350A, QFN-60, 4 MB flash) with a KNX bus coupler.
 *
 *  Selected with SBLIB_BOARD=pico2_testboard (CMake), which defines
 *  SBLIB_BOARD_RP2350_PICO2_TESTBOARD.
 *
 *  Bus coupler polarity is the same as with the LPC11xx controllers:
 *  PIN_EIB_TX idle LOW, HIGH during a 0-bit pulse; PIN_EIB_RX LOW during a 0-bit.
 *
 *  Reserved by the Pico 2 itself: GPIO23 (SMPS mode), GPIO24 (USB VBUS sense),
 *  GPIO25 (LED), GPIO29 (VSYS/3 ADC).
 *
 *  @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_HARDWARE_GPIO_RP2350_PICO2_TESTBOARD_H_
#define SBLIB_HARDWARE_GPIO_RP2350_PICO2_TESTBOARD_H_

#include <sblib/ioports.h>

// KNX bus coupler
#define PIN_EIB_TX  GPIO27
#define PIN_EIB_RX  GPIO28

// Programming button and LED (combined on one pin as with the LPC11xx controllers).
// Note: GPIO0 can't be used for PIN_PROG, BcuBase treats pin value 0 as "no programming pin".
#define PIN_PROG    GPIO22
#define PIN_APRG    PIN_PROG

// Status LEDs, bus voltage measurement
#define PIN_INFO    GPIO_NC     // not available
#define PIN_RUN     GPIO25      // Pico 2 on-board LED
#define PIN_VBUS    GPIO_NC     // no bus voltage measurement

// Debug serial port (UART1, 115200 8N1)
#define PIN_TX      GPIO8
#define PIN_RX      GPIO9

// I2C (I2C0)
#define PIO_SDA     GPIO4
#define PIO_SCL     GPIO5

// SPI: not available, GPIO16..19 are used by the application (e.g. out8 relay outputs)
#define PIN_SSEL0   GPIO_NC
#define PIN_SCK0    GPIO_NC
#define PIN_MISO0   GPIO_NC
#define PIN_MOSI0   GPIO_NC

// Free for applications: GPIO2, GPIO3, GPIO6, GPIO7, GPIO10..21, GPIO26 (GPIO0/1 kept free for UART0).
// Application specific mappings (e.g. relay outputs) belong to the application.

#endif /* SBLIB_HARDWARE_GPIO_RP2350_PICO2_TESTBOARD_H_ */
