/*
 *  ioports.h - Definition of the I/O ports and port pins.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_IOPORTS_H_
#define SBLIB_IOPORTS_H_

#include <cstdint>
#include <sblib/platform_detect.h>

#if defined(SBLIB_PLATFORM_RP2350) && defined(PIO0)
#   error "The CMSIS device header RP2350.h defines PIO0..PIO2 and cannot be used together with the sblib"
#endif

/**
 * Digital ports.
 */
enum Port : uint8_t
{
    // Port 1
    PIO0 = 0,

    // Port 1
    PIO1 = 1,

    // Port 2
    PIO2 = 2,

    // Port 3
    PIO3 = 3
};


/**
 * Constants for port pin functions
 */
enum PinFunc : uint8_t
{
    PF_NONE = 0, PF_PIO, PF_AD, PF_RESET, PF_SWDIO, PF_SWCLK, PF_MAT, PF_CAP,
    PF_CLKOUT, PF_SDA, PF_SCL, PF_SSEL, PF_MISO, PF_MOSI, PF_SCK, PF_RXD,
    PF_TXD, PF_RTS, PF_DTR, PF_DSR, PF_CTS, PF_DCD, PF_RI,
    PF_USBP, PF_USB_M, PF_USB_VBUS, PF_USB_CONNECT, PF_USB_FTOGGLE,
    PF_ARM_TRACE_CLK, PF_ARM_TRACE_SWV,
    PF_SCLK
};


/**
 * Offset of the pin function bits in the pin mode value
 */
constexpr uint8_t PinFunctionBitOffset = 18;

/**
 * Select a specific port pin function when setting the pin mode.
 * This is intended to be used in combination with @ref pinMode.
 *
 * Example: pinMode(PIO1_6, INPUT | PinModeFunc(PF_RXD));
 * This enables the UART RXD function on pin PIO1_6.
 */
constexpr uint32_t PinModeFunc(const PinFunc f) {return f << PinFunctionBitOffset;}

/**
 * Extract the pin function from a pin mode value.
 *
 * @param pinMode - The pin mode value containing an encoded pin function
 * @return The extracted @ref PinFunc
 */
constexpr PinFunc getPinFunction(const uint32_t pinMode) {return static_cast<PinFunc>((pinMode >> PinFunctionBitOffset) & 31);}

// Constants for port pin function manipulation
enum PortFunctionManipulator : uint32_t
{
    PFL_ADMODE = 0x100,

    PFF_SHIFT_OFFSET = 5,
    PFF_MASK = (1 << PFF_SHIFT_OFFSET) - 1,

    PF0_SHIFT = 9,
    PF1_SHIFT = PF0_SHIFT + PFF_SHIFT_OFFSET,
    PF2_SHIFT = PF1_SHIFT + PFF_SHIFT_OFFSET,
    PF3_SHIFT = PF2_SHIFT + PFF_SHIFT_OFFSET
};

#define FUNC0(x) ((x) << PF0_SHIFT)
#define FUNC1(x) ((x) << PF1_SHIFT)
#define FUNC2(x) ((x) << PF2_SHIFT)
#define FUNC3(x) ((x) << PF3_SHIFT)

#if defined(SBLIB_PLATFORM_LPC11XX)
/**
 * Port pins.
 *
 * The bits of the constants are used as follows:
 * Bit 0-4:   pin (0..31)
 * Bit 5-6:   port (0..3)
 * Bit 8:     pin has A/D mode selection bit
 * Bit 9-14:  function 0: none, pio, reset, sw-debug (swdio/swclk)
 * Bit 15-19: function 1: none, pio, A/D, timer match, timer capture, clkout,
 *                        I2C (sda/scl), SPI (ssel, miso, mosi, sck),
 *                        SERIAL (rxd, txd, rts, dtr, dsr, cts, dcd, ri)
 * Bit 20-24: function 2: none, A/D, timer match, timer capture,
 *                        SPI (ssel, miso, mosi, sck), SERIAL (rxd, txd)
 * Bit 25-29: function 3: timer match, timer capture, SERIAL (rxd, txd)
 */
