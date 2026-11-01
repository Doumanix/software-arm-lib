/*
 *  bus.h - Low level EIB bus access.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *  Copyright (c) 2026 Doumanix <doumanix@gmx.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_KNX_BUS_H_
#define SBLIB_KNX_BUS_H_

#include <sblib/timer.h>
#include <sblib/eib/addr_tables.h>
#include <sblib/eib/bcu_const.h>
#include <sblib/eib/bus_const.h>
#include <sblib/eib/callback_bus.h>

/**
 * Low level class for EIB bus access.
 *
 * When creating a bus object, the handler for the timer must also be
 * created. Example:
 *
 * Bus myBus(timer32_0);
 * BUS_TIMER_INTERRUPT_HANDLER(TIMER32_0_IRQHandler, myBus);
 */
class Bus
{
public:
    /**
     * Create a bus access object.
     *
     * @param addrTable       - The address table of the associated bcu
     * @param aTimer          - The timer to use.
     * @param aRxPin          - The pin for receiving from the bus, e.g. PIO1_8
     * @param aTxPin          - The pin for sending to the bus, e.g. PIO1_10
     * @param aCaptureChannel - the timer capture channel of rxPin, e.g. CAP0
     * @param aPwmChannel     - the timer match channel of txPin, e.g. MAT0
     * @param aCallback       - Callback class to inform bcu of events, e.g. telegram sending finished
     */
    Bus(AddrTables* addrTable, Timer& aTimer, const uint32_t& aRxPin, const uint32_t& aTxPin,
        const TimerCapture& aCaptureChannel, const TimerMatch& aPwmChannel, CallbackBus* aCallback);

    /**
     * Begin using the bus.
     *
     * This powers on all used components.
     * This method must be called before the bus can be used.
     *
     * @param physicalAddress - The physical KNX address to use for bus communication.
     */
    void begin(uint16_t physicalAddress);

    /**
     * Waits for a safe time to pause bus access, then pauses bus access.
     *
     * @pre Call from privileged Thread mode with interrupts enabled, no other priority mask
     *      blocking bus interrupts, and the bus timer running with its wake sources enabled.
     *      This method enables interrupts on return; it is not a nested critical section.
     * @param waitForTelegramSent - Whether to wait for an enqueued telegram to be sent or not.
     *                              When passing false, the enqueued telegram is sent out after resume().
     */
    void pause(bool waitForTelegramSent = false);

    /**
     * Resume bus access after it had been paused.
     */
    void resume();

    /**
     * End using the bus.
     *
     * This powers the bus off.
     */
    void end();

    /**
     * The Bus processing loop. This is like the application's loop() function,
     * and is called automatically by main() when the BCU is activated with bcu.begin().
     */
    void loop();

    /**
     * Interface to upper layer for sending a telegram
     *
     * Is called from within the BCU-loop method. Is blocking if there is no free buffer pointer.
     *
     * The procedure is handling two buffer pointers: sendCurTel and SendNextTel, the received pointer is loaded to an available
     * free pointer or is waiting for a free pointer
     *
     * !!! As we have only one buffer at BCU level, the check for buffer free is needed on application before any buffer data is
     * written !!!
     *
     * A free buffer is indicated with "0" as the first byte in the buffer: buffer[0]=0
     *
     * Send a telegram. The checksum byte will be added at the end of telegram[].
     * Ensure that there is at least one byte space at the end of telegram[].
     *
     * @param telegram - the telegram to be sent.
     * @param length - the length of the telegram in sbSendTelegram[], without the checksum
     */
    void sendTelegram(unsigned char* telegram, unsigned short length);

    /**
     * This method is called by the timer interrupt handler.
     * Consider it to be a private method and do not call it.
     */
    void timerInterruptHandler();

    /**
     * Test if there is a frame being sent (data frame or acknowledge frame).
     *
     * @return True if there is a frame to be sent, false if not.
     */
    [[nodiscard]] bool sendingFrame() const;

    /**
     * Test if there is a received telegram in @ref telegram.
     *
     * @return True if there is a telegram in @ref telegram, false if not.
     */
    [[nodiscard]] bool telegramReceived() const;

    /**
     * Discard the received telegram. Call this method when you successfully
     * processed the telegram.
     */
    void discardReceivedTelegram();

    /**
     * Set the number of retries that we do send a telegram when it is not ACKed.
     *
     * @param retries - the number of retries. Default: 3.
     */
    void maxSendRetries(uint8_t retries);

