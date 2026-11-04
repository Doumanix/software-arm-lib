/*
 *  bcu.cpp - EIB bus coupling unit.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */

#include "sblib/eib/bcu2.h"
#include "sblib/eib/knx_lpdu.h"
#include "sblib/eib/knx_npdu.h"
#include "sblib/eib/serial_number.h"
#include "sblib/internal/iap.h"
#include "sblib/bits.h"
#include <cstring>


void BCU2::setOwnAddress(const uint16_t addr)
{
    BcuDefault::setOwnAddress(addr);
    if (ownAddressInAddrTable() && (userEeprom->loadState()[OT_ADDR_TABLE] == LS_LOADING))
    {
        uint8_t* addrTab = addrTables->addrTable() + 1;
        *(addrTab + 0) = HIGH_BYTE(ownAddress());
        *(addrTab + 1) = lowByte(ownAddress());
        userEeprom->modified(true);
    }
}

inline void BCU2::begin(const int manufacturer, const int deviceType, const int version, const uint16_t readOnlyCommObjectTableAddress)
{
    BcuDefault::begin(manufacturer, deviceType, version);

    userRam->peiType() = 0;    // PEI type: 0=no adapter connected to PEI.
    userEeprom->setAppType(0); // Set to BCU2 application. ETS reads this when programming.

    if (ownAddressInAddrTable() && (userEeprom->loadState()[OT_ADDR_TABLE] == LS_LOADING))
    {
        const uint8_t* addrTab = addrTables->addrTable() + 1;
        setOwnAddress(makeWord(*(addrTab), *(addrTab + 1)));
    }

    commObjectTableAddressStatic = readOnlyCommObjectTableAddress;
    if ((commObjectTableAddressStatic != 0) && (userEeprom->commsTabAddr() != commObjectTableAddressStatic))
    {
        userEeprom->setCommsTabAddr(commObjectTableAddressStatic);
        userEeprom->modified(true);
    }

    unsigned int partID;
    uint8_t uniqueID[IAP_UID_LENGTH];

    bool useOldSerialStyle = true;
    if (iapReadUID(&uniqueID[0]) == IAP_SUCCESS)
    {
        /// https://community.nxp.com/t5/LPC-Microcontrollers/IAP-C-code-example-query/m-p/596139/highlight/true#M22963
        /// Unfortunately the details of what go into the 128-bit GUID cannot be disclosed.
        /// It can be said, however, that the 128-bit GUIDs are not random, nor are they sequential.
        /// Thus, to ensure there are no collisions with other devices, the full 128 bits should be used.
        ///
        /// https://community.nxp.com/t5/LPC-Microcontrollers/IAP-C-code-example-query/m-p/596142/highlight/true#M22966
        /// Quote:
        /// In the thread that Zero provided, it shows that the first and third values (result[1] and result[3])
        /// vary much more than the second and fourth values (result[2] and result[4])
        /// -- so it was suggested that only these be used to create some kind of hash.
        /// Even so-- the message from NXP is: "If you don't use all 128-bits, we cannot guarantee collisions".

        // create a 48bit serial/hash from the 128bit Guid
        //useOldSerialStyle = !hashUID(&uniqueID[0], sizeof(uniqueID), &userEeprom->serial()[0], userEeprom->serialSize()); ///\todo delete on release
        uint8_t knxSerial[KNX_SERIAL_NUMBER_LENGTH];
        useOldSerialStyle = !createKNXSerial(&uniqueID[0], sizeof(uniqueID), &knxSerial[0], sizeof(knxSerial));
        if (!useOldSerialStyle)
        {
            userEeprom->setSerial(&knxSerial[0], sizeof(knxSerial));
        }
    }

    if (useOldSerialStyle)
    {
        iapReadPartID(&partID);
        const uint8_t oldSerial[KNX_SERIAL_NUMBER_LENGTH] = {
            static_cast<uint8_t>(partID), static_cast<uint8_t>(partID >> 8), static_cast<uint8_t>(partID >> 16),
            static_cast<uint8_t>(partID >> 24), HIGH_BYTE(SBLIB_VERSION), lowByte(SBLIB_VERSION)};
        userEeprom->setSerial(&oldSerial[0], sizeof(oldSerial));
    }

    userEeprom->setOrderInfo(userEeprom->orderInfoSize() - 2, HIGH_BYTE(SBLIB_VERSION));
    userEeprom->setOrderInfo(userEeprom->orderInfoSize() - 1, lowByte(SBLIB_VERSION));

    BcuDefault::_begin();
    static_cast<ComObjectsBCU2*>(this->comObjects)->printObjectConfigTable();
}

void BCU2::begin(const int manufacturer, const int deviceType, const int version)
{
    begin(manufacturer, deviceType, version, 0);
}

bool BCU2::applicationRunning() const
{
    if (!enabled)
        return false;

    return ((userRam->runState() == 1) &&
            (userEeprom->loadState()[OT_ADDR_TABLE] == LS_LOADED) &&  // address table object
            (userEeprom->loadState()[OT_ASSOC_TABLE] == LS_LOADED) && // association table object
            (userEeprom->loadState()[OT_APPLICATION] == LS_LOADED));  // application object. All three in state "Loaded"
}

BCU2::BCU2() : BCU2(new UserRamBCU2(), new UserEepromBCU2(), new ComObjectsBCU2(this), new AddrTablesBCU2(this), new PropertiesBCU2(this))
{}

BCU2::BCU2(UserRamBCU2* userRam, UserEepromBCU2* userEeprom, ComObjectsBCU2* comObjects, AddrTablesBCU2* addrTables, PropertiesBCU2* properties) :
    BcuDefault(userRam, userEeprom, comObjects, addrTables),
    userRam(userRam),
    userEeprom(userEeprom),
    properties(properties)
{}

