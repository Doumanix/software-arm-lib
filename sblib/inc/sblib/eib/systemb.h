/*
 *  systemb.h - BCU specific stuff.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_KNX_SYSTEMB_H_
#define SBLIB_KNX_SYSTEMB_H_

#include <sblib/eib/mask0701.h>
#include <sblib/eib/userEepromSYSTEMB.h>
#include <sblib/eib/userRamSYSTEMB.h>
#include <sblib/eib/com_objectsSYSTEMB.h>
#include <sblib/eib/addr_tablesSYSTEMB.h>
#include <sblib/eib/propertiesSYSTEMB.h>

/**
 * Class for controlling all BCU related things.
 *
 * In order to use the EIB bus, you need to call bcu.begin() once in your application's
 * setup() function.
 */
class SYSTEMB : public MASK0701
{
public:
    SYSTEMB();
    SYSTEMB(UserRamSYSTEMB* userRam, UserEepromSYSTEMB* userEeprom, ComObjectsSYSTEMB* comObjects, AddrTablesSYSTEMB* addrTables,
            PropertiesSYSTEMB* properties);

    uint16_t getMaskVersion() const override { return 0x7B0; }

protected:
    /** The SYSTEMB address table holds only group addresses, see AddrTablesSYSTEMB. */
    bool ownAddressInAddrTable() const override { return false; }
};

#endif /* SBLIB_KNX_SYSTEMB_H_ */