    /**
     * Set the number of busy retries that we do send a telegram when we receive a BUSY from remote.
     *
     * @param retries - The number of retries.
     */
    void maxSendBusyRetries(uint8_t retries);

    void setOwnAddress(uint16_t newAddress);

    void setAddressTable(AddrTables* newAddressTable);

    /**
     * The received telegram.
     * The higher layer process should not change the telegram data in the buffer!
     */
    uint8_t* telegram = new uint8_t[TelegramBufferSize]{};

    /**
      * The total length of the received telegram in telegram[].
      */
    volatile uint8_t telegramLen = 0;

private:
    /**
     * Determines whether it is currently safe to pause bus access.
     *
     * @param waitForTelegramSent - Whether to wait for an enqueued telegram to be sent or not.
     */
    [[nodiscard]] bool canPause(bool waitForTelegramSent) const;

    /**
     * Switch to @ref Bus::INIT state
     * @details Re-sync to bus by ignoring all edges, until there was no edge for at least 50 bit times.
     */
    void initState();

    /**
     * Switch to @ref Bus::IDLE state
     * @details Set the Bus state machine to @ref Bus::IDLE state.
     *          We waited at least 50 Bit times (without cap event enabled),
     *          now we wait for next Telegram to receive.
     *          Configure the capture to falling edge and interrupt
     *          match register for low PWM output
     */
    void idleState();

    /**
     * Switch to @ref Bus::WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE state to trigger sending
     */
    void startSendingImmediately();

    /**
     * Initializes all class variables to prepare for the next transmission.
     */
    void prepareForSending();

    /**
     * Finish the telegram sending process.
     *
     * Notify upper layer of completion and prepare for next telegram transmission.
     */
    void finishSendingTelegram();

    /**
     * Track collision in sending process correctly.
     */
    void encounteredCollision();

    /**
     * Prepare the telegram for sending.
     *
     * @details Set the sender address to our own address, and calculate the checksum of the telegram.
     *          Stores the checksum in telegram[length].
     * @param telegram   The telegram to process
     * @param length     The length of the telegram
     */
    void prepareTelegram(uint8_t* telegram, uint16_t length) const;

    /**
     * Handle the received bytes on a low level. This function is called by
     * the function TIMER16_1_IRQHandler() to decide about further processing of the
     * received bytes.
     *
     * @param valid - 1 if all bytes had correct parity and the checksum is correct, 0 if not
     */
    void handleTelegram(bool valid);

    // These members are initialized in the constructor
    AddrTables* addressTable;    //!< Address table to check, if a telegram should be processed or not.
    Timer& timer;                //!< The timer
    uint32_t rxPin;              //!< The pin for bus receiving
    uint32_t txPin;              //!< The pin for bus sending
    TimerCapture captureChannel; //!< The timer channel that captures the timer value on the bus-in pin
    TimerMatch pwmChannel;       //!< The timer channel for PWM for sending
    TimerMatch timeChannel;      //!< The timer channel for timeouts
    CallbackBus* callBack;       //!< Callback instance to inform bcu of events, e.g. telegram sending finished
    uint16_t ownAddress;         //!< The physical KNX address to use for bus communication
    // End of members initialized in the constructor

    /** The states of the telegram sending/receiving state machine */
    enum State
    {
        INIT,                                        //!< The Lib is initializing, waiting for 50bit time inactivity on the bus
        IDLE,                                        //!< The lib is idle. there was no receiving or sending for at least 50bit times, only cap interrupt enabled, no timeout interrupt
        INIT_RX_FOR_RECEIVING_NEW_TEL,               //!< The Lib received a cap event, and we need to start/init receiving of a new Telegram
        RECV_WAIT_FOR_STARTBIT_OR_TELEND,            //!< The lib is waiting for Startbit (cap interrupt (low bit received) or timeout (end of stop bit: char end)) interrupt enabled
        RECV_BITS_OF_BYTE,                           //!< The lib is collecting all bit of a character
        RECV_WAIT_FOR_ACK_TX_START,                  //!< After Tel is received the lib is waiting for start sending an ACK to remote side.
        WAIT_50BT_FOR_NEXT_RX_OR_PENDING_TX_OR_IDLE, //!< Timeout event: Start sending the telegram (also triggered in sbSendTelegram[]), cap event: start RX of a new tel
        SEND_START_BIT,                              //!< Send a start bit
        SEND_BIT_0,                                  //!< Send the first bit of the current byte
        SEND_BITS_OF_BYTE,                           //!< Send the bits of the current byte
        SEND_END_OF_BYTE,                            //!< Middle of stop bit reached, decide what to do next
        SEND_END_OF_TX,                              //!< Finish sending current byte
        SEND_WAIT_FOR_RX_ACK_WINDOW,                 //!< after sending we wait for the ack receive window to start, only timeout event enabled
        SEND_WAIT_FOR_RX_ACK                         //!< after sending we wait for the ack in the ack receive window, cap event: rx start, timeout: repeat tel
    };

