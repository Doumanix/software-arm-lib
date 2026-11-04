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

// SYSTEM B: building blocks for one connected property exchange (request, T_ACK, response, T_ACK) with sequence number seq
#define TPCI_DATA(seq) (0x43 + 4 * (seq))
#define TPCI_ACK(seq) (0xC2 + 4 * (seq))
#define TEL_ACK_TX(seq) {TEL_TX, 7, 1, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x60, TPCI_ACK(seq)}}
#define TEL_ACK_RX(seq) {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, TPCI_ACK(seq)}}
#define TEL_CONNECT {TEL_RX, 7, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x60, 0x80}}

#define TEL_DESC_READ(seq, obj, pid) \
    {TEL_RX, 11, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD8, obj, pid, 0x00}}, \
    TEL_ACK_TX(seq)
#define TEL_DESC_RESPONSE(seq, obj, pid, type, elems, access) \
    {TEL_TX, 15, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x68, TPCI_DATA(seq), 0xD9, obj, pid, 0x00, type, 0x00, elems, access}}, \
    TEL_ACK_RX(seq)

#define TEL_VALUE_READ(seq, obj, pid) \
    {TEL_RX, 12, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD5, obj, pid, 0x10, 0x01}}, \
    TEL_ACK_TX(seq)
#define TEL_VALUE_WRITE_1(seq, obj, pid, d0) \
    {TEL_RX, 13, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD7, obj, pid, 0x10, 0x01, d0}}, \
    TEL_ACK_TX(seq)
#define TEL_VALUE_WRITE_5(seq, obj, pid, d0, d1, d2, d3, d4) \
    {TEL_RX, 17, 0, 0, NULL, {0xB0, 0xA0, 0x01, 0xA0, 0x00, 0x61, TPCI_DATA(seq), 0xD7, obj, pid, 0x10, 0x01, d0, d1, d2, d3, d4}}, \
    TEL_ACK_TX(seq)
#define TEL_VALUE_RESPONSE_1(seq, obj, pid, d0) \
    {TEL_TX, 13, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x66, TPCI_DATA(seq), 0xD6, obj, pid, 0x10, 0x01, d0}}, \
    TEL_ACK_RX(seq)
#define TEL_VALUE_RESPONSE_2(seq, obj, pid, d0, d1) \
    {TEL_TX, 14, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x67, TPCI_DATA(seq), 0xD6, obj, pid, 0x10, 0x01, d0, d1}}, \
    TEL_ACK_RX(seq)
#define TEL_VALUE_RESPONSE_4(seq, obj, pid, d0, d1, d2, d3) \
    {TEL_TX, 16, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x69, TPCI_DATA(seq), 0xD6, obj, pid, 0x10, 0x01, d0, d1, d2, d3}}, \
    TEL_ACK_RX(seq)
#define TEL_VALUE_RESPONSE_5(seq, obj, pid, d0, d1, d2, d3, d4) \
    {TEL_TX, 17, 0, 0, NULL, {0xB0, 0xA0, 0x00, 0xA0, 0x01, 0x6A, TPCI_DATA(seq), 0xD6, obj, pid, 0x10, 0x01, d0, d1, d2, d3, d4}}, \
    TEL_ACK_RX(seq)

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

MEMORY_READ_TOO_MANY(0x0D);
MEMORY_READ_TOO_MANY(0x0E);
MEMORY_READ_TOO_MANY(0x0F);

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

MEMORY_TEST_CASE(testCaseMemoryReadNumber13, tc_setup, apciMemoryReadNumber0x0DTelegrams);
MEMORY_TEST_CASE(testCaseMemoryReadNumber14, tc_setup, apciMemoryReadNumber0x0ETelegrams);
MEMORY_TEST_CASE(testCaseMemoryReadNumber15, tc_setup, apciMemoryReadNumber0x0FTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteTooShort, tc_setup, apciMemoryWriteTooShortTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteTooLong, tc_setup, apciMemoryWriteTooLongTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteVerifyModeOk, tc_setupVerifyMode, apciMemoryWriteVerifyModeOkTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteVerifyModeFailed, tc_setupVerifyMode, apciMemoryWriteVerifyModeFailedTelegrams);
MEMORY_TEST_CASE(testCaseMemoryWriteVerifyModeInactiveFailed, tc_setup, apciMemoryWriteVerifyModeInactiveFailedTelegrams);

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
    }
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

    executeTest(bcuTypeToTest, &testCaseMemoryReadNumber13);
    executeTest(bcuTypeToTest, &testCaseMemoryReadNumber14);
    executeTest(bcuTypeToTest, &testCaseMemoryReadNumber15);
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
