/*
 *  properties.h - BCU 2 properties of EIB objects.
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#ifndef SBLIB_KNX_PROPERTIES_SYSTEMB_H_
#define SBLIB_KNX_PROPERTIES_SYSTEMB_H_

#include <sblib/eib/propertiesMASK0701.h>
#include <sblib/eib/userEepromSYSTEMB.h>

class MASK0701;
class SYSTEMB;

/** Define a PropertyDef pointer to variable v in the userEeprom of SYSTEMB */
#define PD_USER_EEPROM_SYSTEMB_OFFSET(v) (UserEepromSYSTEMB::v + PPT_USER_EEPROM)

class PropertiesSYSTEMB : public PropertiesMASK0701
{
public:
    explicit PropertiesSYSTEMB(SYSTEMB* bcuInstance) : PropertiesMASK0701((MASK0701*)bcuInstance), bcu(bcuInstance) {}

    LoadState handleAllocAbsDataSegment(const int objectIdx, const uint8_t* payLoad, const int len) override;
    LoadState handleDataRelativeAllocation(const int objectIdx, const uint8_t* payLoad, const int len) override;
    LoadState handleLoadStateMachine(const int objectIdx, const uint8_t* data, const int len) override;
    virtual uint16_t crc16(const uint8_t* ptr, int len);
    int loadProperty(int objectIdx, const uint8_t* data, int len) override;
    bool propertyValueReadTelegram(int objectIdx, PropertyID propertyId, int count, int start, uint8_t* sendBuffer) override;
    bool propertyValueWriteTelegram(int objectIdx, PropertyID propertyId, int count, int start, uint8_t* sendBuffer) override;

protected:
    const PropertyDef* const* propertiesTab() const override { return propertiesTabSystemB; }

private:
    SYSTEMB* bcu;

    /**
     * The properties of the address table object (index 1)
     * See KNX Spec 03_05_03 3.9.3.3 and 06 Profiles/Annex A.2 (07B0h)
     *
     * PID_TABLE_REFERENCE is a PDT_UNSIGNED_LONG (20 bit address, upper bits 0, KNX 03_05_01 4.2.7), but the EEPROM
     * only holds the 16 bit address, see propertyValueReadTelegram().
     */
    const PropertyDef addrTabProps[5] =
    {
        {PID_OBJECT_TYPE, PDT_UNSIGNED_INT, OT_ADDR_TABLE},
        {PID_LOAD_STATE_CONTROL, PDT_CONTROL | PC_WRITABLE | PC_POINTER,
         PD_USER_EEPROM_FIELD_OFFSET(loadStateOffset + OT_ADDR_TABLE)},
        {PID_TABLE_REFERENCE, PDT_UNSIGNED_LONG | PC_POINTER, PD_USER_EEPROM_SYSTEMB_OFFSET(addrTabAddrOffset)},
        {PID_MCB_TABLE, PDT_GENERIC_08 | PC_POINTER | PC_WRITABLE, PD_USER_EEPROM_SYSTEMB_OFFSET(addrTabMcbOffset)},
        PROPERTY_DEF_TABLE_END
    };

    /** The properties of the association table object (index 2) */
    const PropertyDef assocTabProps[5] =
    {
        {PID_OBJECT_TYPE, PDT_UNSIGNED_INT, OT_ASSOC_TABLE},
        {PID_LOAD_STATE_CONTROL, PDT_CONTROL | PC_WRITABLE | PC_POINTER,
         PD_USER_EEPROM_FIELD_OFFSET(loadStateOffset + OT_ASSOC_TABLE)},
        {PID_TABLE_REFERENCE, PDT_UNSIGNED_LONG | PC_POINTER, PD_USER_EEPROM_SYSTEMB_OFFSET(assocTabAddrOffset)},
        {PID_MCB_TABLE, PDT_GENERIC_08 | PC_POINTER | PC_WRITABLE, PD_USER_EEPROM_SYSTEMB_OFFSET(assocTabMcbOffset)},
        PROPERTY_DEF_TABLE_END
    };

