/**************************************************************************//**
 * @addtogroup SBLIB_MAIN_GROUP Selfbus library
 * @defgroup SBLIB_UNIT_TEST Unit tests
 * @ingroup SBLIB_MAIN_GROUP
 * @brief   Catch unit test for APCI processing
 * @details
 *
 *
 * @{
 *
 * @file   test_prot_apci.cpp
 * @author Darthyson <darth@maptrack.de> Copyright (c) 2022
 * @bug No known bugs.
 ******************************************************************************/

/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 3 as
 published by the Free Software Foundation.
 ---------------------------------------------------------------------------*/

#include <sblib/eib/apci.h>
#include <sblib/internal/bootloader_commands.h>
#include <protocol.h>
#include "prot_systemb_telegrams.h"

#include <catch.hpp> // If possible, include catch.hpp as last header


#define MANUFACTURER (0x0004)
#define DEVICE       (0x2060)
#define VERSION      (0x01)

#define OWN_KNX_ADDRESS (0xA000) // own address 10.0.0

static void tc_setup(Telegram* tel, uint16_t telCount)
{
    // bcuUnderTest->setTL4State(TLayer4::CLOSED); // to "reset" connection for next test
    bcuUnderTest->setOwnAddress(OWN_KNX_ADDRESS);
    telegramPreparation(bcuUnderTest, tel, telCount);
}

static Telegram apciAdcReadPduTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_ADC_READ_PDU
    {TEL_RX, 9, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x62, 0x41, 0x80, 0x89}},
    // 3. Check T_ACK, loop() once so APCI_ADC_RESPONSE_PDU will be send
    {TEL_TX, 7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_ADC_RESPONSE_PDU
    {TEL_TX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x64, 0x41, 0xC0, 0x89, 0x00, 0x00}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciMemoryReadTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_MEMORY_READ_PDU count = 2, address = (BCU1: 0x105, BIM112: 0x3F05) (UserEeprom.deviceTypeH and UserEeprom.deviceTypeL)
    {TEL_RX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, 0x42, 0x02, 0x01, 0x05}},
    // 3. Check T_ACK, loop() once so APCI_MEMORY_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_MEMORY_RESPONSE_PDU count = 2, address = (BCU1: 0x105, BIM112: 0x3F05), value = DEVICE = 0x2060
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x42, 0x42, 0x01, 0x05, 0x20, 0x60}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciMemoryWriteTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_MEMORY_WRITE_PDU count = 2, address = (BCU1: 0x105, BIM112: 0x3F05), value = 0xCDEF
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x66, 0x42, 0x82, 0x01, 0x05, 0xCD, 0xEF}},
    // 3. Check T_ACK, loop() once
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_MEMORY_READ_PDU count = 2, address = (BCU1: 0x105, BIM112: 0x3F05) (UserEeprom.deviceTypeH and UserEeprom.deviceTypeL)
    {TEL_RX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, 0x46, 0x02, 0x01, 0x05}},
    // 5. Check T_ACK, loop() once so APCI_MEMORY_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
    // 6. APCI_MEMORY_RESPONSE_PDU count = 2, address = (BCU1: 0x105, BIM112: 0x3F05), value = 0xCDEF
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x42, 0x42, 0x01, 0x05, 0xCD, 0xEF}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciDeviceDescriptorReadTelegrams[] =
{
    // 1. APCI_DEVICEDESCRIPTOR_READ_PDU
    {TEL_RX,  8, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x03, 0x00}},
    // 2. DeviceDescriptorResponse
    {TEL_TX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x63, 0x03, 0x40, dummyMaskVersionHigh, dummyMaskVersionLow}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciDeviceDescriptorReadConnectedTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_DEVICEDESCRIPTOR_READ_PDU
    {TEL_RX,  8, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0x00}},
    // 3. Check T_ACK, loop() once so APCI_DEVICEDESCRIPTOR_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. DeviceDescriptorResponse
    {TEL_TX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x63, 0x43, 0x40, dummyMaskVersionHigh, dummyMaskVersionLow}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciBasicRestartTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_BASIC_RESTART_PDU
    {TEL_RX,  8, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0x80}},
    // 3. Check T_ACK
    {TEL_TX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. T_DISCONNECT_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x81}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciMasterResetTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_MASTER_RESET_PDU eraseCode = BOOTLOADER_MAGIC_ERASE == T_MASTERRESET_FACTORY_WO_IA,
    //                          channelNumber = BOOTLOADER_MAGIC_CHANNEL = 255
    {TEL_RX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0x81,  T_MASTERRESET_FACTORY_WO_IA, 255}},
    // 3. Check T_ACK, loop() once so APCI_MASTER_RESET_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_MASTER_RESET_RESPONSE_PDU => T_RESTART_NO_ERROR with 1 second, loop() once so T_DISCONNECT_PDU will be sent
    {TEL_TX, 11, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x64, 0x43, 0xA1, 0x00, 0x00, 0x01}},
    // 5. T_DISCONNECT_PDU
    {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x81}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciAuthorizeRequestTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_AUTHORIZE_REQUEST_PDU
    {TEL_RX, 8, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD1}},
    // 3. Check T_ACK, loop() once so APCI_AUTHORIZE_RESPONSE_PDU will be send
    {TEL_TX, 7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_AUTHORIZE_RESPONSE_PDU
    {TEL_TX, 9, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x62, 0x43, 0xD2, 0x00}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciPropertyDescriptionReadTelegrams_BCU1[] =
{
    // special BCU 1 version, which should not respond
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_PROPERTY_DESCRIPTION_READ_PDU, objIdx = 0x00 (deviceObjectProps) , propId = 0x0C (PID_MANUFACTURER_ID), index = 0x00
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD8, 0x00, 0x0C, 0x00}},
    // 3. Check T_ACK, loop() once so APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. Check for empty TX-Response
    {CHECK_TX_BUFFER, 0, 0, 0, NULL, {}},
    // 5.
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciPropertyDescriptionReadTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_PROPERTY_DESCRIPTION_READ_PDU, objIdx = 0x00 (deviceObjectProps) , propId = 0x0C (PID_MANUFACTURER_ID), index = 0x00
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD8, 0x00, 0x0C, 0x00}},
    // 3. Check T_ACK, loop() once so APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU
    {TEL_TX, 15, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x68, 0x43, 0xD9, 0x00, 0x0C, 0x00, 0x12, 0x00, 0x01, 0x50}},
    // 5. T_ACK for APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC2}},
    // 6. APCI_PROPERTY_DESCRIPTION_READ_PDU, objIdx = 0x03 (appObjectProps) , propId = 0x0D (PID_PROG_VERSION), index = 0x00
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x47, 0xD8, 0x03, 0x0D, 0x00}},
    // 7. Check T_ACK, loop() once so APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
    // 8. APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU
    {TEL_TX, 15, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x68, 0x47, 0xD9, 0x03, 0x0D, 0x00, 0x15, 0x00, 0x01, 0x50}},
    // 9. T_ACK for APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC6}},
    // 10.
    {END, 0, 0, 0, nullptr, {0}}
};

