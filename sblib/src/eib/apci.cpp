/******************************************************************************
 * @addtogroup SBLIB Selfbus library
 * @defgroup
 * @brief
 * @details
 *          
 * @{
 *
 * @file   apci.cpp
 * @author Darthyson <darth@maptrack.de> Copyright (c) 2021
 * @bug No known bugs.
 ******************************************************************************/

/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 3 as
 published by the Free Software Foundation.
 ---------------------------------------------------------------------------*/
#include <sblib/bits.h>
#include <sblib/eib/apci.h>
#include <sblib/eib/knx_npdu.h>
#include <sblib/utils.h>

ApciCommand apciCommand(unsigned char* telegram)
{
    auto apci = (unsigned short)(((telegram[telegramTpciIndex(telegram)] & 0x03) << 8) |
                                 telegram[telegramTpciIndex(telegram) + 1]);
    unsigned short shortCommand = apci & APCI_GROUP_MASK;
    switch (shortCommand)
    {
        case APCI_ADC_READ_PDU:
        case APCI_MEMORY_READ_PDU:
        case APCI_MEMORY_WRITE_PDU:
        case APCI_DEVICEDESCRIPTOR_READ_PDU:
            return ((ApciCommand)shortCommand);

        default:
            return ((ApciCommand)apci);
    }
}

void setApciCommand(unsigned char* telegram, const ApciCommand newApciCommand, const uint8_t additionalData)
{
    // Send buffers are standard frames. The control field of a connected response is written
    // after processApci(), so it can't select the octets here.
    telegram[APCI_HIGH_BYTE] = HIGH_BYTE(newApciCommand);
    telegram[APCI_LOW_BYTE] = lowByte(newApciCommand);
    telegram[APCI_LOW_BYTE] |= additionalData;
}

/** @}*/
