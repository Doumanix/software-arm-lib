/*
 * Host tests of the unchanged RP digital pin implementation.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include <rp2350/digital_pin.cpp>
#include "sblib/io_pin_names.h"
#include <stdexcept>
#include <catch.hpp>

namespace
{
    struct PinFixture
    {
        PinFixture()
        {
            sim::reset();
            std::fill(std::begin(interruptEvents), std::end(interruptEvents), 0);
        }
    };
    unsigned irqCalls = 0;
    void onPinIrq()
    {
        ++irqCalls;
        gpio_acknowledge_irq(7, GPIO_IRQ_EDGE_FALL);
    }
} // namespace
TEST_CASE_METHOD(PinFixture, "Pin modes map direction pulls input and latch", "[pin]")
{
    for (const uint32_t mode: {INPUT, OUTPUT, INPUT_CAPTURE, OUTPUT_MATCH})
    {
        digitalWrite(7, true);
        pinMode(7, mode | REPEATER_MODE | HYSTERESIS);
        const auto& c = sim::pinConfig(7);
        REQUIRE(c.function == GPIO_FUNC_SIO);
        REQUIRE(c.output == (mode == OUTPUT || mode == OUTPUT_MATCH));
        REQUIRE(c.pullUp);
        REQUIRE(c.pullDown);
        REQUIRE(c.inputEnabled);
        REQUIRE(digitalRead(7));
    }
    pinMode(7, INPUT | PULL_UP);
    REQUIRE(sim::pinConfig(7).pullUp);
    REQUIRE_FALSE(sim::pinConfig(7).pullDown);
    pinMode(7, INPUT | PULL_DOWN);
    REQUIRE_FALSE(sim::pinConfig(7).pullUp);
    REQUIRE(sim::pinConfig(7).pullDown);
    pinMode(7, INPUT);
    REQUIRE_FALSE(sim::pinConfig(7).pullUp);
    REQUIRE_FALSE(sim::pinConfig(7).pullDown);
    pinMode(7, INPUT_ANALOG | REPEATER_MODE);
    REQUIRE(sim::pinConfig(7).function == GPIO_FUNC_NULL);
    REQUIRE_FALSE(sim::pinConfig(7).inputEnabled);
    REQUIRE_FALSE(sim::pinConfig(7).pullUp);
    REQUIRE_FALSE(sim::pinConfig(7).pullDown);
    pinMode(7, INPUT);
    REQUIRE(sim::pinConfig(7).inputEnabled);
    REQUIRE_THROWS_AS(pinMode(7, OUTPUT | OPEN_DRAIN), std::runtime_error);
}
TEST_CASE_METHOD(PinFixture, "Pin peripheral function mappings", "[pin]")
{
    for (const PinFunc function: {PF_RXD, PF_TXD, PF_RTS, PF_CTS})
    {
        pinMode(7, INPUT | PinModeFunc(function));
        REQUIRE(sim::pinConfig(7).function == GPIO_FUNC_UART);
    }
    for (const PinFunc function: {PF_SCK, PF_MISO, PF_MOSI, PF_SSEL})
    {
        pinMode(7, OUTPUT | PinModeFunc(function));
        REQUIRE(sim::pinConfig(7).function == GPIO_FUNC_SPI);
    }
    for (const PinFunc function: {PF_SDA, PF_SCL})
    {
        pinMode(7, INPUT | PinModeFunc(function));
        REQUIRE(sim::pinConfig(7).function == GPIO_FUNC_I2C);
    }
    for (const PinFunc function: {PF_NONE, PF_PIO, PF_MAT, PF_CAP})
    {
        pinMode(7, INPUT | PinModeFunc(function));
        REQUIRE(sim::pinConfig(7).function == GPIO_FUNC_SIO);
    }
    REQUIRE_THROWS_AS(pinMode(7, PinModeFunc(static_cast<PinFunc>(31))), std::runtime_error);
}
TEST_CASE_METHOD(PinFixture, "Pin bus match function follows selected backend", "[pin]")
{
    pinMode(PIN_EIB_TX, OUTPUT_MATCH);
#if defined(SBLIB_RP2350_PIO_BUS)
    REQUIRE(sim::pinConfig(digitalPinToGpio(PIN_EIB_TX)).function == GPIO_FUNC_PIO0);
#else
    REQUIRE(sim::pinConfig(digitalPinToGpio(PIN_EIB_TX)).function == GPIO_FUNC_SIO);
#endif
    REQUIRE(sim::pinConfig(digitalPinToGpio(PIN_EIB_TX)).output);
}
TEST_CASE_METHOD(PinFixture, "Pin invalid pins have no SDK effects", "[pin]")
{
    for (const uint32_t pin: {static_cast<uint32_t>(GPIO_NC), 48u, 255u})
    {
        pinMode(pin, OUTPUT | OPEN_DRAIN);
        pinDirection(pin, OUTPUT);
        pinInterruptMode(pin, INTERRUPT_ENABLED | INTERRUPT_EDGE_BOTH);
        pinEnableInterrupt(pin);
        pinDisableInterrupt(pin);
        digitalWrite(pin, true);
        REQUIRE_FALSE(digitalRead(pin));
        REQUIRE(pulseIn(pin, true, 10) == 0);
    }
    REQUIRE(sim::gpioCalls() == 0);
    for (uint gpio = 0; gpio < NUM_BANK0_GPIOS; ++gpio)
    {
        REQUIRE(sim::pinConfig(gpio).calls == 0);
        REQUIRE(sim::pinIrqEvents(gpio) == 0);
        REQUIRE_FALSE(sim::pinLevel(gpio));
    }
}
TEST_CASE_METHOD(PinFixture, "Pin digital IO and port masks preserve unrelated bits", "[pin]")
{
    digitalWrite(7, true);
    REQUIRE(digitalRead(7));
    digitalWrite(7, false);
    REQUIRE_FALSE(digitalRead(7));
    digitalWrite(8, true);
    portWriteMask(0, (1u << 7) | (1u << 31), true);
    REQUIRE(digitalRead(7));
    REQUIRE(digitalRead(8));
    REQUIRE(digitalRead(31));
    portWriteMask(1, (1u << 0) | (1u << 15) | (1u << 31), true);
    REQUIRE(digitalRead(32));
    REQUIRE(digitalRead(47));
    REQUIRE_FALSE(digitalRead(46));
    portWriteMask(0, 1u << 7, false);
    REQUIRE_FALSE(digitalRead(7));
    REQUIRE(digitalRead(31));
    portWriteMask(1, 1u << 15, false);
    REQUIRE_FALSE(digitalRead(47));
    REQUIRE(digitalRead(32));
    portMode(1, 3, OUTPUT | PULL_UP | PinModeFunc(PF_TXD));
    REQUIRE(sim::pinConfig(32).function == GPIO_FUNC_SIO);
    REQUIRE(sim::pinConfig(33).output);
    REQUIRE(sim::pinConfig(33).pullUp);
    REQUIRE(sim::pinConfig(34).calls == 0);
    portDirection(1, 1, INPUT);
    REQUIRE_FALSE(sim::pinConfig(32).output);
    REQUIRE(sim::pinConfig(33).output);
    pinDirection(33, INPUT);
    REQUIRE_FALSE(sim::pinConfig(33).output);
}
TEST_CASE_METHOD(PinFixture, "Pin interrupt modes select and retain SDK events", "[pin]")
{
    const uint16_t modes[] = {INTERRUPT_LEVEL_LOW, INTERRUPT_LEVEL_HIGH, INTERRUPT_EDGE_FALLING, INTERRUPT_EDGE_RISING,
                              INTERRUPT_EDGE_BOTH};
    const uint32_t events[] = {GPIO_IRQ_LEVEL_LOW, GPIO_IRQ_LEVEL_HIGH, GPIO_IRQ_EDGE_FALL, GPIO_IRQ_EDGE_RISE,
                               GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE};
    for (unsigned i = 0; i < 5; ++i)
    {
        sim::fallingEdge(7);
        pinInterruptMode(7, modes[i], PULL_UP);
        REQUIRE(sim::pinIrqEvents(7) == 0);
        REQUIRE(sim::pinConfig(7).pullUp);
        REQUIRE_FALSE(sim::pinConfig(7).output);
        pinEnableInterrupt(7);
        REQUIRE(sim::pinIrqEvents(7) == events[i]);
        REQUIRE(gpio_get_irq_event_mask(7) == 0);
        pinDisableInterrupt(7);
        REQUIRE(sim::pinIrqEvents(7) == 0);
        pinInterruptMode(7, modes[i] | INTERRUPT_ENABLED);
        REQUIRE(sim::pinIrqEvents(7) == events[i]);
    }
}
TEST_CASE_METHOD(PinFixture, "Pin IRQ registration dispatch disable and reenable", "[pin]")
{
    irqCalls = 0;
    gpio_add_raw_irq_handler(7, onPinIrq);
    irq_set_enabled(IO_IRQ_BANK0, true);
    pinInterruptMode(7, INTERRUPT_EDGE_FALLING | INTERRUPT_ENABLED);
    sim::fallingEdge(7);
    REQUIRE(irqCalls == 1);
    pinDisableInterrupt(7);
    sim::fallingEdge(7);
    REQUIRE(irqCalls == 1);
    pinEnableInterrupt(7);
    REQUIRE(irqCalls == 2);
    gpio_remove_raw_irq_handler(7, onPinIrq);
    pinDisableInterrupt(7);
    sim::fallingEdge(7);
    REQUIRE(irqCalls == 2);
}