// SYSTEM B has fixed interface object indices, index 3 is the group object table without PID_PROGRAM_VERSION,
// the application program 1 object is at index 4
static Telegram apciPropertyDescriptionReadTelegrams_SystemB[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_PROPERTY_DESCRIPTION_READ_PDU, objIdx = 0x00 (device) , propId = 0x0C (PID_MANUFACTURER_ID), index = 0x00
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD8, 0x00, 0x0C, 0x00}},
    // 3. Check T_ACK, loop() once so APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU
    {TEL_TX, 15, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x68, 0x43, 0xD9, 0x00, 0x0C, 0x00, 0x12, 0x00, 0x01, 0x50}},
    // 5. T_ACK for APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC2}},
    // 6. APCI_PROPERTY_DESCRIPTION_READ_PDU, objIdx = 0x04 (application program 1) , propId = 0x0D (PID_PROG_VERSION), index = 0x00
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x47, 0xD8, 0x04, 0x0D, 0x00}},
    // 7. Check T_ACK, loop() once so APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
    // 8. APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU, writable, one element
    {TEL_TX, 15, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x68, 0x47, 0xD9, 0x04, 0x0D, 0x00, 0x95, 0x00, 0x01, 0xF1}},
    // 9. T_ACK for APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC6}},
    // 10.
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciPropertyValueReadTelegrams_BCU1[] =
{   // special BCU 1 version, which should not respond
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_PROPERTY_VALUE_READ_PDU, objIdx = 0x00 (deviceObjectProps) , propId = 0x0C (PID_MANUFACTURER_ID), count = 1, start = 1
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD5, 0x00, 0x0C, 0x10, 0x01}},
    // 3. Check T_ACK, loop() once so APCI_PROPERTY_VALUE_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. Check for empty TX-Response
    {CHECK_TX_BUFFER, 0, 0, 0, NULL, {}},
    // 5.
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciPropertyValueReadTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_PROPERTY_VALUE_READ_PDU, objIdx = 0x00 (deviceObjectProps) , propId = 0x0C (PID_MANUFACTURER_ID), count = 1, start = 1
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD5, 0x00, 0x0C, 0x10, 0x01}},
    // 3. Check T_ACK, loop() once so APCI_PROPERTY_VALUE_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_PROPERTY_VALUE_RESPONSE_PDU (Manufacturer)
    {TEL_TX, 14, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x67, 0x43, 0xD6, 0x00, 0x0C, 0x10, 0x01, (MANUFACTURER >> 8), (MANUFACTURER & 0xFF)}},
    // 5. T_ACK for APCI_PROPERTY_VALUE_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC2}},

    // 6. APCI_PROPERTY_VALUE_READ_PDU, objIdx = 0x03 (appObjectProps) , propId = 0x0D (PID_PROG_VERSION), count = 1, start = 1
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x47, 0xD5, 0x03, 0x0D, 0x10, 0x01}},
    // 7. Check T_ACK, loop() once so APCI_PROPERTY_VALUE_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
    // 8. APCI_PROPERTY_VALUE_RESPONSE_PDU (Manufacturer, Device, Version)
    {TEL_TX, 17, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6A, 0x47, 0xD6, 0x03, 0x0D, 0x10, 0x01, (MANUFACTURER >> 8), (MANUFACTURER & 0xFF), (DEVICE >> 8), (DEVICE & 0xFF), VERSION}},
    // 9. T_ACK for APCI_PROPERTY_VALUE_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC6}},
    // 10.
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciPropertyValueReadTelegrams_SystemB[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_PROPERTY_VALUE_READ_PDU, objIdx = 0x00 (device) , propId = 0x0C (PID_MANUFACTURER_ID), count = 1, start = 1
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD5, 0x00, 0x0C, 0x10, 0x01}},
    // 3. Check T_ACK, loop() once so APCI_PROPERTY_VALUE_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_PROPERTY_VALUE_RESPONSE_PDU (Manufacturer)
    {TEL_TX, 14, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x67, 0x43, 0xD6, 0x00, 0x0C, 0x10, 0x01, (MANUFACTURER >> 8), (MANUFACTURER & 0xFF)}},
    // 5. T_ACK for APCI_PROPERTY_VALUE_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC2}},
    // 6.
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciPropertyValueWriteTelegrams_BCU1[] =
{   // special BCU 1 version, which should not respond
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_PROPERTY_VALUE_WRITE_PDU, objIdx = 0x00 (deviceObjectProps) , propId = 0x4E (PID_HARDWARE_TYPE), count = 1, start = 1, length 6
    {TEL_RX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD7, 0x00, 0x4E, 0x10, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
    // 3. Check T_ACK, loop() once so APCI_PROPERTY_VALUE_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. Check for empty TX-Response
    {CHECK_TX_BUFFER, 0, 0, 0, NULL, {}},
    // 5.
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciPropertyValueWriteTelegrams[] =
{
    // 1. T_CONNECT_PDU (0x80) from sourceAddr=10.0.1 to destAddr=10.0.0
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // 2. APCI_PROPERTY_VALUE_WRITE_PDU, objIdx = 0x00 (deviceObjectProps) , propId = 0x4E (PID_HARDWARE_TYPE), count = 1, start = 1, length 6
    {TEL_RX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD7, 0x00, 0x4E, 0x10, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
    // 3. Check T_ACK, loop() once so APCI_PROPERTY_VALUE_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // 4. APCI_PROPERTY_VALUE_RESPONSE_PDU objIdx = 0x00 (deviceObjectProps) , propId = 0x4E (PID_HARDWARE_TYPE)
    {TEL_TX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6B, 0x43, 0xD6, 0x00, 0x4E, 0x10, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
    // 5. T_ACK for APCI_PROPERTY_VALUE_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC2}},
    // 6. APCI_PROPERTY_VALUE_WRITE_PDU, objIdx = 0x03 (appObjectProps) , propId = 0x1B (PID_MCB_TABLE), count = 1, start = 1, length 8
    {TEL_RX, 20, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x47, 0xD7, 0x03, 0x1B, 0x10, 0x01, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}},
    // 7. Check T_ACK, loop() once so APCI_PROPERTY_VALUE_RESPONSE_PDU will be send
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
    // 8. APCI_PROPERTY_VALUE_RESPONSE_PDU objIdx = 0x03 (appObjectProps) , propId = 0x1B (PID_MCB_TABLE)
    {TEL_TX, 20, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6D, 0x47, 0xD6, 0x03, 0x1B, 0x10, 0x01, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}},
    // 9. T_ACK for APCI_PROPERTY_VALUE_RESPONSE_PDU
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC6}},
    // 10.
    {END, 0, 0, 0, nullptr, {0}}
};


