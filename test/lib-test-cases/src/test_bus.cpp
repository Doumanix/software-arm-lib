/**
 * Test frame for unit tests of the Bus class, see test_bus.h.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * @par
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "test_bus.h"

#include <sblib/io_pin_names.h>
#include <sblib/timer.h>
#include <sblib/eib/bus_const.h>

void BusTestCallback::finishedSendingTelegram(const bool successful)
{
    finishedCount++;
    lastSuccessful = successful;
}

uint8_t BusTestCallback::getLayerStatus() const
{
    return layerStatus;
}

BusTest::BusTest(const uint16_t ownAddress, const uint8_t layerStatus) :
    bus(nullptr, timer16_1, PIN_EIB_RX, PIN_EIB_TX, CAP0, TIMER_MATCH_MAT0, &callback)
{
    callback.layerStatus = layerStatus;
    bus.setOwnAddress(ownAddress);
    _LPC_TMR16B1.IR = 0;
    _LPC_TMR16B1.TC = 0;
    bus.idleState();
}

BusTest::~BusTest()
{
    // Bus never frees its buffers, it exists as long as the application runs
    delete[] bus.telegram;
    delete[] bus.rx_telegram;
    _LPC_TMR16B1.IR = 0;
}

void BusTest::receive(const uint8_t* octets, const uint16_t count)
{
    // As in state INIT_RX_FOR_RECEIVING_NEW_TEL after the falling edge of the first start bit
    bus.nextByteIndex = 0;
    bus.rx_error = RX_OK;
    bus.checksum = 0xff;
    bus.sendAck = 0;
    bus.valid = true;

    for (uint16_t i = 0; i < count; ++i)
    {
        // All data bits and the parity bit are received, the stop bit ends now
        bus.state = Bus::RECV_BITS_OF_BYTE;
        bus.currentByte = octets[i];
        bus.bitMask = 0x400;
        bus.bitTime = 0;
        bus.parity = true;
        timer16_1.match(bus.timeChannel, BYTE_TIME_INCL_STOP - 1);
        timeout();
        REQUIRE(bus.state == Bus::RECV_WAIT_FOR_STARTBIT_OR_TELEND);
    }

    // No further start bit: end of the frame
    timeout();
}

uint16_t BusTest::sendUntilAcknowledgeWindow()
{
    // Each octet takes about a dozen timeouts, an extended frame up to 263 octets
    constexpr uint32_t MaxTimeouts = 20000;
    uint32_t timeouts = 0;
    while (bus.state != Bus::SEND_WAIT_FOR_RX_ACK_WINDOW && timeouts < MaxTimeouts)
    {
        timeout();
        timeouts++;
    }
    REQUIRE(bus.state == Bus::SEND_WAIT_FOR_RX_ACK_WINDOW);
    return bus.nextByteIndex;
}

void BusTest::timeout()
{
    _LPC_TMR16B1.IR = Timer::flagMask(bus.timeChannel);
    bus.timerInterruptHandler();
}

void setFrameChecksum(uint8_t* frame, const uint16_t length)
{
    uint8_t checksum = 0xff;
    for (uint16_t i = 0; i < length; ++i)
    {
        checksum ^= frame[i];
    }
    frame[length] = checksum;
}