enum PortPin : uint32_t
{
    // Port 0 pin 0
    PIO0_0 = 0x00 | FUNC0(PF_RESET) | FUNC1(PF_PIO),

    // Port 0 pin 1
    PIO0_1 = 0x01 | FUNC0(PF_PIO) | FUNC1(PF_CLKOUT) | FUNC2(PF_MAT),

    // Port 0 pin 2
    PIO0_2 = 0x02 | FUNC0(PF_PIO) | FUNC1(PF_SSEL) | FUNC2(PF_CAP),

    // Port 0 pin 3
    PIO0_3 = 0x03 | FUNC0(PF_PIO),

    // Port 0 pin 4
    PIO0_4 = 0x04 | FUNC0(PF_PIO) | FUNC1(PF_SCL),

    // Port 0 pin 5
    PIO0_5 = 0x05 | FUNC0(PF_PIO) | FUNC1(PF_SDA),

    // Port 0 pin 6
    PIO0_6 = 0x06 | FUNC0(PF_PIO) | FUNC1(PF_NONE) | FUNC2(PF_SCK),

    // Port 0 pin 7
    PIO0_7 = 0x07 | FUNC0(PF_PIO) | FUNC1(PF_CTS),

    // Port 0 pin 8
    PIO0_8 = 0x08 | FUNC0(PF_PIO) | FUNC1(PF_MISO) | FUNC2(PF_MAT),

    // Port 0 pin 9
    PIO0_9 = 0x09 | FUNC0(PF_PIO) | FUNC1(PF_MOSI) | FUNC2(PF_MAT),

    // Port 0 pin 10
    PIO0_10 = 0x0a | FUNC0(PF_SWCLK) | FUNC1(PF_PIO) | FUNC2(PF_SCK) | FUNC3(PF_MAT),

    // Port 0 pin 11
    PIO0_11 = 0x0b | FUNC0(PF_NONE) | FUNC1(PF_PIO) | FUNC2(PF_AD) | FUNC3(PF_MAT) | PFL_ADMODE,


    // Port 1 pin 0
    PIO1_0 = 0x20 | FUNC0(PF_NONE) | FUNC1(PF_PIO) | FUNC2(PF_AD) | FUNC3(PF_CAP) | PFL_ADMODE,

    // Port 1 pin 1
    PIO1_1 = 0x21 | FUNC0(PF_NONE) | FUNC1(PF_PIO) | FUNC2(PF_AD) | FUNC3(PF_MAT) | PFL_ADMODE,

    // Port 1 pin 2
    PIO1_2 = 0x22 | FUNC0(PF_NONE) | FUNC1(PF_PIO) | FUNC2(PF_AD) | FUNC3(PF_MAT) | PFL_ADMODE,

    // Port 1 pin 3
    PIO1_3 = 0x23 | FUNC0(PF_SWDIO) | FUNC1(PF_PIO) | FUNC2(PF_AD) | FUNC3(PF_MAT) | PFL_ADMODE,

    // Port 1 pin 4
    PIO1_4 = 0x24 | FUNC0(PF_PIO) | FUNC1(PF_AD) | FUNC2(PF_MAT) | PFL_ADMODE,

    // Port 1 pin 5
    PIO1_5 = 0x25 | FUNC0(PF_PIO) | FUNC1(PF_RTS) | FUNC2(PF_CAP),

    // Port 1 pin 6
    PIO1_6 = 0x26 | FUNC0(PF_PIO) | FUNC1(PF_RXD) | FUNC2(PF_MAT),

    // Port 1 pin 7
    PIO1_7 = 0x27 | FUNC0(PF_PIO) | FUNC1(PF_TXD) | FUNC2(PF_MAT),

    // Port 1 pin 8
    PIO1_8 = 0x28 | FUNC0(PF_PIO) | FUNC1(PF_CAP),

    // Port 1 pin 9
    PIO1_9 = 0x29 | FUNC0(PF_PIO) | FUNC1(PF_MAT) | FUNC2(PF_MOSI),

    // Port 1 pin 10
    PIO1_10 = 0x2a | FUNC0(PF_PIO) | FUNC1(PF_AD) | FUNC2(PF_MAT) | FUNC3(PF_MISO) | PFL_ADMODE,

