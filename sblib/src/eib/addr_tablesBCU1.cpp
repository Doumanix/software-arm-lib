/*
 * addr_tablesBCU1.cpp
 *
 *  Created on: 20.11.2021
 *      Author: dridders
 */

#include <sblib/eib/addr_tablesBCU1.h>

int AddrTablesBCU1::indexOfAddr(const int addr)
{
    uint8_t* tab = addrTable();
    int num = 0;

    if (tab)
        num = *tab;
    // BCU1 addresses start at 0x17; the final EEPROM byte is the checksum.
    if (tab && num > (static_cast<int>(userEeprom->size()) - 0x17 - 1) / 2)
    {
        return -1;
    }
    tab += 3;

    const int addrHigh = addr >> 8;
    const int addrLow = addr & 255;

    for (int i = 1; i < num; ++i, tab += 2)
    {
        if (tab[0] == addrHigh && tab[1] == addrLow)
            return i;
    }

    return -1;
}

uint8_t* AddrTablesBCU1::addrTable()
{
    return (uint8_t*) &userEeprom->addrTabSize();
}

uint8_t* AddrTablesBCU1::assocTable()
{
    return userEeprom->userEepromData + userEeprom->assocTabPtr();
}
