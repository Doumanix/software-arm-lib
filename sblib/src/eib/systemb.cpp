/*
 *  systemb.cpp - EIB bus coupling unit.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */

#include <sblib/eib/systemb.h>

SYSTEMB::SYSTEMB() : SYSTEMB(new UserRamSYSTEMB(), new UserEepromSYSTEMB(), new ComObjectsSYSTEMB(this), new AddrTablesSYSTEMB(this), new PropertiesSYSTEMB(this))
{}

SYSTEMB::SYSTEMB(UserRamSYSTEMB* userRam, UserEepromSYSTEMB* userEeprom, ComObjectsSYSTEMB* comObjects, AddrTablesSYSTEMB* addrTables, PropertiesSYSTEMB* properties) :
    MASK0701(userRam, userEeprom, comObjects, addrTables, properties)
{}

bool SYSTEMB::applicationRunning() const
{
    if (!enabled)
    {
        return false;
    }

    // the load states are indexed by the interface object index, the application program 2 (object 5) is not part of
    // the load procedure "Load ap1"
    const uint8_t* loadState = userEeprom->loadState();
    return ((userRam->runState() == 1) &&
            (loadState[1] == LS_LOADED) &&  // address table object
            (loadState[2] == LS_LOADED) &&  // association table object
            (loadState[3] == LS_LOADED) &&  // group object table object
            (loadState[4] == LS_LOADED));   // application program 1 object
}