// load events: 00h No Operation, 01h Start Loading, 02h Load Completed, 03h Additional Load Controls, 04h Unload
// load states: 0 Unloaded, 1 Loaded, 2 Loading, 3 Error
// ways to reach a state, seq is the next sequence number, Error is reached by Additional Load Controls in Loaded
#define TEL_REACH_UNLOADED(seq, obj) TEL_LOAD_EVENT(seq, obj, 0x04, 0x00)
#define TEL_REACH_LOADING(seq, obj) TEL_REACH_UNLOADED(seq, obj), TEL_LOAD_EVENT(seq + 1, obj, 0x01, 0x02)
#define TEL_REACH_LOADED(seq, obj) TEL_REACH_LOADING(seq, obj), TEL_LOAD_EVENT(seq + 2, obj, 0x02, 0x01)
#define TEL_REACH_ERROR(seq, obj) TEL_REACH_LOADED(seq, obj), TEL_LOAD_ADDITIONAL(seq + 3, obj, 0x03)
#define TEL_REACH_UNLOADED_N 1
#define TEL_REACH_LOADING_N 2
#define TEL_REACH_LOADED_N 3
#define TEL_REACH_ERROR_N 4

// one transition: connect, reach the state, send the event, the response and the read back value hold the new state
#define TEL_LOAD_TRANSITION(obj, state, event, expected)     TEL_CONNECT,     TEL_REACH_##state(0, obj),     TEL_LOAD_EVENT(TEL_REACH_##state##_N, obj, event, expected),     TEL_VALUE_READ(TEL_REACH_##state##_N + 1, obj, 0x05),     TEL_VALUE_RESPONSE_1(TEL_REACH_##state##_N + 1, obj, 0x05, expected), TEL_DISCONNECT

// Additional Load Controls with a segment (data relative allocation) instead of a plain event
#define TEL_LOAD_TRANSITION_SEGMENT(obj, state, expected)     TEL_CONNECT,     TEL_REACH_##state(0, obj),     TEL_LOAD_ADDITIONAL(TEL_REACH_##state##_N, obj, expected),     TEL_VALUE_READ(TEL_REACH_##state##_N + 1, obj, 0x05),     TEL_VALUE_RESPONSE_1(TEL_REACH_##state##_N + 1, obj, 0x05, expected), TEL_DISCONNECT

// too short data (5 octets instead of 10) leave the state unchanged
#define TEL_LOAD_SHORT(obj, state, event, expected)     TEL_CONNECT,     TEL_REACH_##state(0, obj),     TEL_VALUE_WRITE_5(TEL_REACH_##state##_N, obj, 0x05, event, 0x00, 0x00, 0x00, 0x00),     TEL_VALUE_RESPONSE_1(TEL_REACH_##state##_N, obj, 0x05, expected), TEL_DISCONNECT

// Table 94: all events from all states, an unknown event (09h) and too short data
#define TEL_LOAD_STATE_SEQUENCE(obj)     TEL_LOAD_TRANSITION(obj, UNLOADED, 0x00, 0x00), TEL_LOAD_TRANSITION(obj, UNLOADED, 0x01, 0x02),     TEL_LOAD_TRANSITION(obj, UNLOADED, 0x02, 0x00), TEL_LOAD_TRANSITION_SEGMENT(obj, UNLOADED, 0x00),     TEL_LOAD_TRANSITION(obj, UNLOADED, 0x04, 0x00), TEL_LOAD_TRANSITION(obj, UNLOADED, 0x09, 0x00),     TEL_LOAD_TRANSITION(obj, LOADED, 0x00, 0x01), TEL_LOAD_TRANSITION(obj, LOADED, 0x01, 0x02),     TEL_LOAD_TRANSITION(obj, LOADED, 0x02, 0x01), TEL_LOAD_TRANSITION_SEGMENT(obj, LOADED, 0x03),     TEL_LOAD_TRANSITION(obj, LOADED, 0x04, 0x00), TEL_LOAD_TRANSITION(obj, LOADED, 0x09, 0x01),     TEL_LOAD_TRANSITION(obj, LOADING, 0x00, 0x02), TEL_LOAD_TRANSITION(obj, LOADING, 0x01, 0x02),     TEL_LOAD_TRANSITION(obj, LOADING, 0x02, 0x01), TEL_LOAD_TRANSITION_SEGMENT(obj, LOADING, 0x02),     TEL_LOAD_TRANSITION(obj, LOADING, 0x04, 0x00), TEL_LOAD_TRANSITION(obj, LOADING, 0x09, 0x02),     TEL_LOAD_TRANSITION(obj, ERROR, 0x00, 0x03), TEL_LOAD_TRANSITION(obj, ERROR, 0x01, 0x03),     TEL_LOAD_TRANSITION(obj, ERROR, 0x02, 0x03), TEL_LOAD_TRANSITION_SEGMENT(obj, ERROR, 0x03),     TEL_LOAD_TRANSITION(obj, ERROR, 0x04, 0x00), TEL_LOAD_TRANSITION(obj, ERROR, 0x09, 0x03),     TEL_LOAD_SHORT(obj, LOADING, 0x04, 0x02), TEL_LOAD_SHORT(obj, UNLOADED, 0x01, 0x00),     TEL_LOAD_SHORT(obj, ERROR, 0x04, 0x03)

static void setSystemBTableAddresses([[maybe_unused]] void* state, [[maybe_unused]] unsigned int var)
{
    UserEepromBCU2* eeprom = static_cast<UserEepromBCU2*>(bcuUnderTest->userEeprom);
    eeprom->addrTabAddr() = 0x3456;
    eeprom->assocTabAddr() = 0x3458;
    eeprom->commsTabAddr() = 0x3500;
    eeprom->eibObjAddr() = 0x3600;
    eeprom->commsSeg0Addr() = 0x3700;
}

