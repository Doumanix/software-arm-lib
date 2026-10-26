/*
 * addr_tablesBCU2.cpp
 *
 *  Created on: 20.11.2021
 *      Author: dridders
 */

#include <sblib/eib/addr_tablesBCU2.h>
#include <sblib/eib/bcu2.h>
#include <sblib/bits.h>

int AddrTablesBCU2::indexOfAddr(const int addr)
{
    uint8_t* tab = addrTable();
    int num = 0;

    if (tab)
        num = *tab;
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

uint8_t* AddrTablesBCU2::addrTable()
{
    const uint8_t* addr = (uint8_t*) &bcu->userEeprom->addrTabAddr();
    const unsigned short memAddr = makeWord(*(addr + 1), *addr);

    return bcu->userMemoryPtr(memAddr);
}

uint8_t* AddrTablesBCU2::assocTable()
{
    const uint8_t* addr = (uint8_t*) &bcu->userEeprom->assocTabAddr();
    return bcu->userMemoryPtr(makeWord(*(addr + 1), *addr));
}

uint16_t AddrTablesBCU2::addrCount()
{
    const uint8_t* ptrAddrTable = addrTable();
    const uint16_t count = makeWord(*(ptrAddrTable + 1), *ptrAddrTable);
    return (count);
}
