/**
 * Test frame for unit tests of the Bus class.
 *
 * The tests call Bus::timerInterruptHandler() directly and drive it with the emulated timer registers of
 * timer16_1 (_LPC_TMR16B1), as the transport layer tests do. A received octet enters the state machine in
 * RECV_BITS_OF_BYTE at the end of its stop bit, so the octet store, the checksum, the end of frame and
 * Bus::handleTelegram() run as on the bus. The bit timing and collisions are not simulated.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * @par
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#ifndef TEST_BUS_H_
#define TEST_BUS_H_

#include <cstdint>

// catch.hpp has to be included before private and protected are redefined, see protocol.h
#include <catch.hpp>

#define private   public
#define protected public
#   include <sblib/eib/bus.h>
#undef private
#undef protected

#include <sblib/eib/bcu_const.h>
#include <sblib/eib/callback_bus.h>

/**
 * Callback of the bus under test: provides the layer status and records the end of sending.
 */
class BusTestCallback : public CallbackBus
{
public:
    void finishedSendingTelegram(bool successful) override;
    [[nodiscard]] uint8_t getLayerStatus() const override;

    uint8_t layerStatus = BCU_STATUS_LINK_LAYER | BCU_STATUS_TRANSPORT_LAYER; //!< Layer status returned to the bus
    uint16_t finishedCount = 0;  //!< Number of calls of finishedSendingTelegram()
    bool lastSuccessful = false; //!< Parameter of the last call of finishedSendingTelegram()
};

/**
 * A bus on timer16_1 in state IDLE, as after Bus::begin() and 50 bit times without bus activity.
 */
class BusTest
{
public:
    /**
     * @param ownAddress  - The physical address of the bus
     * @param layerStatus - The layer status the callback returns
     */
    BusTest(uint16_t ownAddress, uint8_t layerStatus);
    ~BusTest();

    BusTest(const BusTest&) = delete;
    BusTest& operator=(const BusTest&) = delete;

    /**
     * Receive a frame octet by octet and end it with the timeout after the last octet.
     *
     * @param octets - The octets of the frame including the checksum
     * @param count  - The number of octets, also more than TelegramBufferSize
     */
    void receive(const uint8_t* octets, uint16_t count);

    /**
     * Run the send process of the pending telegram with timeouts only (no collision) until the bus waits for
     * the acknowledge frame.
     *
     * @return The number of octets sent
     */
    uint16_t sendUntilAcknowledgeWindow();

    /**
     * Trigger the timeout of the time channel (no capture event).
     */
    void timeout();

    BusTestCallback callback;
    Bus bus;
};

/**
 * Set the checksum octet of a frame.
 *
 * @param frame  - The frame
 * @param length - The length of the frame without the checksum, the checksum is stored in frame[length]
 */
void setFrameChecksum(uint8_t* frame, uint16_t length);

#endif /* TEST_BUS_H_ */