// objects 0..5 report their object type, object 6 does not exist
static Telegram apciSystemBObjectTypeTelegrams[] =
{
    TEL_CONNECT,
    TEL_VALUE_READ(0, 0, 0x01), TEL_VALUE_RESPONSE_2(0, 0, 0x01, 0x00, 0x00),
    TEL_VALUE_READ(1, 1, 0x01), TEL_VALUE_RESPONSE_2(1, 1, 0x01, 0x00, 0x01),
    TEL_VALUE_READ(2, 2, 0x01), TEL_VALUE_RESPONSE_2(2, 2, 0x01, 0x00, 0x02),
    TEL_VALUE_READ(3, 3, 0x01), TEL_VALUE_RESPONSE_2(3, 3, 0x01, 0x00, 0x09),
    TEL_VALUE_READ(4, 4, 0x01), TEL_VALUE_RESPONSE_2(4, 4, 0x01, 0x00, 0x03),
    TEL_VALUE_READ(5, 5, 0x01), TEL_VALUE_RESPONSE_2(5, 5, 0x01, 0x00, 0x04),
    // object 6 does not exist: description with 0 elements
    TEL_DESC_READ(6, 6, 0x01), TEL_DESC_RESPONSE(6, 6, 0x01, 0x00, 0x00, 0x00),
    {END, 0, 0, 0, nullptr, {0}}
};

// PID_TABLE_REFERENCE is a 4 byte value, the upper two bytes are 0
static Telegram apciSystemBTableReferenceTelegrams[] =
{
    {TEL_RX, 7, 0, 0, setSystemBTableAddresses, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    TEL_DESC_READ(0, 1, 0x07), TEL_DESC_RESPONSE(0, 1, 0x07, 0x09, 0x01, 0x50),
    TEL_VALUE_READ(1, 1, 0x07), TEL_VALUE_RESPONSE_4(1, 1, 0x07, 0x00, 0x00, 0x34, 0x56),
    TEL_VALUE_READ(2, 2, 0x07), TEL_VALUE_RESPONSE_4(2, 2, 0x07, 0x00, 0x00, 0x34, 0x58),
    TEL_VALUE_READ(3, 3, 0x07), TEL_VALUE_RESPONSE_4(3, 3, 0x07, 0x00, 0x00, 0x35, 0x00),
    TEL_VALUE_READ(4, 4, 0x07), TEL_VALUE_RESPONSE_4(4, 4, 0x07, 0x00, 0x00, 0x36, 0x00),
    TEL_VALUE_READ(5, 5, 0x07), TEL_VALUE_RESPONSE_4(5, 5, 0x07, 0x00, 0x00, 0x37, 0x00),
    {END, 0, 0, 0, nullptr, {0}}
};

// PID_PROGRAM_VERSION and PID_PEI_TYPE of the application programs 1 and 2 can be written and read back,
// the load state of object 5 is "unloaded" after a reset
static Telegram apciSystemBApplicationTelegrams[] =
{
    TEL_CONNECT,
    TEL_VALUE_READ(0, 5, 0x05), TEL_VALUE_RESPONSE_1(0, 5, 0x05, 0x00),
    TEL_VALUE_WRITE_5(1, 4, 0x0D, 0x00, 0x04, 0x20, 0x60, 0x01),
    TEL_VALUE_RESPONSE_5(1, 4, 0x0D, 0x00, 0x04, 0x20, 0x60, 0x01),
    TEL_VALUE_WRITE_5(2, 5, 0x0D, 0x00, 0x04, 0x20, 0x61, 0x02),
    TEL_VALUE_RESPONSE_5(2, 5, 0x0D, 0x00, 0x04, 0x20, 0x61, 0x02),
    TEL_VALUE_READ(3, 4, 0x0D), TEL_VALUE_RESPONSE_5(3, 4, 0x0D, 0x00, 0x04, 0x20, 0x60, 0x01),
    TEL_VALUE_READ(4, 5, 0x0D), TEL_VALUE_RESPONSE_5(4, 5, 0x0D, 0x00, 0x04, 0x20, 0x61, 0x02),
    TEL_VALUE_WRITE_1(5, 4, 0x10, 0x01), TEL_VALUE_RESPONSE_1(5, 4, 0x10, 0x01),
    TEL_VALUE_WRITE_1(6, 5, 0x10, 0x02), TEL_VALUE_RESPONSE_1(6, 5, 0x10, 0x02),
    TEL_VALUE_READ(7, 4, 0x10), TEL_VALUE_RESPONSE_1(7, 4, 0x10, 0x01),
    TEL_VALUE_READ(8, 5, 0x10), TEL_VALUE_RESPONSE_1(8, 5, 0x10, 0x02),
    {END, 0, 0, 0, nullptr, {0}}
};

static Test_Case testCaseApciAdcReadPdu =
{
    "APCI_ADC_READ_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciAdcReadPduTelegrams
};

static Test_Case testCaseApciMemoryReadPdu =
{
    "APCI_MEMORY_READ_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciMemoryReadTelegrams
};

static Test_Case testCaseApciMemoryWritePdu =
{
    "APCI_MEMORY_WRITE_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciMemoryWriteTelegrams
};

static Test_Case testCaseApciDeviceDescriptorReadPdu =
{
    "APCI_DEVICEDESCRIPTOR_READ_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciDeviceDescriptorReadTelegrams
};

static Test_Case testCaseApciDeviceDescriptorReadConnectedPdu =
{
    "APCI_DEVICEDESCRIPTOR_READ_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciDeviceDescriptorReadConnectedTelegrams
};

static Test_Case testCaseApciBasicRestartPdu =
{
    "APCI_BASIC_RESTART_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciBasicRestartTelegrams
};

static Test_Case testCaseApciMasterResetPdu =
{
    "APCI_MASTER_RESET_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciMasterResetTelegrams
};

static Test_Case testCaseApciAuthorizeRequestPdu =
{
    "APCI_AUTHORIZE_REQUEST_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciAuthorizeRequestTelegrams
};

static Test_Case testCaseApciPropertyDescriptionReadPdu =
{
    "APCI_PROPERTY_DESCRIPTION_READ_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciPropertyDescriptionReadTelegrams
};

static Test_Case testCaseApciPropertyValueReadPdu =
{
    "APCI_PROPERTY_VALUE_READ_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciPropertyValueReadTelegrams
};

static Test_Case testCaseApciPropertyValueWritePdu =
{
    "APCI_PROPERTY_VALUE_WRITE_PDU",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciPropertyValueWriteTelegrams
};

static void tc_setupVerifyMode(Telegram* tel, uint16_t telCount)
{
    tc_setup(tel, telCount);
    bcuUnderTest->userRam->deviceControl() |= DEVCTRL_MEM_AUTO_RESPONSE;
}

// APCI_MEMORY_READ_PDU with number > 12 does not fit into an APDU: response with number 0 and the requested address
#define MEMORY_READ_TOO_MANY(NUMBER) \
static Telegram apciMemoryReadNumber##NUMBER##Telegrams[] = \
{ \
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}}, \
    {TEL_RX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, 0x42, NUMBER, 0x01, 0x05}}, \
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}}, \
    {TEL_TX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x63, 0x42, 0x40, 0x01, 0x05}}, \
    {END, 0, 0, 0, nullptr, {0}} \
}