    State state = INIT;                              //!< The state of the lib's telegram sending/receiving
    uint8_t sendRetries = 0;                         //!< The number of repeats when sending a telegram
    uint8_t sendRetriesMax = NACK_RETRY_DEFAULT;     //!< The maximum number of repeats when sending a telegram.
    uint8_t sendBusyRetries = 0;                     //!< The number of busy repeats when sending a telegram
    uint8_t sendBusyRetriesMax = BUSY_RETRY_DEFAULT; //!< The maximum number of busy repeats when sending a telegram.
    uint8_t sendAck = 0;                             //!< Send an acknowledgment or not-acknowledge byte if != 0
    uint16_t nextByteIndex = 0;                      //!< The number of the next byte in the telegram
    uint16_t currentByte = 0;        //!< The current byte that is received/sent, including the parity bit
    uint16_t sendTelegramLen = 0;    //!< The size of the to be sent telegram in bytes (including the checksum).
    uint8_t* sendCurTelegram = nullptr; //!< The telegram that is currently being sent.
    uint8_t* rx_telegram = new uint8_t[TelegramBufferSize]{}; //!< Telegram buffer for the L1/L2 receiving process
    uint16_t bitMask = 0;            //!< Bit mask for the current byte
    uint32_t bitTime = 0;            //!< The bit-time within a byte when receiving
    bool parity = false;             //!< Parity bit of the current byte
    bool valid = false;              //!< True if parity is valid for all bits of the telegram
    uint8_t checksum = 0xff;         //!< Checksum of the telegram: 0 if valid at end of telegram
    uint16_t rx_error = RX_OK;       //!< hold the rx error flags of the rx process of the state machine
    uint16_t tx_error = TX_OK;       //!< hold the tx error flags of the tx process of the state machine
    bool wait_for_ack_from_remote = false;  //!< sending process is requesting an ack from remote side
    bool busy_wait_from_remote = false;     //!< remote side is busy, re-send telegram after 150bit time wait
    bool repeatTelegram = false;            //!< need to repeat last telegram sent
    uint8_t collisions = 0;                 //!< Number of collisions when sending @ref sendCurTelegram
};

#if defined(SBLIB_RP2350_TIMER_STATS)
/**
 * Diagnostics of @ref Bus::pause() (CMake option SBLIB_RP2350_TIMER_STATS, RP2350 only).
 * States are the values of the bus state machine (Bus::State).
 */
struct BusPauseStats
{
    uint32_t calls;         //!< calls of pause() since start
    uint32_t lastUs;        //!< duration of the last pause() call in us
    uint32_t maxUs;         //!< longest pause() call since start in us
    uint32_t maxIterations; //!< loop iterations of the longest call
    uint8_t maxEntryState;  //!< bus state at the start of the longest call
    uint8_t maxStuckState;  //!< bus state after 100 ms in the longest call, 0xff if it took less
};

/**
 * @brief Get the pause() counters since start.
 */
const volatile BusPauseStats& busPauseStats();

/**
 * Get the longest pause() call of the previous run, kept in the watchdog scratch registers
 * over a watchdog reboot (e.g. the restart by the ETS). Clears the record.
 *
 * @return true if a record was present
 */
bool busPauseStatsPreviousRun(BusPauseStats& out);
#endif


/**
 * Create an interrupt handler for the EIB bus access. This macro must be used
 * once for every Bus object that is created. For the default bus object, this is
 * done.
 *
 * @param handler - the name of the interrupt handler, e.g. TIMER16_0_IRQHandler
 * @param busObj - the bus object that shall receive the interrupt.
 */
#define BUS_TIMER_INTERRUPT_HANDLER(handler, busObj) \
    extern "C" void handler() { busObj.timerInterruptHandler(); }

#endif /* SBLIB_KNX_BUS_H_ */
