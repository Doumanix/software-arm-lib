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
     * @return The index of the address, -1 if not found. 0 if the table has count 0, which
     * accepts all group addresses (03_05_01 4.16.8.2.4); such an address has no association.
     *
     * @brief The address table starts with a 16 bit count followed by the
     * configured group addresses. The index of the first group address is 1.
     */
    int indexOfAddr(int addr) override;

    /**
     * Get the number of group addresses
     * @return The big endian 16 bit count from the first two bytes of the table
     *
     * @brief The SYSTEMB address table does not contain the individual address.
     */
    uint16_t addrCount() override;

private:
    SYSTEMB* bcu;
};

#endif /* SBLIB_KNX_ADDR_TABLES_SYSTEMB_H_ */
