/*
 *  addr_tables.h - BCU communication address tables.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *  Copyright (c) 2026 Doumanix <doumanix@gmx.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_KNX_ADDR_TABLES_SYSTEMB_H_
#define SBLIB_KNX_ADDR_TABLES_SYSTEMB_H_

#include <sblib/eib/addr_tablesMASK0701.h>

class SYSTEMB;

class AddrTablesSYSTEMB : public AddrTablesMASK0701
{
public:
    explicit AddrTablesSYSTEMB(SYSTEMB* bcuInstance) : AddrTablesMASK0701((MASK0701*)bcuInstance), bcu(bcuInstance) {}

    /**
     * Get the index of a group address in the address table.
     *
     * @param addr - the address to find.
     * @return The index of the address, -1 if not found.
     *
     * @brief The address table contains the configured group addresses and our
     * own physical address. This function skips the own physical address and
     * only scans the group addresses.
     */
    int indexOfAddr(int addr) override;

    /**
     * Get total number of address entries
     * @return The 16 bit count from the first two bytes of the table
     *
     * @brief SYSTEMB uses a 16 bit count; see ComObjects::firstObjectAddr().
     */
    uint16_t addrCount() override;

private:
    SYSTEMB* bcu;
};

#endif /* SBLIB_KNX_ADDR_TABLES_SYSTEMB_H_ */
