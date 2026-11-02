/*
 * serial_rp2350.cpp - Serial port of the bus monitor example on the RP2350 (Pico 2 test board).
 *
 * The serial driver of the sblib (src/serial.cpp) is not ported to the RP2350 yet. This file
 * implements the parts of class Serial that the example uses, with UART1 of the Pico SDK:
 * TX on GPIO8, RX on GPIO9. Received bytes stay in the 32 byte FIFO of the UART, writing waits
 * for room in the transmit FIFO.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include <sblib/serial.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>

namespace
{
    uart_inst_t* const SerialUart = uart1;
    constexpr uint32_t SerialTxPin = 8;
    constexpr uint32_t SerialRxPin = 9;

    int16_t peekedByte = -1; //!< Byte read by peek(), -1 if none
} // namespace

Serial serial(SerialRxPin, SerialTxPin);

Serial::Serial(uint32_t /* rxPin */, uint32_t /* txPin */) :
    enabled_(false),
    savedFIFOcontrolRegister(0),
    receiveBuffer(nullptr),
    transmitBuffer(nullptr),
    errorCallback(nullptr),
    errorCallbackContext(nullptr)
{}

Serial::~Serial()
{
    end();
}

void Serial::begin(const SerialBaudRate baudRate)
{
    uart_init(SerialUart, baudRate);
    gpio_set_function(SerialTxPin, GPIO_FUNC_UART);
    gpio_set_function(SerialRxPin, GPIO_FUNC_UART);
    peekedByte = -1;
    enabled_ = true;
}

void Serial::end()
{
    if (!enabled_)
    {
        return;
    }
    uart_tx_wait_blocking(SerialUart);
    uart_deinit(SerialUart);
    enabled_ = false;
}

int16_t Serial::read()
{
    if (peekedByte >= 0)
    {
        const int16_t ch = peekedByte;
        peekedByte = -1;
        return ch;
    }
    if (!enabled_ || !uart_is_readable(SerialUart))
    {
        return -1;
    }
    return static_cast<int16_t>(uart_getc(SerialUart));
}

int16_t Serial::peek()
{
    if (peekedByte < 0 && enabled_ && uart_is_readable(SerialUart))
    {
        peekedByte = static_cast<int16_t>(uart_getc(SerialUart));
    }
    return peekedByte;
}

uint32_t Serial::available()
{
    if (peekedByte >= 0)
    {
        return 1;
    }
    if (enabled_ && uart_is_readable(SerialUart))
    {
        return 1;
    }
    return 0;
}

uint32_t Serial::write(const uint8_t ch)
{
    if (!enabled_)
    {
        return 0;
    }
    uart_putc_raw(SerialUart, static_cast<char>(ch));
    return 1;
}

uint32_t Serial::write(const uint8_t* data, const uint32_t count)
{
    if (!enabled_)
    {
        return 0;
    }
    uart_write_blocking(SerialUart, data, count);
    return count;
}

void Serial::flush()
{
    if (enabled_)
    {
        uart_tx_wait_blocking(SerialUart);
    }
}
