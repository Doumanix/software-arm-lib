/*
 *  property_types.cpp - BCU 2 property types of EIB objects.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */

#include <sblib/utils.h>
#include <sblib/eib/property_types.h>
#include <sblib/eib/bcu_default.h>
#include <sblib/eib/userEepromBCU2.h>

#if defined(INCLUDE_SERIAL)
#   include <sblib/serial.h>
#endif

/**
 * PropertyDataType sizes in bytes
 */
const uint8_t propertySizes[] =
{
    1, 1, 1, 2, 2, 2, 3, 3, 4, 4,                                          // PDT_CONTROL -> PDT_UNSIGNED_LONG
    4, 8, 10, 3, 5, 8, 0,                                                  // PDT_FLOAT - >PDT_VARIABLE_LENGTH
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, // PDT_GENERIC_01 -> PDT_GENERIC_20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                          // PDT_RESERVED_0x25 -> PDT_RESERVED_0x2e
    0, 2, 6, 1, 1, 2, 1, 1,                                                // PDT_UTF_8 -> PDT_SCALING
    0, 0, 0, 0, 0,                                                         // PDT_RESERVED_0x37 -> PDT_RESERVED_0x3b
    0, 0, 0, 0                                                             // PDT_NE_VL -> PDT_ESCAPE
};


int PropertyDef::size() const
{
    extern const uint8_t propertySizes[];
    return propertySizes[control & PC_TYPE_MASK];
}

uint8_t* PropertyDef::valuePointer(BcuBase* bcu) const
{
    if (control & PC_POINTER)
    {
        const int offs = valAddr & PPT_OFFSET_MASK;

        switch (valAddr & PPT_MASK)
        {
            case PPT_USER_RAM:
                DB_PROPERTIES(serial.println("RAM"););
                return bcu->userRam->userRamData + offs; ///\todo check this was correct replaced or not
            case PPT_USER_EEPROM:
                DB_PROPERTIES(serial.println("EEPROM"););
                return ((BcuDefault*)bcu)->userEeprom->userEepromData + offs;
            case PPT_USER_EEPROM_FIELD:
                DB_PROPERTIES(serial.println("EEPROM field"););
                return ((BcuDefault*)bcu)->userEeprom->userEepromData
                       + static_cast<UserEepromBCU2*>(((BcuDefault*)bcu)->userEeprom)->fieldOffset(offs);
            default:
                fatalError(); // invalid property pointer type encountered
                break;
        }
    }

    return (uint8_t*) &valAddr;
}

Memory* PropertyDef::valueMemory(BcuBase* bcu) const
{
    Memory* memory = nullptr;

    if (control & PC_POINTER)
    {
        switch (valAddr & PPT_MASK)
        {
            case PPT_USER_RAM:
                memory = bcu->userRam;
                break;
            case PPT_USER_EEPROM:
            case PPT_USER_EEPROM_FIELD:
                memory = ((BcuDefault*)bcu)->userEeprom;
                break;
            default:
                fatalError(); // invalid property pointer type encountered
                break;
        }
    }

    return memory;
}

uint32_t PropertyDef::valueAddress(BcuBase* bcu) const
{
    const Memory* memory = valueMemory(bcu);
    uint32_t address = 0;

    if (memory)
    {
        address = memory->startAddr() + (valAddr & PPT_OFFSET_MASK);
    }

    return address;
}
