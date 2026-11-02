/**************************************************************************//**
 * @addtogroup SBLIB_EXAMPLES Selfbus library usage examples
 * @defgroup SBLIB_EXAMPLE_BIM112_TX_STRESSTEST BCU BIM112 Maskversion 0x0701 Example
 * @ingroup SBLIB_EXAMPLES
 * @brief   A simple application which stress tests the KNX-Tx driver.
 * @details The mask version of this example is 0x0701.
 *
 * @{
 *
 * @file   app_main.cpp
 * @author Darthyson <darth@maptrack.de> Copyright (c) 2023
 * @bug No known bugs.
 ******************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <sblib/eibMASK0701.h>
#include <sblib/eib/knx_lpdu.h>
#include <sblib/eib/knx_tpdu.h>
#include <sblib/eib/apci.h>
#include <sblib/io_pin_names.h>

#include <sblib/timeout.h>
#include <string.h>

// The macros in #ifndef can be set by the build, e.g. by CMakeLists.txt for the RP2350 test board.
#ifndef KNX_PHYS_ADDRESS
// #define KNX_PHYS_ADDRESS    (0xAFFE)    //!< own physical KNX address (10.15.254)
#define KNX_PHYS_ADDRESS    (0x0002)    //!< own physical KNX address (0.0.2)
#endif
#define MANUFACTURER        (0x83)      //!< Manufacturer ID (MDT)
#define DEVICETYPE          (0x0030)    //!< Device Type (Binary input 16f)
#define APPVERSION          (0x20)      //!< Application Version

#define TEST_FILLBYTE       (0x00)      //!< Value the payload bytes should be filled with
#ifndef TEST_DELAY_MS
#define TEST_DELAY_MS       (1)         //!< Delay in milliseconds between two test telegrams
#endif
#define TEST_TELEGRAM_SIZE  (24)        //!< Length of the test telegram, including trailing 0x00 byte and checksum byte (sblib supports up to 24 Bytes)
#ifndef TEST_MAX_SEND_RETRIES
#define TEST_MAX_SEND_RETRIES (100)     //!< maximum retries in case of no ACK
#endif
#ifndef TEST_STARTUP_DELAY_MS
#define TEST_STARTUP_DELAY_MS (1000)    //!< Delay in milliseconds before the test starts
#endif

// Group telegram mode, enabled by defining TEST_GROUP_ADDRESS_FIRST (RP2350 test board on a real bus):
// A_GroupValue_Write (1 bit, on/off) to the group addresses TEST_GROUP_ADDRESS_FIRST and the following
// TEST_GROUP_ADDRESS_COUNT - 1 addresses in turn. The delay TEST_DELAY_MS starts when the bus has
// finished a telegram (including repetitions), sending stops after TEST_DURATION_MS.
// A line with the count of telegrams handed to the bus is printed every TEST_REPORT_MS (stdout).
#if defined(TEST_GROUP_ADDRESS_FIRST)
#   include <cstdio>
#   if !defined(TEST_GROUP_ADDRESS_COUNT) || !defined(TEST_DURATION_MS) || !defined(TEST_REPORT_MS)
#       error "TEST_GROUP_ADDRESS_FIRST needs TEST_GROUP_ADDRESS_COUNT, TEST_DURATION_MS and TEST_REPORT_MS"
#   endif
#endif


const unsigned char hardwareVersion[6] = {0, 0, 0, 0, 0x00, 0x1E}; //!< The hardware identification number

MASK0701 bcu;

Timeout sendTimeout;

APP_VERSION("SBSTRES", "0", "01");

uint8_t testTelegram[TEST_TELEGRAM_SIZE];
unsigned int testTelegramSize = sizeof(testTelegram)/sizeof(testTelegram[0]) - 2; // last 2 bytes reserved for KNX checksum and trailing 0x00 byte

#if defined(TEST_GROUP_ADDRESS_FIRST)
unsigned int groupOffset = 0; //!< Offset of the next group address to TEST_GROUP_ADDRESS_FIRST
uint8_t switchValue = 1;      //!< Value of the next group telegram
unsigned int sentCount = 0;   //!< Telegrams handed to the bus
unsigned int testStartMs = 0; //!< Time of the first telegram in milliseconds
bool testRunning = false;     //!< True after the first telegram
bool testFinished = false;    //!< True after TEST_DURATION_MS
Timeout reportTimeout;

void initTestTelegram()
{
    testTelegramSize = 8; // control, source, destination, DAF/hop count/length, TPCI/APCI, APCI/data
    memset(testTelegram, TEST_FILLBYTE, sizeof(testTelegram));
    initLpdu(testTelegram, PRIORITY_LOW, false, FRAME_STANDARD); // init control byte
    testTelegram[5] = 0xE1;                                      // group address, hop count 6, length of payload 1
    setApciCommand(testTelegram, APCI_GROUP_VALUE_WRITE_PDU, 0);
}

void printReport()
{
    printf("STRESS t=%u sent=%u done=%u\n", static_cast<unsigned int>(millis() - testStartMs), sentCount,
           static_cast<unsigned int>(testFinished));
}

void sendTestTelegram()
{
    initLpdu(testTelegram, PRIORITY_LOW, false, FRAME_STANDARD); // init control byte
    setDestinationAddress(testTelegram, TEST_GROUP_ADDRESS_FIRST + groupOffset);
    setApciCommand(testTelegram, APCI_GROUP_VALUE_WRITE_PDU, switchValue);
    bcu.bus->sendTelegram(testTelegram, testTelegramSize);
    sentCount++;
    groupOffset++;
    if (groupOffset >= TEST_GROUP_ADDRESS_COUNT)
    {
        groupOffset = 0;
        switchValue ^= 1;
    }
}
#else
void initTestTelegram()
{
    int count = (testTelegramSize - 7);
    int memoryAddress = 0;
    memset(testTelegram, TEST_FILLBYTE, testTelegramSize);
    // send a APCI_MEMORY_RESPONSE_PDU response
    initLpdu(testTelegram, PRIORITY_SYSTEM, false, FRAME_STANDARD); // init control byte
    setDestinationAddress(testTelegram, KNX_PHYS_ADDRESS - 1); // set destination address
    testTelegram[5] = 0x60 + count; // hopcount + length of payload
    setApciCommand(testTelegram, APCI_MEMORY_RESPONSE_PDU, (count - 2));
    setSequenceNumber(testTelegram, 0);
    testTelegram[8] = memoryAddress >> 8;
    testTelegram[9] = memoryAddress;
}

void sendTestTelegram()
{
    initLpdu(testTelegram, PRIORITY_SYSTEM, false, FRAME_STANDARD); // init control byte
    bcu.bus->sendTelegram(testTelegram, testTelegramSize);
    sendTimeout.start(TEST_DELAY_MS);
}
#endif

/**
 * @brief This function is called by the Selfbus's library main
 *        when the processor is started or reset.
 *
 * @note  You must implement this function in your code.
 */