#if SBLIB_MAX_APDU_LENGTH == 15
MEMORY_READ_TOO_MANY(0x0D);
MEMORY_READ_TOO_MANY(0x0E);
MEMORY_READ_TOO_MANY(0x0F);
#endif

static Telegram apciMemoryWriteTooShortTelegrams[] =
{
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // number = 2, but only one data octet: ignored, no response
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, 0x42, 0x82, 0x01, 0x05, 0xCD}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // the memory is unchanged
    {TEL_RX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, 0x46, 0x02, 0x01, 0x05}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x42, 0x42, 0x01, 0x05, 0x20, 0x60}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciMemoryWriteTooLongTelegrams[] =
{
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // number = 1, but two data octets: ignored, no response
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x65, 0x42, 0x81, 0x01, 0x05, 0xCD, 0xEF}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    {TEL_RX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, 0x46, 0x02, 0x01, 0x05}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x42, 0x42, 0x01, 0x05, 0x20, 0x60}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciMemoryWriteVerifyModeOkTelegrams[] =
{
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x66, 0x42, 0x82, 0x01, 0x05, 0xCD, 0xEF}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // successful write: response with the written data
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x42, 0x42, 0x01, 0x05, 0xCD, 0xEF}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciMemoryWriteVerifyModeFailedTelegrams[] =
{
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // address 0xFFF0 is not reachable
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, 0x42, 0x81, 0xFF, 0xF0, 0xAA}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    // failed write: response with number 0
    {TEL_TX, 10, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x63, 0x42, 0x40, 0xFF, 0xF0}},
    {END, 0, 0, 0, nullptr, {0}}
};

static Telegram apciMemoryWriteVerifyModeInactiveFailedTelegrams[] =
{
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x64, 0x42, 0x81, 0xFF, 0xF0, 0xAA}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    {END, 0, 0, 0, nullptr, {0}}
};

#define MEMORY_TEST_CASE(NAME, SETUP, TELEGRAMS) \
static Test_Case NAME = {#NAME, MANUFACTURER, DEVICE, VERSION, 0, NULL, SETUP, NULL, NULL, NULL, TELEGRAMS}

#if SBLIB_MAX_APDU_LENGTH == 15
MEMORY_TEST_CASE(testCaseMemoryReadNumber13, tc_setup, apciMemoryReadNumber0x0DTelegrams);
MEMORY_TEST_CASE(testCaseMemoryReadNumber14, tc_setup, apciMemoryReadNumber0x0ETelegrams);
MEMORY_TEST_CASE(testCaseMemoryReadNumber15, tc_setup, apciMemoryReadNumber0x0FTelegrams);
#endif
MEMORY_TEST_CASE(testCaseMemoryWriteTooShort, tc_setup, apciMemoryWriteTooShortTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteTooLong, tc_setup, apciMemoryWriteTooLongTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteVerifyModeOk, tc_setupVerifyMode, apciMemoryWriteVerifyModeOkTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteVerifyModeFailed, tc_setupVerifyMode, apciMemoryWriteVerifyModeFailedTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteVerifyModeInactiveFailed, tc_setup, apciMemoryWriteVerifyModeInactiveFailedTelegrams);

// load state machine of the objects 1 to 5 (Table 94 of KNX 03_05_01 4.23.2.3.3), each transition on its own connection
static Telegram apciSystemBLoadStateTelegrams[] =
{
    TEL_LOAD_STATE_SEQUENCE(1),
    TEL_LOAD_STATE_SEQUENCE(2),
    TEL_LOAD_STATE_SEQUENCE(3),
    TEL_LOAD_STATE_SEQUENCE(4),
    TEL_LOAD_STATE_SEQUENCE(5),
    {END, 0, 0, 0, nullptr, {0}}
};

// element count checks (PID_MCB_TABLE has 1 element of 8 octets, PID_PROGRAM_VERSION 1 of 5): element 0 holds the number of elements,
// requests beyond the last element and writing element 0 are rejected with a response without data
static Telegram apciSystemBElementCountTelegrams[] =
{
    TEL_CONNECT,
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(0), 0xD5, 0x04, 0x1B, 0x20, 0x01}},
    TEL_ACK_TX(0),
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, TPCI_DATA(0), 0xD6, 0x04, 0x1B, 0x00, 0x01}},
    TEL_ACK_RX(0),
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(1), 0xD5, 0x04, 0x1B, 0x10, 0x00}},
    TEL_ACK_TX(1),
    {TEL_TX, 14, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x67, TPCI_DATA(1), 0xD6, 0x04, 0x1B, 0x10, 0x00, 0x00, 0x01}},
    TEL_ACK_RX(1),
    {TEL_RX, 20, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(2), 0xD7, 0x04, 0x1B, 0x10, 0x00, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18}},
    TEL_ACK_TX(2),
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, TPCI_DATA(2), 0xD6, 0x04, 0x1B, 0x00, 0x00}},
    TEL_ACK_RX(2),
    {TEL_RX, 22, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(3), 0xD7, 0x04, 0x0D, 0x20, 0x01, 0x11, 0x12, 0x13, 0x14, 0x15, 0x21, 0x22, 0x23, 0x24, 0x25}},
    TEL_ACK_TX(3),
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, TPCI_DATA(3), 0xD6, 0x04, 0x0D, 0x00, 0x01}},
    TEL_ACK_RX(3),
    {END, 0, 0, 0, nullptr, {0}}
};

// Unload clears the table reference of the object
#define TEL_UNLOAD_CLEARS_REFERENCE(seq, obj, a0, a1)     TEL_VALUE_READ(seq, obj, 0x07), TEL_VALUE_RESPONSE_4(seq, obj, 0x07, 0x00, 0x00, a0, a1),     TEL_LOAD_EVENT(seq + 1, obj, 0x04, 0x00),     TEL_VALUE_READ(seq + 2, obj, 0x07), TEL_VALUE_RESPONSE_4(seq + 2, obj, 0x07, 0x00, 0x00, 0x00, 0x00)