    // Port 1 pin 11
    PIO1_11 = 0x2b | FUNC0(PF_PIO) | FUNC1(PF_AD) | FUNC2(PF_CAP) | PFL_ADMODE,


    // Port 2 pin 0
    PIO2_0 = 0x40 | FUNC0(PF_PIO) | FUNC1(PF_DTR) | FUNC2(PF_SSEL),

    // Port 2 pin 1
    PIO2_1 = 0x41 | FUNC0(PF_PIO) | FUNC1(PF_DSR) | FUNC2(PF_SCK),

    // Port 2 pin 2
    PIO2_2 = 0x42 | FUNC0(PF_PIO) | FUNC1(PF_DCD) | FUNC2(PF_MISO),

    // Port 2 pin 3
    PIO2_3 = 0x43 | FUNC0(PF_PIO) | FUNC1(PF_RI) | FUNC2(PF_MOSI),

    // Port 2 pin 4
    PIO2_4 = 0x44 | FUNC0(PF_PIO) | FUNC1(PF_MAT) | FUNC2(PF_SSEL),

    // Port 2 pin 5
    PIO2_5 = 0x45 | FUNC0(PF_PIO) | FUNC1(PF_MAT),

    // Port 2 pin 6
    PIO2_6 = 0x46 | FUNC0(PF_PIO) | FUNC1(PF_MAT),

    // Port 2 pin 7
    PIO2_7 = 0x47 | FUNC0(PF_PIO) | FUNC1(PF_MAT) | FUNC2(PF_RXD),

    // Port 2 pin 8
    PIO2_8 = 0x48 | FUNC0(PF_PIO) | FUNC1(PF_MAT) | FUNC2(PF_TXD),

    // Port 2 pin 9
    PIO2_9 = 0x49 | FUNC0(PF_PIO) | FUNC1(PF_CAP),

    // Port 2 pin 10
    PIO2_10 = 0x4a | FUNC0(PF_PIO),

    // Port 2 pin 11
    PIO2_11 = 0x4b | FUNC0(PF_PIO) | FUNC1(PF_SCK) | FUNC2(PF_CAP),


    // Port 3 pin 0
    PIO3_0 = 0x60 | FUNC0(PF_PIO) | FUNC1(PF_DTR) | FUNC2(PF_MAT) | FUNC3(PF_TXD),

    // Port 3 pin 1
    PIO3_1 = 0x61 | FUNC0(PF_PIO) | FUNC1(PF_DSR) | FUNC2(PF_MAT) | FUNC3(PF_RXD),

    // Port 3 pin 2
    PIO3_2 = 0x62 | FUNC0(PF_PIO) | FUNC1(PF_DCD) | FUNC2(PF_MAT) | FUNC3(PF_SCK),

    // Port 3 pin 3
    PIO3_3 = 0x63 | FUNC0(PF_PIO) | FUNC1(PF_RI) | FUNC2(PF_CAP),

    // Port 3 pin 4
    PIO3_4 = 0x64 | FUNC0(PF_PIO) | FUNC1(PF_CAP) | FUNC2(PF_RXD),

    // Port 3 pin 5
    PIO3_5 = 0x65 | FUNC0(PF_PIO) | FUNC1(PF_CAP) | FUNC2(PF_TXD)
};

#undef FUNC0
#undef FUNC1
#undef FUNC2
#undef FUNC3

/**
 * Analog channels.
 */
enum AnalogChannel : uint8_t
{
    AD0 = 0, //!< Analog channel 0 (this is pin PIO0_11)
    AD1 = 1, //!< Analog channel 1 (this is pin PIO1_0)
    AD2 = 2, //!< Analog channel 2 (this is pin PIO1_1)
    AD3 = 3, //!< Analog channel 3 (this is pin PIO1_2)
    AD4 = 4, //!< Analog channel 4 (this is pin PIO1_3)
    AD5 = 5, //!< Analog channel 5 (this is pin PIO1_4)
    AD6 = 6, //!< Analog channel 6 (this is pin PIO1_10)
    AD7 = 7, //!< Analog channel 7 (this is pin PIO1_11)
};