    /** The properties of the group object table object (index 3) */
    const PropertyDef groupObjectTabProps[5] =
    {
        {PID_OBJECT_TYPE, PDT_UNSIGNED_INT, OT_GROUP_OBJECT_TABLE},
        {PID_LOAD_STATE_CONTROL, PDT_CONTROL | PC_WRITABLE | PC_POINTER, PD_USER_EEPROM_FIELD_OFFSET(loadStateOffset + 3)},
        {PID_TABLE_REFERENCE, PDT_UNSIGNED_LONG | PC_POINTER, PD_USER_EEPROM_FIELD_OFFSET(commsTabAddrOffset)},
        {PID_MCB_TABLE, PDT_GENERIC_08 | PC_POINTER | PC_WRITABLE, PD_USER_EEPROM_SYSTEMB_OFFSET(commsTabMcbOffset)},
        PROPERTY_DEF_TABLE_END
    };

    /** The properties of the application program 1 object (index 4) */
    const PropertyDef appProgram1Props[8] =
    {
        {PID_OBJECT_TYPE, PDT_UNSIGNED_INT, OT_APPLICATION},
        {PID_LOAD_STATE_CONTROL, PDT_CONTROL | PC_WRITABLE | PC_POINTER, PD_USER_EEPROM_FIELD_OFFSET(loadStateOffset + 4)},
        {PID_RUN_STATE_CONTROL, PDT_UNSIGNED_CHAR | PC_POINTER, PD_USER_RAM_OFFSET(_runStateOffset)},
        {PID_TABLE_REFERENCE, PDT_UNSIGNED_LONG | PC_POINTER, PD_USER_EEPROM_FIELD_OFFSET(eibObjAddrOffset)},
        {PID_PROGRAM_VERSION, PDT_GENERIC_05 | PC_WRITABLE | PC_POINTER,
         PD_USER_EEPROM_SYSTEMB_OFFSET(eibObjVerOffset)},
        {PID_PEI_TYPE, PDT_UNSIGNED_CHAR | PC_WRITABLE | PC_POINTER, PD_USER_EEPROM_OFFSET(appPeiTypeOffset)},
        {PID_MCB_TABLE, PDT_GENERIC_08 | PC_POINTER | PC_WRITABLE, PD_USER_EEPROM_SYSTEMB_OFFSET(eibObjMcbOffset)},
        PROPERTY_DEF_TABLE_END
    };

    /** The properties of the application program 2 (interface program) object (index 5) */
    const PropertyDef appProgram2Props[7] =
    {
        {PID_OBJECT_TYPE, PDT_UNSIGNED_INT, OT_INTERFACE_PROGRAM},
        {PID_LOAD_STATE_CONTROL, PDT_CONTROL | PC_WRITABLE | PC_POINTER, PD_USER_EEPROM_FIELD_OFFSET(loadStateOffset + 5)},
        {PID_TABLE_REFERENCE, PDT_UNSIGNED_LONG | PC_POINTER, PD_USER_EEPROM_FIELD_OFFSET(commsSeg0AddrOffset)},
        {PID_PROGRAM_VERSION, PDT_GENERIC_05 | PC_WRITABLE | PC_POINTER,
         PD_USER_EEPROM_SYSTEMB_OFFSET(commsSeg0VerOffset)},
        {PID_PEI_TYPE, PDT_UNSIGNED_CHAR | PC_WRITABLE | PC_POINTER,
         PD_USER_EEPROM_SYSTEMB_OFFSET(commsSeg0PeiTypeOffset)},
        {PID_MCB_TABLE, PDT_GENERIC_08 | PC_POINTER | PC_WRITABLE, PD_USER_EEPROM_SYSTEMB_OFFSET(commsSeg0McbOffset)},
        PROPERTY_DEF_TABLE_END
    };

    /**
     * The interface objects of SYSTEM B (mask 07B0h). ETS addresses them by fixed index without checking the
     * object type, see KNX Spec 03_05_03 3.9.3.3.
     */
    const PropertyDef* const propertiesTabSystemB[NUM_PROP_OBJECTS] =
    {
        deviceObjectProps,    //!> Interface Object 0, device
        addrTabProps,         //!> Interface Object 1, address table
        assocTabProps,        //!> Interface Object 2, association table
        groupObjectTabProps,  //!> Interface Object 3, group object table
        appProgram1Props,     //!> Interface Object 4, application program 1
        appProgram2Props      //!> Interface Object 5, application program 2
    };
};
#endif /* SBLIB_KNX_PROPERTIES_SYSTEMB_H_ */