static Telegram apciSystemBUnloadTelegrams[] =
{
    {TEL_RX, 7, 0, 0, setSystemBTableAddresses, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    TEL_UNLOAD_CLEARS_REFERENCE(0, 1, 0x34, 0x56),
    TEL_UNLOAD_CLEARS_REFERENCE(3, 2, 0x34, 0x58),
    TEL_UNLOAD_CLEARS_REFERENCE(6, 3, 0x35, 0x00),
    TEL_UNLOAD_CLEARS_REFERENCE(9, 4, 0x36, 0x00),
    TEL_UNLOAD_CLEARS_REFERENCE(12, 5, 0x37, 0x00),
    {END, 0, 0, 0, nullptr, {0}}
};

// allocation ascending from 0x3700, reallocation of object 4 keeps its base, an allocation that does not fit fails
static Telegram apciSystemBAllocationTelegrams[] =
{
    TEL_CONNECT,
    TEL_START_LOADING(0, 4), TEL_ALLOC(1, 4, 0x00, 0x40, 0x00, 0x00, 0x02),
    TEL_TABLE_REFERENCE(2, 4, 0x37, 0x00), TEL_MCB_SIZE(3, 4, 0x00, 0x40),
    TEL_START_LOADING(4, 3), TEL_ALLOC(5, 3, 0x00, 0x20, 0x00, 0x00, 0x02),
    TEL_TABLE_REFERENCE(6, 3, 0x37, 0x40),
    TEL_ALLOC(7, 4, 0x00, 0x40, 0x00, 0x00, 0x02), TEL_TABLE_REFERENCE(8, 4, 0x37, 0x00),
    TEL_START_LOADING(9, 1), TEL_ALLOC(10, 1, 0x08, 0x00, 0x00, 0x00, 0x03),
    TEL_TABLE_REFERENCE(11, 1, 0x00, 0x00), TEL_LOAD_STATE(12, 1, 0x03), TEL_MCB_SIZE(13, 1, 0x00, 0x00),
    {END, 0, 0, 0, nullptr, {0}}
};

// reallocation: a segment that no longer fits moves behind all segments, the topmost one keeps its base
static Telegram apciSystemBReallocationTelegrams[] =
{
    TEL_CONNECT,
    TEL_START_LOADING(0, 4), TEL_ALLOC(1, 4, 0x00, 0x40, 0x00, 0x00, 0x02),
    TEL_START_LOADING(2, 3), TEL_ALLOC(3, 3, 0x00, 0x20, 0x00, 0x00, 0x02),
    TEL_ALLOC(4, 4, 0x00, 0x50, 0x00, 0x00, 0x02), TEL_TABLE_REFERENCE(5, 4, 0x37, 0x60),
    TEL_ALLOC(6, 4, 0x00, 0x20, 0x00, 0x00, 0x02), TEL_TABLE_REFERENCE(7, 4, 0x37, 0x60),
    TEL_MCB_SIZE(8, 4, 0x00, 0x20), TEL_TABLE_REFERENCE(9, 3, 0x37, 0x40),
    {END, 0, 0, 0, nullptr, {0}}
};

// mode 1 fills the new segment with the fill byte from its base on (memory addresses 0x0510 and 0x051E are 0x3710 and 0x371E)
static Telegram apciSystemBAllocationFillTelegrams[] =
{
    TEL_CONNECT,
    TEL_START_LOADING(0, 2), TEL_ALLOC(1, 2, 0x00, 0x10, 0x00, 0x00, 0x02),
    TEL_START_LOADING(2, 3), TEL_ALLOC(3, 3, 0x00, 0x10, 0x01, 0xA5, 0x02),
    TEL_TABLE_REFERENCE(4, 3, 0x37, 0x10),
    TEL_MEMORY_READ_2(5, 0x05, 0x10, 0xA5, 0xA5), TEL_MEMORY_READ_2(6, 0x05, 0x1E, 0xA5, 0xA5),
    {END, 0, 0, 0, nullptr, {0}}
};

// unload clears the table reference and the MCB size, the next allocation uses the freed range
static Telegram apciSystemBAllocationUnloadTelegrams[] =
{
    TEL_CONNECT,
    TEL_START_LOADING(0, 4), TEL_ALLOC(1, 4, 0x00, 0x40, 0x00, 0x00, 0x02),
    TEL_LOAD_EVENT(2, 4, 0x04, 0x00), TEL_TABLE_REFERENCE(3, 4, 0x00, 0x00), TEL_MCB_SIZE(4, 4, 0x00, 0x00),
    TEL_START_LOADING(5, 5), TEL_ALLOC(6, 5, 0x00, 0x20, 0x00, 0x00, 0x02),
    TEL_TABLE_REFERENCE(7, 5, 0x37, 0x00),
    {END, 0, 0, 0, nullptr, {0}}
};

// Load Completed stores the CRC16 of the segment in the MCB. The 8 octets 01 02 .. 08 at 0x3700 (memory address 0x0500)
// give 9795h: CRC-16/CCITT (polynomial 1021h, start value 1D0Fh, no reflection, no final XOR), see KNX 03_05_01 4.2.27.
// The CRC is 0 for a table without a segment.
static Telegram apciSystemBCrcTelegrams[] =
{
    TEL_CONNECT,
    TEL_START_LOADING(0, 1), TEL_ALLOC(1, 1, 0x00, 0x08, 0x00, 0x00, 0x02), TEL_TABLE_REFERENCE(2, 1, 0x37, 0x00),
    TEL_MEMORY_WRITE_8(3, 0x05, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08),
    TEL_MEMORY_READ_8(4, 0x05, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08),
    TEL_LOAD_EVENT(5, 1, 0x02, 0x01), TEL_MCB_SIZE_CRC(6, 1, 0x00, 0x08, 0x97, 0x95),
    TEL_START_LOADING(7, 2), TEL_LOAD_EVENT(8, 2, 0x02, 0x01), TEL_MCB_SIZE_CRC(9, 2, 0x00, 0x00, 0x00, 0x00),
    {END, 0, 0, 0, nullptr, {0}}
};

static void checkSystemBApplicationRunning([[maybe_unused]] void* state, unsigned int running)
{
    CHECK(bcuUnderTest->applicationRunning() == (running != 0));
}

#define TEL_CHECK_APPLICATION_RUNNING(running) {LOOP, 0, 0, running, checkSystemBApplicationRunning, {0}}

// the application runs if the objects 1 to 4 are loaded, the object 5 is not needed
static Telegram apciSystemBApplicationRunningTelegrams[] =
{
    TEL_CONNECT,
    TEL_CHECK_APPLICATION_RUNNING(0),
    TEL_START_LOADING(0, 1), TEL_LOAD_EVENT(1, 1, 0x02, 0x01),
    TEL_START_LOADING(2, 2), TEL_LOAD_EVENT(3, 2, 0x02, 0x01),
    TEL_START_LOADING(4, 4), TEL_LOAD_EVENT(5, 4, 0x02, 0x01),
    TEL_CHECK_APPLICATION_RUNNING(0),
    TEL_START_LOADING(6, 3), TEL_LOAD_EVENT(7, 3, 0x02, 0x01),
    TEL_CHECK_APPLICATION_RUNNING(1),
    TEL_LOAD_EVENT(8, 4, 0x04, 0x00),
    TEL_CHECK_APPLICATION_RUNNING(0),
    {END, 0, 0, 0, nullptr, {0}}
};

#define SYSTEMB_ALLOCATION_TEST_CASE(NAME, TITLE, TELEGRAMS) \
    static Test_Case NAME = {TITLE, MANUFACTURER, DEVICE, VERSION, 0, NULL, tc_setup, NULL, NULL, NULL, TELEGRAMS}
SYSTEMB_ALLOCATION_TEST_CASE(testCaseApciSystemBAllocation, "APCI_PROPERTY SYSTEM B allocation",
                             apciSystemBAllocationTelegrams);
SYSTEMB_ALLOCATION_TEST_CASE(testCaseApciSystemBReallocation, "APCI_PROPERTY SYSTEM B reallocation",
                             apciSystemBReallocationTelegrams);
SYSTEMB_ALLOCATION_TEST_CASE(testCaseApciSystemBAllocationFill, "APCI_PROPERTY SYSTEM B allocation fill",
                             apciSystemBAllocationFillTelegrams);
SYSTEMB_ALLOCATION_TEST_CASE(testCaseApciSystemBAllocationUnload, "APCI_PROPERTY SYSTEM B allocation unload",
                             apciSystemBAllocationUnloadTelegrams);
SYSTEMB_ALLOCATION_TEST_CASE(testCaseApciSystemBApplicationRunning, "APCI_PROPERTY SYSTEM B application running",
                             apciSystemBApplicationRunningTelegrams);
static Test_Case testCaseApciSystemBCrc = {"APCI_PROPERTY SYSTEM B MCB CRC", MANUFACTURER, DEVICE, VERSION, 0, NULL,
                                           tc_setupVerifyMode, NULL, NULL, NULL, apciSystemBCrcTelegrams};

static Test_Case testCaseApciSystemBObjectType =
{
    "APCI_PROPERTY SYSTEM B object types",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciSystemBObjectTypeTelegrams
};

static Test_Case testCaseApciSystemBLoadState =
{
    "APCI_PROPERTY SYSTEM B load state machine",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciSystemBLoadStateTelegrams
};

static Test_Case testCaseApciSystemBElementCount =
{
    "APCI_PROPERTY SYSTEM B element count",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciSystemBElementCountTelegrams
};

static Test_Case testCaseApciSystemBUnload =
{
    "APCI_PROPERTY SYSTEM B unload",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciSystemBUnloadTelegrams
};

static Test_Case testCaseApciSystemBTableReference =
{
    "APCI_PROPERTY SYSTEM B table reference",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciSystemBTableReferenceTelegrams
};

static Test_Case testCaseApciSystemBApplication =
{
    "APCI_PROPERTY SYSTEM B application programs",
    MANUFACTURER, DEVICE, VERSION,
    0,    //powerOnDelay
    NULL, // eePromSetup
    tc_setup,
    NULL,
    NULL,
    NULL,
    apciSystemBApplicationTelegrams
};

TEST_CASE("APCI processing", "[protocol][APCI]")
{
    BcuType bcuTypeToTest;
    // BCU 1 doesn't have properties so use special telegram sets
    testCaseApciPropertyDescriptionReadPdu.telegram = apciPropertyDescriptionReadTelegrams_BCU1;
    testCaseApciPropertyValueReadPdu.telegram = apciPropertyValueReadTelegrams_BCU1;
    testCaseApciPropertyValueWritePdu.telegram = apciPropertyValueWriteTelegrams_BCU1;
    SECTION("BCU 1") {
        bcuTypeToTest = BCU_1;
        executeTest(bcuTypeToTest, &testCaseApciAdcReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryWritePdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadConnectedPdu);
        executeTest(bcuTypeToTest, &testCaseApciBasicRestartPdu);
        executeTest(bcuTypeToTest, &testCaseApciMasterResetPdu);
        executeTest(bcuTypeToTest, &testCaseApciAuthorizeRequestPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyDescriptionReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueWritePdu);
    }

    // all other BCUs have properties so use default telegram sets
    testCaseApciPropertyDescriptionReadPdu.telegram = apciPropertyDescriptionReadTelegrams;
    testCaseApciPropertyValueReadPdu.telegram = apciPropertyValueReadTelegrams;
    testCaseApciPropertyValueWritePdu.telegram = apciPropertyValueWriteTelegrams;
    SECTION("BCU 2") {
        bcuTypeToTest = BCU_2;
        executeTest(bcuTypeToTest, &testCaseApciAdcReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryWritePdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadConnectedPdu);
        executeTest(bcuTypeToTest, &testCaseApciBasicRestartPdu);
        executeTest(bcuTypeToTest, &testCaseApciMasterResetPdu);
        executeTest(bcuTypeToTest, &testCaseApciAuthorizeRequestPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyDescriptionReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueWritePdu);
    }

    SECTION("BCU 0x0701 (BIM112)") {
        bcuTypeToTest = BCU_0701;
        executeTest(bcuTypeToTest, &testCaseApciAdcReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryWritePdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadConnectedPdu);
        executeTest(bcuTypeToTest, &testCaseApciBasicRestartPdu);
        executeTest(bcuTypeToTest, &testCaseApciMasterResetPdu);
        executeTest(bcuTypeToTest, &testCaseApciAuthorizeRequestPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyDescriptionReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueWritePdu);
    }

    SECTION("BCU 0x0705 (BIM112)") {
        bcuTypeToTest = BCU_0705;
        executeTest(bcuTypeToTest, &testCaseApciAdcReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryWritePdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadConnectedPdu);
        executeTest(bcuTypeToTest, &testCaseApciBasicRestartPdu);
        executeTest(bcuTypeToTest, &testCaseApciMasterResetPdu);
        executeTest(bcuTypeToTest, &testCaseApciAuthorizeRequestPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyDescriptionReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueWritePdu);
    }

    SECTION("BCU 0x07B0") {
        bcuTypeToTest = BCU_07B0;
        testCaseApciPropertyDescriptionReadPdu.telegram = apciPropertyDescriptionReadTelegrams_SystemB;
        testCaseApciPropertyValueReadPdu.telegram = apciPropertyValueReadTelegrams_SystemB;
        executeTest(bcuTypeToTest, &testCaseApciAdcReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciMemoryWritePdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciDeviceDescriptorReadConnectedPdu);
        executeTest(bcuTypeToTest, &testCaseApciBasicRestartPdu);
        executeTest(bcuTypeToTest, &testCaseApciMasterResetPdu);
        executeTest(bcuTypeToTest, &testCaseApciAuthorizeRequestPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyDescriptionReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueReadPdu);
        executeTest(bcuTypeToTest, &testCaseApciPropertyValueWritePdu);
        executeTest(bcuTypeToTest, &testCaseApciSystemBObjectType);
        executeTest(bcuTypeToTest, &testCaseApciSystemBTableReference);
        executeTest(bcuTypeToTest, &testCaseApciSystemBApplication);
        executeTest(bcuTypeToTest, &testCaseApciSystemBLoadState);
        executeTest(bcuTypeToTest, &testCaseApciSystemBElementCount);
        executeTest(bcuTypeToTest, &testCaseApciSystemBUnload);
        executeTest(bcuTypeToTest, &testCaseApciSystemBAllocation);
        executeTest(bcuTypeToTest, &testCaseApciSystemBReallocation);
        executeTest(bcuTypeToTest, &testCaseApciSystemBAllocationFill);
        executeTest(bcuTypeToTest, &testCaseApciSystemBAllocationUnload);
        executeTest(bcuTypeToTest, &testCaseApciSystemBCrc);
        executeTest(bcuTypeToTest, &testCaseApciSystemBApplicationRunning);
    }
}


TEST_CASE("SYSTEM B CRC16 matches the KNX example", "[protocol][APCI]")
{
    // KNX 03_05_01 4.2.27: the ASCII string "123456789" has the CRC E5CCh
    const uint8_t example[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    PropertiesSYSTEMB properties(nullptr);

    CHECK(properties.crc16(example, sizeof(example)) == 0xE5CC);
    CHECK(properties.crc16(example, 0) == 0x1D0F);
}

TEST_CASE("APCI memory services handle invalid parameters", "[protocol][APCI][apci-memory]")
{
    BcuType bcuTypeToTest = BCU_1;
    SECTION("BCU 1") {
        bcuTypeToTest = BCU_1;
    }
    SECTION("BCU 2") {
        bcuTypeToTest = BCU_2;
    }
    SECTION("BCU 0x0701 (BIM112)") {
        bcuTypeToTest = BCU_0701;
    }
    SECTION("BCU 0x0705 (BIM112)") {
        bcuTypeToTest = BCU_0705;
    }

#if SBLIB_MAX_APDU_LENGTH == 15
    // with longer APDUs these numbers are valid, see test_prot_memory_lf.cpp
    executeTest(bcuTypeToTest, &testCaseMemoryReadNumber13);
    executeTest(bcuTypeToTest, &testCaseMemoryReadNumber14);
    executeTest(bcuTypeToTest, &testCaseMemoryReadNumber15);
#endif
    executeTest(bcuTypeToTest, &testCaseMemoryWriteTooShort);
    executeTest(bcuTypeToTest, &testCaseMemoryWriteTooLong);
    if (bcuTypeToTest != BCU_1)
    {
        // Verify Mode is not available for BCU 1
        executeTest(bcuTypeToTest, &testCaseMemoryWriteVerifyModeOk);
        executeTest(bcuTypeToTest, &testCaseMemoryWriteVerifyModeFailed);
    }
    executeTest(bcuTypeToTest, &testCaseMemoryWriteVerifyModeInactiveFailed);
}



static Telegram apciPropertyValueInvalidParametersTelegrams[] =
{
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}},
    // PID_HARDWARE_TYPE (6 octets), valid write
    {TEL_RX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x43, 0xD7, 0x00, 0x4E, 0x10, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC2}},
    {TEL_TX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6B, 0x43, 0xD6, 0x00, 0x4E, 0x10, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC2}},
    // write start index 0: rejected, response without data
    {TEL_RX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x47, 0xD7, 0x00, 0x4E, 0x10, 0x00, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xC6}},
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x47, 0xD6, 0x00, 0x4E, 0x00, 0x00}},
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xC6}},
    // write start index 2 (only element 1 exists): rejected
    {TEL_RX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x4B, 0xD7, 0x00, 0x4E, 0x10, 0x02, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xCA}},
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x4B, 0xD6, 0x00, 0x4E, 0x00, 0x02}},
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xCA}},
    // write with only 3 data octets instead of 6: rejected
    {TEL_RX, 15, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x4F, 0xD7, 0x00, 0x4E, 0x10, 0x01, 0x11, 0x12, 0x13}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xCE}},
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x4F, 0xD6, 0x00, 0x4E, 0x00, 0x01}},
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xCE}},
    // the value is unchanged (read returns the stored byte order, as before)
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x53, 0xD5, 0x00, 0x4E, 0x10, 0x01}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xD2}},
    {TEL_TX, 18, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6B, 0x53, 0xD6, 0x00, 0x4E, 0x10, 0x01, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}},
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xD2}},
    // read start index 0: current number of elements
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x57, 0xD5, 0x00, 0x4E, 0x10, 0x00}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xD6}},
    {TEL_TX, 14, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x67, 0x57, 0xD6, 0x00, 0x4E, 0x10, 0x00, 0x00, 0x01}},
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xD6}},
    // read start index 2: response without data
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x5B, 0xD5, 0x00, 0x4E, 0x10, 0x02}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xDA}},
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x5B, 0xD6, 0x00, 0x4E, 0x00, 0x02}},
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xDA}},
    // read two elements starting at 1: response without data
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, 0x5F, 0xD5, 0x00, 0x4E, 0x20, 0x01}},
    {TEL_TX,  7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, 0xDE}},
    {TEL_TX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x65, 0x5F, 0xD6, 0x00, 0x4E, 0x00, 0x01}},
    {TEL_RX,  7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0xDE}},
    {END, 0, 0, 0, nullptr, {0}}
};

MEMORY_TEST_CASE(testCasePropertyValueInvalidParameters, tc_setup, apciPropertyValueInvalidParametersTelegrams);

TEST_CASE("APCI property value services handle invalid parameters", "[protocol][APCI][apci-property]")
{
    BcuType bcuTypeToTest = BCU_2;
    SECTION("BCU 2") {
        bcuTypeToTest = BCU_2;
    }
    SECTION("BCU 0x0701 (BIM112)") {
        bcuTypeToTest = BCU_0701;
    }
    SECTION("BCU 0x0705 (BIM112)") {
        bcuTypeToTest = BCU_0705;
    }

    executeTest(bcuTypeToTest, &testCasePropertyValueInvalidParameters);
}
