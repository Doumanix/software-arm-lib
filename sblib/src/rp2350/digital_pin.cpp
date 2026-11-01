/*
 *  digital_pin.cpp - Functions for digital I/O, RP2350 implementation.
 *
 *  The PinMode values of the LPC11xx (see digital_pin.h) are mapped to the RP2350 GPIO:
 *    INPUT, OUTPUT          SIO input / output
 *    PULL_UP, PULL_DOWN     pull-up / pull-down resistor
 *    REPEATER_MODE          pull-up and pull-down = bus keeper
 *    HYSTERESIS             ignored, the Schmitt trigger of the RP2350 is always enabled
 *    INPUT_ANALOG           digital input buffer disabled, no pulls (ADC input)
 *    PinModeFunc(PF_RXD...) UART, SPI, I2C function of the pin
 *    INPUT_CAPTURE,         SIO input / output. Capture and match of the LPC11xx timers are
 *    OUTPUT_MATCH           emulated by the Timer class (porting step M5). With SBLIB_RP2350_PIO_BUS
 *                           PIN_EIB_TX gets the function PIO0: a state machine of the timer emulation
 *                           switches it (order 052).
 *    OPEN_DRAIN             not supported (fatalError)
 *
 *  Pins that do not exist on the chip (e.g. GPIO_NC) are ignored.
 *
 *  GPIO interrupts: all GPIOs share IO_IRQ_BANK0. pinInterruptMode() configures the events
 *  of a pin, pinEnableInterrupt()/pinDisableInterrupt() switch them. The interrupt handler
 *  is registered with the Pico SDK (gpio_add_raw_irq_handler) by the module using it and
 *  the interrupt is enabled with enableInterrupt(IO_IRQ_BANK0).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include "sblib/digital_pin.h"
#include "sblib/platform.h"
#include "sblib/utils.h"
#if defined(SBLIB_RP2350_PIO_BUS)
#   include "sblib/io_pin_names.h"
#endif

#include <hardware/timer.h>

namespace
{
    constexpr uint32_t PinModeTypeMask = 0xf000;
    constexpr uint32_t OpenDrainBit = OPEN_DRAIN;

    // GPIO interrupt events (GPIO_IRQ_xxx) per pin, set by pinInterruptMode()
    uint8_t interruptEvents[NUM_BANK0_GPIOS];

    bool isValidGpio(const uint32_t gpio)
    {
        return gpio < NUM_BANK0_GPIOS;
    }

    gpio_function_t gpioFunction(const PinFunc func)
    {
        switch (func)
        {
            case PF_NONE:
            case PF_PIO:
            case PF_MAT: // emulated by the Timer class
            case PF_CAP: // emulated by the Timer class
                return GPIO_FUNC_SIO;

            case PF_RXD:
            case PF_TXD:
            case PF_RTS:
            case PF_CTS:
                return GPIO_FUNC_UART;

            case PF_SCK:
            case PF_MISO:
            case PF_MOSI:
            case PF_SSEL:
                return GPIO_FUNC_SPI;

            case PF_SDA:
            case PF_SCL:
                return GPIO_FUNC_I2C;

            default:
                fatalError(); // the function is not available on the RP2350
                return GPIO_FUNC_NULL;
        }
    }

    // Same evaluation as the LPC11xx GPIO registers IS (level), IBE (both edges) and IEV (high/rising)
    uint32_t gpioInterruptEvents(const uint16_t interruptMode)
    {
        const bool highOrRising = interruptMode & 0x0001;
        if (interruptMode & 0x0100)
        {
            uint32_t events = GPIO_IRQ_LEVEL_LOW;
            if (highOrRising)
            {
                events = GPIO_IRQ_LEVEL_HIGH;
            }
            return events;
        }
        if (interruptMode & 0x0010)
        {
            return GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;
        }
        uint32_t events = GPIO_IRQ_EDGE_FALL;
        if (highOrRising)
        {
            events = GPIO_IRQ_EDGE_RISE;
        }
        return events;
    }

    constexpr uint32_t AllInterruptEvents =
            GPIO_IRQ_LEVEL_LOW | GPIO_IRQ_LEVEL_HIGH | GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;

} // namespace

void pinMode(const uint32_t pin, const uint32_t mode)
{
    const uint32_t gpio = digitalPinToGpio(pin);
    if (!isValidGpio(gpio))
    {
        return;
    }

    if (mode & OpenDrainBit)
    {
        fatalError(); // open drain output is not supported by the RP2350 GPIO
    }

    const uint32_t type = mode & PinModeTypeMask;

    if (type == INPUT_ANALOG)
    {
        // same as adc_gpio_init(), without the dependency on hardware_adc
        gpio_set_function(gpio, GPIO_FUNC_NULL);
        gpio_disable_pulls(gpio);
        gpio_set_input_enabled(gpio, false);
        return;
    }

    // Pull resistors: PULL_UP and PULL_DOWN together (REPEATER_MODE) is the bus keeper
    gpio_set_pulls(gpio, (mode & PULL_UP) == PULL_UP, (mode & PULL_DOWN) == PULL_DOWN);

    // Function. gpio_set_function() also enables the input buffer and removes the pad isolation.
    // The output latch of the pin is not changed, as with the LPC11xx.
#if defined(SBLIB_RP2350_PIO_BUS)
    if (type == OUTPUT_MATCH && gpio == digitalPinToGpio(PIN_EIB_TX))
    {
        gpio_set_function(gpio, GPIO_FUNC_PIO0); // the TX state machine of the timer emulation drives the pin
    }
    else
    {
        gpio_set_function(gpio, gpioFunction(getPinFunction(mode)));
    }
#else
    gpio_set_function(gpio, gpioFunction(getPinFunction(mode)));
#endif

    // As last step, set pin direction after the function is configured
    const bool isOutput = (type == OUTPUT) || (type == OUTPUT_MATCH);
    gpio_set_dir(gpio, isOutput);
}

void pinDirection(const uint32_t pin, const uint32_t dir)
{
    const uint32_t gpio = digitalPinToGpio(pin);
    if (!isValidGpio(gpio))
    {
        return;
    }

    gpio_set_dir(gpio, dir == OUTPUT);
}

void pinInterruptMode(const uint32_t pin, const uint16_t interruptMode, const uint32_t pinModeConfig)
{
    const uint32_t gpio = digitalPinToGpio(pin);
    if (!isValidGpio(gpio))
    {
        return;
    }

    // Ensure that the pin is at least configured as INPUT, otherwise the interrupt configuration will not work.
    pinMode(pin, pinModeConfig | INPUT);

    gpio_set_irq_enabled(gpio, AllInterruptEvents, false);
    interruptEvents[gpio] = static_cast<uint8_t>(gpioInterruptEvents(interruptMode));
    gpio_acknowledge_irq(gpio, AllInterruptEvents); // discard edges detected before

    if (interruptMode & INTERRUPT_ENABLED)
    {
        gpio_set_irq_enabled(gpio, interruptEvents[gpio], true);
    }
}

void pinEnableInterrupt(const uint32_t pin)
{
    const uint32_t gpio = digitalPinToGpio(pin);
    if (!isValidGpio(gpio))
    {
        return;
    }

    gpio_set_irq_enabled(gpio, interruptEvents[gpio], true);
}

void pinDisableInterrupt(const uint32_t pin)
{
    const uint32_t gpio = digitalPinToGpio(pin);
    if (!isValidGpio(gpio))
    {
        return;
    }

    gpio_set_irq_enabled(gpio, AllInterruptEvents, false);
}

void portMode(const uint8_t portNum, uint32_t pinMask, const uint32_t mode)
{
    // Only the PinMode values without a pin function are used, as with the LPC11xx
    const uint32_t modeWithoutFunction = mode & ~PinModeFunc(static_cast<PinFunc>(31));
    for (uint32_t pinNum = 0; pinMask != 0; ++pinNum, pinMask >>= 1)
    {
        if (pinMask & 1)
        {
            pinMode(portNum * 32 + pinNum, modeWithoutFunction);
        }
    }
}

void portDirection(const uint8_t portNum, uint32_t pinMask, const uint32_t dir)
{
    for (uint32_t pinNum = 0; pinMask != 0; ++pinNum, pinMask >>= 1)
    {
        if (pinMask & 1)
        {
            pinDirection(portNum * 32 + pinNum, dir);
        }
    }
}

uint32_t pulseIn(const uint32_t pin, const bool state, const uint32_t timeout)
{
    const uint32_t gpio = digitalPinToGpio(pin);
    if (!isValidGpio(gpio))
    {
        return 0;
    }

    const uint32_t start = time_us_32();
    auto timedOut = [start, timeout]() { return (time_us_32() - start) >= timeout; };

    // Wait for any previous pulse to end
    while (gpio_get(gpio) == state)
    {
        if (timedOut())
        {
            return 0;
        }
    }

    // Wait for the pulse to start
    while (gpio_get(gpio) != state)
    {
        if (timedOut())
        {
            return 0;
        }
    }

    // Wait for the pulse to stop
    const uint32_t pulseStart = time_us_32();
    while (gpio_get(gpio) == state)
    {
        if (timedOut())
        {
            return 0;
        }
    }

    return time_us_32() - pulseStart;
}
