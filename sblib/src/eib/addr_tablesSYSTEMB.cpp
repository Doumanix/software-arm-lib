/*
 * addr_tablesSYSTEMB.cpp
 *
 *  Created on: 20.11.2021
 *      Author: dridders
 *      Author: Doumanix <doumanix@gmx.de>
 */

#include <sblib/eib/addr_tablesSYSTEMB.h>
#include <sblib/eib/systemb.h>
#include <sblib/bits.h>

int AddrTablesSYSTEMB::indexOfAddr(const int addr)
{
    uint8_t* tab = addrTable();
    int num = 0;

    if (tab)
    {
        num = (tab[0] << 8) + tab[1];
        if (num == 0)
        {
            return 0; // count 0 accepts all group addresses, none of them has a table index
        }
    }
    tab += 2;

    const int addrHigh = addr >> 8;
    const int addrLow = addr & 255;

    for (int i = 1; i <= num; ++i, tab += 2)
    {
        if (tab[0] == addrHigh && tab[1] == addrLow)
            return i;
    }

    return -1;
}

uint16_t AddrTablesSYSTEMB::addrCount()
{
    const uint8_t* ptrAddrTable = addrTable();
    const uint16_t count = makeWord(*ptrAddrTable, *(ptrAddrTable + 1)); // big endian, without the individual address
    return (count);
}