#elif defined(SBLIB_PLATFORM_RP2350)
#   undef FUNC0
#   undef FUNC1
#   undef FUNC2
#   undef FUNC3

/**
 * Port pins of the RP2350 / RP2354.
 *
 * The value of a pin is its GPIO number, so the bits are used as with the LPC11xx:
 * Bit 0-4:   pin (0..31) within the 32 bit GPIO bank
 * Bit 5-6:   "port" = GPIO bank (GPIO0..31 -> 0, GPIO32..47 -> 1)
 * There is no function information encoded. The pin function is selected with
 * @ref PinModeFunc in @ref pinMode.
 *
 * QFN-60 packages (RP2350A, e.g. Pico 2) have GPIO0..29, QFN-80 packages
 * (RP2350B, RP2354B) GPIO0..47.
 *
 * The LPC11xx names PIOx_y are intentionally not available. A board header maps the
 * functions of a controller board (bus RX/TX, programming button, I/Os) to GPIOs,
 * see sblib/io_pin_names.h.
 */
enum PortPin : uint32_t
{
    GPIO0 = 0,   GPIO1 = 1,   GPIO2 = 2,   GPIO3 = 3,   GPIO4 = 4,   GPIO5 = 5,   GPIO6 = 6,   GPIO7 = 7,
    GPIO8 = 8,   GPIO9 = 9,   GPIO10 = 10, GPIO11 = 11, GPIO12 = 12, GPIO13 = 13, GPIO14 = 14, GPIO15 = 15,
    GPIO16 = 16, GPIO17 = 17, GPIO18 = 18, GPIO19 = 19, GPIO20 = 20, GPIO21 = 21, GPIO22 = 22, GPIO23 = 23,
    GPIO24 = 24, GPIO25 = 25, GPIO26 = 26, GPIO27 = 27, GPIO28 = 28, GPIO29 = 29, GPIO30 = 30, GPIO31 = 31,
    GPIO32 = 32, GPIO33 = 33, GPIO34 = 34, GPIO35 = 35, GPIO36 = 36, GPIO37 = 37, GPIO38 = 38, GPIO39 = 39,
    GPIO40 = 40, GPIO41 = 41, GPIO42 = 42, GPIO43 = 43, GPIO44 = 44, GPIO45 = 45, GPIO46 = 46, GPIO47 = 47,

    /**
     * Pin not connected / not available on a board (e.g. PIN_VBUS without bus voltage measurement).
     * digitalWrite() and digitalRead() ignore it, as do all pins >= NUM_BANK0_GPIOS.
     */
    GPIO_NC = 0x7f
};

/**
 * Analog channels of the RP2350 ADC.
 *
 * QFN-60 (RP2350A, Pico 2): AD0..AD3 = GPIO26..29.
 * QFN-80 (RP2350B, RP2354B): AD0..AD7 = GPIO40..47.
 * The internal temperature sensor is not part of this enum.
 */
enum AnalogChannel : uint8_t
{
    AD0 = 0, //!< Analog channel 0 (GPIO26 on QFN-60, GPIO40 on QFN-80)
    AD1 = 1, //!< Analog channel 1 (GPIO27 on QFN-60, GPIO41 on QFN-80)
    AD2 = 2, //!< Analog channel 2 (GPIO28 on QFN-60, GPIO42 on QFN-80)
    AD3 = 3, //!< Analog channel 3 (GPIO29 on QFN-60, GPIO43 on QFN-80)
    AD4 = 4, //!< Analog channel 4 (GPIO44, QFN-80 only)
    AD5 = 5, //!< Analog channel 5 (GPIO45, QFN-80 only)
    AD6 = 6, //!< Analog channel 6 (GPIO46, QFN-80 only)
    AD7 = 7, //!< Analog channel 7 (GPIO47, QFN-80 only)
};
#endif /* SBLIB_PLATFORM_RP2350 */

/**
 * Find the function number for the function of a pin.
 *
 * @param pin - the pin
 * @param func - the port function to find, e.g. PF_PIO
 * @return the function number, or -1 if not found
 */
int8_t getPinFunctionNumber(uint32_t pin, uint16_t func);

#endif /* SBLIB_IOPORTS_H_ */