BcuBase* setup()
{
    bcu.begin(MANUFACTURER, DEVICETYPE, APPVERSION);
    bcu.setOwnAddress(KNX_PHYS_ADDRESS);
    bcu.bus->maxSendRetries(TEST_MAX_SEND_RETRIES);
    bcu.setHardwareType(hardwareVersion, sizeof(hardwareVersion));
    // prepare test telegram
    initTestTelegram();
    sendTimeout.start(TEST_STARTUP_DELAY_MS);
    return (&bcu);
}

/**
 * @brief The main processing loop while a KNX-application is loaded.
 *
 */
#if defined(TEST_GROUP_ADDRESS_FIRST)
void loop(void)
{
    if (bcu.bus->sendingFrame())
    {
        sendTimeout.start(TEST_DELAY_MS); // the delay starts when the telegram is finished
    }
    else if (!testFinished && sendTimeout.expired())
    {
        if (!testRunning)
        {
            testRunning = true;
            testStartMs = millis();
            reportTimeout.start(TEST_REPORT_MS);
            printf("STRESS start own=%04X groups=%04X+%u delay=%u duration=%u\n", KNX_PHYS_ADDRESS,
                   TEST_GROUP_ADDRESS_FIRST, TEST_GROUP_ADDRESS_COUNT, TEST_DELAY_MS, TEST_DURATION_MS);
        }
        if ((millis() - testStartMs) >= TEST_DURATION_MS)
        {
            testFinished = true;
            printReport();
        }
        else
        {
            sendTestTelegram();
        }
    }

    if (testRunning && !testFinished && reportTimeout.expired())
    {
        printReport();
        reportTimeout.start(TEST_REPORT_MS);
    }
    waitForInterrupt();
}
#else
void loop(void)
{
    if (!bcu.bus->sendingFrame() && ((TEST_DELAY_MS <= 0) || (sendTimeout.expired())))
    {
        sendTestTelegram();
    }
    else
    {
        waitForInterrupt();
    }
}
#endif

/**
 * @brief The processing loop while no KNX-application is loaded.
 */
void loop_noapp(void)
{
    loop();
}

/** @}*/