bool BCU2::processApci(const ApciCommand apciCmd, unsigned char* telegram, const TelegramLength telLength,
                       uint8_t* sendBuffer)
{
    uint8_t count;
    uint16_t address;
    uint8_t index;
    bool found;
    uint8_t id;

    switch (apciCmd)
    {
        case APCI_PROPERTY_VALUE_READ_PDU:
        case APCI_PROPERTY_VALUE_WRITE_PDU:
            index = telegram[telegramTpciIndex(telegram) + 2];
            id = telegram[telegramTpciIndex(telegram) + 3];
            count = telegram[telegramTpciIndex(telegram) + 4] >> 4;
            address = ((telegram[telegramTpciIndex(telegram) + 4] & 0x0f) << 4);
            address = (uint16_t)(address | (telegram[telegramTpciIndex(telegram) + 5]));

            sendBuffer[5] = 0x60 + 5; // routing count in high nibble + response length in low nibble
            setApciCommand(sendBuffer, APCI_PROPERTY_VALUE_RESPONSE_PDU, 0);
            sendBuffer[8] = index;
            sendBuffer[9] = id;
            sendBuffer[10] = telegram[telegramTpciIndex(telegram) + 4];
            sendBuffer[11] = telegram[telegramTpciIndex(telegram) + 5];

            if (apciCmd == APCI_PROPERTY_VALUE_READ_PDU)
                found = properties->propertyValueReadTelegram(index, (PropertyID)id, count, address, sendBuffer);
            else
                found = properties->propertyValueWriteTelegram(index, (PropertyID)id, count, address, sendBuffer);
            if (!found)
                sendBuffer[10] &= 0x0f; // number of elements 0, start index unchanged
            return (true);

        case APCI_PROPERTY_DESCRIPTION_READ_PDU:
            index = telegram[telegramTpciIndex(telegram) + 2];
            id = telegram[telegramTpciIndex(telegram) + 3];
            address = telegram[telegramTpciIndex(telegram) + 4];
            sendBuffer[5] = 0x60 + 8; // routing count in high nibble + response length in low nibble
            setApciCommand(sendBuffer, APCI_PROPERTY_DESCRIPTION_RESPONSE_PDU, 0);
            sendBuffer[8] = index;
            sendBuffer[9] = id;
            sendBuffer[10] = (uint8_t)address;
            properties->propertyDescReadTelegram(index, (PropertyID)id, address, sendBuffer);
            return (true);

        default:
            return BcuDefault::processApci(apciCmd, telegram, telLength, sendBuffer);
    }
}

uint16_t BCU2::getCommObjectTableAddressStatic() const
{
    return commObjectTableAddressStatic;
}

void BCU2::setHardwareType(const uint8_t* hardwareType, uint8_t size)
{
    if (size > userEeprom->orderSize())
    {
        size = userEeprom->orderSize();
    }
    userEeprom->setOrder(hardwareType, size);
}

bool BCU2::processBroadCastTelegram(const ApciCommand apciCmd, unsigned char* telegram, const TelegramLength telLength)
{
    switch (apciCmd)
    {
        case APCI_INDIVIDUALADDRESS_SERIALNUMBER_WRITE_PDU:
            // octets 2-7 after the TPCI contain the serial number to check, octets 8-9 the new physical address
            if (memcmp(&telegram[telegramTpciIndex(telegram) + 2], &userEeprom->serial()[0],
                       userEeprom->serialSize()) != 0)
            {
                return (false);
            }

            setOwnAddress(makeWord(telegram[telegramTpciIndex(telegram) + 8],
                                   telegram[telegramTpciIndex(telegram) + 9]));
            if (userEeprom->isModified())
            {
                while (bus->sendingFrame()) // ensure L2 ack is sent, before calling noInterrupts()
                    ;
                userEeprom->writeUserEeprom();
            }
            break;

        case APCI_INDIVIDUALADDRESS_SERIALNUMBER_READ_PDU:
            // octets 2-7 after the TPCI contain the serial number to check
            if (memcmp(&telegram[telegramTpciIndex(telegram) + 2], &userEeprom->serial()[0],
                       userEeprom->serialSize()) != 0)
            {
                return (false);
            }
            sendApciIndividualAddressSerialNumberReadResponse();
            break;

        default:
            // let base class handle the broadcast
            return (BcuDefault::processBroadCastTelegram(apciCmd, telegram, telLength));
    }
    return (true);
}

void BCU2::sendApciIndividualAddressSerialNumberReadResponse()
{
    const auto sendBuffer = acquireSendBuffer();
    initLpdu(sendBuffer, PRIORITY_SYSTEM, false, FRAME_STANDARD);
    // 1+2 contain the sender address, which is set by bus.sendTelegram()
    setDestinationAddress(sendBuffer, 0x0000); // Zero target address, it's a broadcast
    sendBuffer[5] = 0xe0 + 11;                 // address type & routing count in high nibble + response length (11) in low nibble
    setApciCommand(sendBuffer, APCI_INDIVIDUALADDRESS_SERIALNUMBER_RESPONSE_PDU, 0);
    // sendBuffer[8-13] contains serial number
    for (uint8_t i = 0; i < userEeprom->serialSize(); i++)
    {
        sendBuffer[8 + i] = userEeprom->serial()[i];
    }

    // sendBuffer[14-15] contains domain address
    sendBuffer[14] = 0x00;
    sendBuffer[15] = 0x00;

    // sendBuffer[16-17] reserved
    sendBuffer[16] = 0x00;
    sendBuffer[17] = 0x00;

    sendPreparedTelegram();
}
