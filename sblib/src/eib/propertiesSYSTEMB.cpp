/*
 *  properties.cpp - properties of EIB objects for all BCUs except BCU 1
 *
 *  Copyright (c) 2014 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */

#include <sblib/core.h>
#include <sblib/eib/bcu1.h>

#include <cstring> // for memcpy in propertyValueReadTelegram()

#include <sblib/eib/propertiesSYSTEMB.h>
#include <sblib/eib/systemb.h>
#include <sblib/eib/bus.h>
#include <sblib/eib/knx_npdu.h>

/**
 * NOT IMPLEMENTED!
 * should handle Additional Load Control: LoadEvent: AllocAbsDataSeg (segment type 0) <LdCtrlAbsSegment>
 *
 * DMP_LoadStateMachineWrite_RCo_Mem (APCI_MEMORY_WRITE_PDU) See KNX Spec. 3/5/2 3.28.2 p.109  (deprecated) or
 * DMP_LoadStateMachineWrite_RCo_IO (ApciPropertyValueWrite) See KNX Spec. 3/5/2 3.28.3.4 p.114
 *
 * @param objectIdx - interface object index (ObjectType)
 * @param payLoad - the telegram data bytes
 * @param len - the length of the payLoad
 *
 * @return new LoadState of the interface object objectIdx
 */
LoadState PropertiesSYSTEMB::handleAllocAbsDataSegment([[maybe_unused]] const int objectIdx, const uint8_t* payLoad,
    [[maybe_unused]] const int len)
{
    /*
     *  from KNX Spec. 06 Profiles 4.2.9 RAM cleared
     *  RAM to be cleared by the Management Client during download of an application program:
     *
     *  0x00CE-0x00DF   BCU1, RAM
     *
     *  0x00BD-0x00DF   BCU2, Zero-Page-RAM
     *  0x0972-0x0989   BCU2, High RAM
     *
     *  0x0700-0x????   BIM112, RAM
     *
     *  //XXX sblib ignores this
     */

    // payLoad[0..1] : start address        (SSSS)
    // payLoad[2..3] : length (LLLL)        (EEEE-SSSS+1) --> EEEE = SSSS+LLLL-1 // KNX Spec not really clear about that
    // payLoad[4]    : access attributes    (bit 0-3 write access, bit 4-7 read access level)
    // payLoad[5]    : memory type          (bit 0-2,  1=zero page RAM, 2=RAM, 3=EEPROM, bit 3-7 reserved)
    // payLoad[6]    : memory attributes    (bit 0-6 reserved, bit 7=0: checksum control disabled
    // payLoad[7]    : reserved
    LoadState newLoadState = LS_ERROR;
    const unsigned int absDataSegmentStartAddress = makeWord(payLoad[0], payLoad[1]);
    const unsigned int absDataSegmentLength = makeWord(payLoad[2], payLoad[3]);
    const unsigned int absDataSegmentEndAddress = absDataSegmentStartAddress + absDataSegmentLength - 1;
    const auto memType = MemoryType(payLoad[5] & 0x07); // take only bits 0..2

    DB_PROPERTIES(
        serial.print("handleAllocAbsDataSegment only partly implemented! ");
        printObjectIdx(objectIdx);
        serial.print(" ");
        printData(payLoad, len);
        serial.println();
        serial.print("  --> start: 0x", absDataSegmentStartAddress, HEX, 4);
        serial.print(" length: 0x", absDataSegmentLength, HEX, 4);
        serial.print(" end: 0x", absDataSegmentEndAddress, HEX, 4);
        serial.print(" access: 0x", payLoad[4], HEX, 2);
        serial.print(" memtype: 0x", payLoad[5], HEX, 2);
        serial.print(" checksum: ", (payLoad[6] & 0x80) >> 7, DEC, 1);
        serial.println(" attrib: 0x", (payLoad[6]), HEX, 2);
    );

    bool memStartValid = false;
    bool memEndValid = false;

    // FIXME this check should be done in class BCU, and if its not available it should be allocated
    switch (memType)
    {
        // there is no real reason to differentiate between memory types,
        // important is, that we can reach it.
        case MT_ZERO_PAGE_RAM:
        case MT_RAM:
        case MT_EEPROM:
        {
            // check against user ram
            memStartValid = bcu->userRam->inRange(absDataSegmentStartAddress);
            memEndValid = bcu->userRam->inRange(absDataSegmentEndAddress);
            // check against user EEPROM
            memStartValid |= bcu->userEeprom->inRange(absDataSegmentStartAddress);
            memEndValid |= bcu->userEeprom->inRange(absDataSegmentEndAddress);
            // check against MemMapper
            MemMapper* bcuMemMapper = bcu->getMemMapper();
            memStartValid |= (bcuMemMapper != nullptr) && (bcuMemMapper->isMapped(absDataSegmentStartAddress));
            memEndValid |= (bcuMemMapper != nullptr) && (bcuMemMapper->isMapped(absDataSegmentEndAddress));

            newLoadState = LS_LOADING;
            break;
        }
        default:
        {
            newLoadState = LS_ERROR;
            break;
        }
    }

    if (!memStartValid)
    {
        DB_PROPERTIES(serial.println("  ------> invalid start: 0x", absDataSegmentStartAddress, HEX, 4); serial.println(););
        newLoadState = LS_ERROR;
    }
    if (!memEndValid)
    {
        DB_PROPERTIES(serial.println("  ------> invalid end: 0x", absDataSegmentEndAddress, HEX, 4); serial.println(););
        newLoadState = LS_ERROR;
    }
    DB_PROPERTIES(serial.println(););
    return newLoadState;
}

/**
 * PARTIALLY IMPLEMENTED for System_B !
 * should handle Additional Load Control: LoadEvent: DataRelativeAllocation (segment type 0x0B)
 *
 * DMP_LoadStateMachineWrite_RCo_Mem (APCI_MEMORY_WRITE_PDU) See KNX Spec. 3/5/2 3.28.2 p.109  (deprecated) or
 * DMP_LoadStateMachineWrite_RCo_IO (ApciPropertyValueWrite) See KNX Spec. 3/5/2 3.28.3.4 p.116
 *
 * @param objectIdx - interface object index (ObjectType)
 * @param payLoad - the telegram data bytes
 * @param len - the length of the payLoad
 *
 * @return new LoadState of the interface object objectIdx
 */
LoadState PropertiesSYSTEMB::handleDataRelativeAllocation(const int objectIdx, const uint8_t* payLoad,
    [[maybe_unused]] const int len)
{
    // payLoad[0..3] : requested memory size
    // payLoad[4]    : mode (0x00)
    // payLoad[5]    : fill (0x00)
    // payLoad[6..7] : reserved
    DB_PROPERTIES(
        serial.print("handleDataRelativeAllocation PARTIALLY IMPLEMENTED for System_B! ");
        printObjectIdx(objectIdx);
        serial.print(" ");
        printData(payLoad, len);
        serial.println();
        serial.print("  --> requested memory size: 0x", (payLoad[0] << 24) | (payLoad[1] << 16) | (payLoad[2] << 8) | payLoad[3], HEX, 8);
        serial.print(" mode: 0x", payLoad[4], HEX, 2);
        serial.println(" fill: 0x", payLoad[5], HEX, 2);
        serial.println();
    );

    const unsigned int reqMemSize = ((payLoad[0] << 24) | (payLoad[1] << 16) | (payLoad[2] << 8) | payLoad[3]);
    UserEepromSYSTEMB* userEeprom = static_cast<UserEepromSYSTEMB*>(bcu->userEeprom);

    const uint16_t tableAddress[] = {
        userEeprom->addrTabAddr(), userEeprom->assocTabAddr(), userEeprom->commsTabAddr(),
        userEeprom->eibObjAddr(), userEeprom->commsSeg0Addr()
    };

    uint16_t virtMemAddr = 0x3A9E; ///\todo get rid of magic number USER_EEPROM_START + USER_EEPROM_SIZE
    for (int i = 0; i < 5; i++)
    {
        if ((tableAddress[i] != 0) && (tableAddress[i] < virtMemAddr))
            virtMemAddr = tableAddress[i];
    }

    const uint16_t newAddress = virtMemAddr - reqMemSize;
    switch (objectIdx - 1)
    {
        case 0:
            userEeprom->setAddrTabAddr(newAddress);
            break;
        case 1:
            userEeprom->setAssocTabAddr(newAddress);
            break;
        case 2:
            userEeprom->setCommsTabAddr(newAddress);
            break;
        case 3:
            userEeprom->setEibObjAddr(newAddress);
            break;
        case 4:
            userEeprom->setCommsSeg0Addr(newAddress);
            break;
    }
    userEeprom->setMcbSize(objectIdx - 1, reqMemSize);

    // Mode = 0x01 => fill allocated memory
    if (payLoad[4] > 0)
    {
        const uint8_t fillByte = payLoad[5];
        bcu->userEeprom->fill(virtMemAddr, reqMemSize, fillByte);
    }

    return LS_LOADING;
}

/**
 * Load State Machine of SYSTEM B, Realisation Type 1 (property based)
 * See KNX Spec 03_05_01 4.23.2.3.3 Table 94 "Load State Machine transition table" and 03_05_03 3.5 Table 4
 *
 * Next state by load event (rows) and current state (columns). The states Unloading and LoadCompleting are never
 * stored, a stored value above Error is treated like Error.
 *
 *                        Unloaded  Loaded   Loading  Error
 *  No Operation (00h)    Unloaded  Loaded   Loading  Error
 *  Start Loading (01h)   Loading   Loading  Loading  Error
 *  Load Completed (02h)  Unloaded  Loaded   Loaded   Error
 *  Additional (03h)      Unloaded  Error    Loading  Error    (Loading, if the segment is accepted)
 *  Unload (04h)          Unloaded  Unloaded Unloaded Unloaded
 *
 * Unknown load events (03_05_01 4.23.2.3.2) and too short data leave the state unchanged, the response holds the
 * unchanged state. Additional Load Controls in the state Loading return LS_LOADCOMPLETING, loadProperty()
 * evaluates the segment.
 *
 * @param objectIdx - interface object index
 * @param data - the Load Control data, data[0] is the load event
 * @param len - the length of data
 *
 * @return new LoadState of the interface object objectIdx
 */
LoadState PropertiesSYSTEMB::handleLoadStateMachine(const int objectIdx, const uint8_t* data, const int len)
{
    LoadState currentState = LS_ERROR;
    const uint8_t storedState = bcu->userEeprom->loadState()[objectIdx];
    if (storedState <= LS_ERROR)
    {
        currentState = LoadState(storedState);
    }

    if (len < PID_LOAD_STATE_CONTROL_LENGTH)
    {
        return currentState;
    }

    LoadState newLoadState = currentState;
    const auto loadControl = LoadControl(data[0] & 0x0F);
    switch (loadControl)
    {
        case LC_NO_OPERATION:
        {
            break;
        }
        case LC_START_LOADING:
        {
            if (currentState != LS_ERROR)
            {
                newLoadState = LS_LOADING;
            }
            break;
        }
        case LC_LOAD_COMPLETED:
        {
            if (currentState == LS_LOADING)
            {
                newLoadState = LS_LOADED;
            }
            break;
        }
        case LC_ADDITIONAL_LOAD_CONTROLS:
        {
            if (currentState == LS_LOADING)
            {
                newLoadState = LS_LOADCOMPLETING; // internal value for loadProperty(), the segment is evaluated there
            }
            else if (currentState == LS_LOADED)
            {
                newLoadState = LS_ERROR;
            }
            break;
        }
        case LC_UNLOAD:
        {
            newLoadState = LS_UNLOADED;
            break;
        }
        default:
        {
            DB_PROPERTIES(printObjectIdx(objectIdx); serial.println(" unknown loadcontrol=0x", data[0], HEX, 2); serial.println(););
            break;
        }
    }

    DB_PROPERTIES(serial.print("handleLoadStateMachine: "); printObjectIdx(objectIdx); serial.print(" "); printLoadState(newLoadState);serial.println(); serial.flush(););
    return newLoadState;
}

uint16_t PropertiesSYSTEMB::crc16(uint8_t* ptr, int len)
{
    //int len = 9;
    //uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,  0x39};
    // uint8_t* ptr = &data[0];
    uint16_t crc = 0x1D0F;
    while (--len >= 0)
    {
        crc = crc ^ (uint16_t)*ptr++ << 8;
        char i = 8;
        do
        {
            if (crc & 0x8000)
                crc = crc << 1 ^ 0x1021;
            else
                crc = crc << 1;
        }
        while (--i);
    }
    return crc;
}

int PropertiesSYSTEMB::loadProperty(const int objectIdx, const uint8_t* data, int len)
{
    // See KNX 3/5/2, 3.27 DM_LoadStateMachineWrite
    // See KNX 6/6 Profiles, p. 101 for load states
    // See BCU2 help System Architecture > Load Procedure

    // DMP_LoadStateMachineWrite_RCo_IO length of data must be 10  (ApciPropertyValueWrite) See KNX Spec. 3/5/2 3.28.3 p.115
    // DMP_LoadStateMachineWrite_RCo_Mem length of data must be 11 (APCI_MEMORY_WRITE_PDU) See KNX Spec. 3/5/2 3.28.2 p.109  (deprecated)
    if ((len > DMP_LOADSTATE_MACHINE_WRITE_RCO_MEM_LENGTH) || (len < DMP_LOADSTATE_MACHINE_WRITE_RCO_IO_LENGTH))
    {
        DB_PROPERTIES(serial.print("loadProperty: "); printObjectIdx(objectIdx); serial.print(" invalid "); printData(data, len););
        return handleLoadStateMachine(objectIdx, data, 0); // the state stays unchanged
    }

    const LoadState newLoadState = handleLoadStateMachine(objectIdx, data, len);

    // When memory load is complete, calculate the crc 16 and store it in the mcb
    if (newLoadState == LS_LOADED)
    {
        const PropertyDef* def = propertyDef(objectIdx, PID_TABLE_REFERENCE);
        const uint8_t* valuePtr = def->valuePointer(bcu);
        const uint16_t virtMemStart = ((valuePtr[1] << 8) + valuePtr[0]);
        uint8_t* memStart = bcu->userMemoryPtr(virtMemStart); ///\todo read from flash, see P0 step 3

        def = propertyDef(objectIdx, PID_MCB_TABLE);
        const uint8_t* mcbPtr = def->valuePointer(bcu);
        const uint16_t memSize = (mcbPtr[2] << 8) + mcbPtr[3];

        if (memStart != nullptr)
        {
            const uint16_t crc = crc16(memStart, memSize);
            static_cast<UserEepromSYSTEMB*>(bcu->userEeprom)->setMcbCrc(objectIdx - 1, crc);
        }

        return newLoadState;
    }

    if (newLoadState == LS_UNLOADED)
    {
        // clear table reference address on unload
        const PropertyDef* def = propertyDef(objectIdx, PID_TABLE_REFERENCE);
        uint8_t* valuePtr = def->valuePointer(bcu);
        valuePtr[1] = 0;
        valuePtr[0] = 0;
        return newLoadState;
    }

    if (newLoadState != LS_LOADCOMPLETING)
    {
        return newLoadState;
    }

    //
    // from here newLoadState == LS_LOADCOMPLETING
    //
    // Additional Load Control: LoadEvent: segmentType
    //
    const int segmentType = data[1]; // this is in both versions of DMP_LoadStateMachineWrite_RCo always the 2.octet

    uint8_t payloadOffset;
    const bool apciPropertyValueWrite = (len == DMP_LOADSTATE_MACHINE_WRITE_RCO_IO_LENGTH); // determine the realization type of DMP_LoadStateMachineWrite_RCo
    if (apciPropertyValueWrite)
        payloadOffset = DMP_LOADSTATE_MACHINE_WRITE_RCO_IO_PAYLOAD_OFFSET; // offset for RCo_IO mode, where the real data for Additional Load Controls starts
    else
        payloadOffset = DMP_LOADSTATE_MACHINE_WRITE_RCO_MEM_PAYLOAD_OFFSET; // offset for RCo_Mem mode, where the real data for Additional Load Controls starts


    const uint8_t* payload = data + payloadOffset; // "move" to start of payload data
    len -= payloadOffset;                       // reduce len by payloadOffset

    switch (segmentType)
    {
        case ST_ALLOC_ABS_DATA_SEG: // Allocate absolute code/data segment (LdCtrlAbsSegment)
            return handleAllocAbsDataSegment(objectIdx, payload, len);

        case ST_ALLOC_ABS_STACK_SEG: // ignored, Allocate absolute stack segment
            return handleAllocAbsStackSeg(objectIdx, payload, len);

        case ST_ALLOC_ABS_TASK_SEG: // Segment control record (LdCtrlTaskSegment)
            return handleAllocAbsTaskSegment(objectIdx, payload, len);

        case ST_TASK_PTR: // Task pointer (ignored)
            return handleTaskPtr(objectIdx, payload, len);

        case ST_RELATIVE_ALLOCATION: // relative allocation
            return handleRelativeAllocation(objectIdx, payload, len);

        case ST_DATA_RELATIVE_ALLOCATION: // data relative allocation (ignored)
            return handleDataRelativeAllocation(objectIdx, payload, len);

        default:
            DB_PROPERTIES(serial.println(" FATAL ERROR."););
            IF_DEBUG(fatalError());
            return LS_ERROR; // reply: Error
    }
    return LS_LOADING;
}

bool PropertiesSYSTEMB::propertyValueReadTelegram(const int objectIdx, const PropertyID propertyId, const int count, int start, uint8_t* sendBuffer)
{
    // DB_PROPERTIES(serial.print("propertyValueReadTelegram: "); printObjectIdx(objectIdx); serial.print(" "); printPropertyID(propertyId);serial.println(););
    const PropertyDef* def = propertyDef(objectIdx, propertyId);
    if (!def)
        return false; // not found

    const auto type = (PropertyDataType)(def->control & PC_TYPE_MASK);
    const uint8_t* valuePtr = def->valuePointer(bcu);

    --start;
    const int size = def->size();
    int len = count * size;
    if (len > PROPERTY_MAX_VALUE_LENGTH)
        return false; // length error

    if (type == PDT_UNSIGNED_LONG)
    {
        // the 4 byte value is backed by a 16 bit address in the EEPROM, the upper 16 bit are 0 (KNX 03_05_01 4.2.7)
        if ((start != 0) || (count != 1))
            return false; // only one element

        sendBuffer[telegramTpciIndex(sendBuffer) + 6] = 0;
        sendBuffer[telegramTpciIndex(sendBuffer) + 7] = 0;
        reverseCopy(sendBuffer + (telegramTpciIndex(sendBuffer) + 8), valuePtr, 2);
    }
    else if (type < PDT_CHAR_BLOCK)
        reverseCopy(sendBuffer + (telegramTpciIndex(sendBuffer) + 6), valuePtr + start * size, len);
    else
        memcpy(sendBuffer + (telegramTpciIndex(sendBuffer) + 6), valuePtr + start * size, len);

    telegramAddApduLength(sendBuffer, len);

    return true;
}

bool PropertiesSYSTEMB::propertyValueWriteTelegram(const int objectIdx, const PropertyID propertyId, const int count, int start, uint8_t* sendBuffer)
{
    const PropertyDef* def = propertyDef(objectIdx, propertyId);
    if (!def)
        return false; // not found

    if (!(def->control & PC_WRITABLE))
    {
        DB_PROPERTIES(printObjectIdx(objectIdx); serial.println(" "); printPropertyID(propertyId); serial.println(" not writable!"););
        return false; // not writable
    }

    const PropertyDataType type = def->type();
    uint8_t* valuePtr = def->valuePointer(bcu);
    Memory* memory = def->valueMemory(bcu);

    // A_PropertyValue_Write has the TPCI in octet 6 of a standard and in octet 7 of an extended frame
    const int dataIndex = telegramTpciIndex(bcu->bus->telegram) + 6;
    const uint8_t* data = bcu->bus->telegram + dataIndex;
    int len;

    if (type == PDT_CONTROL)
    {
        len = bcu->bus->telegramLen - (dataIndex + 1);
        const int state = loadProperty(objectIdx, data, len);
        bcu->userEeprom->setLoadState(objectIdx, state);
        sendBuffer[telegramTpciIndex(sendBuffer) + 6] = state;
        len = 1;
    }
    else
    {
        --start;
        const int size = def->size();
        len = count * size;
        if ((len > PROPERTY_MAX_VALUE_LENGTH) || (len > (bcu->bus->telegramLen - (dataIndex + 1))))
        {
            return false; // data does not fit into the telegram
        }

        DB_PROPERTIES(serial.print("propertyValueWriteTelegram: "); printObjectIdx(objectIdx); serial.print(" "); printPropertyID(propertyId); serial.println(););
        if (type >= PDT_CHAR_BLOCK) // byte arrays are stored in telegram order, like in propertyValueReadTelegram()
        {
            if (memory)
            {
                memory->write(def->valueAddress(bcu) + start * size, data, len);
            }
            else
            {
                memcpy(valuePtr + start * size, data, len);
            }
            memcpy(sendBuffer + (telegramTpciIndex(sendBuffer) + 6), valuePtr + start * size, len);
        }
        else
        {
            if (memory)
            {
                const uint32_t address = def->valueAddress(bcu) + start * size;
                for (int i = 0; i < len; ++i)
                {
                    memory->setUInt8(address + i, data[len - 1 - i]);
                }
            }
            else
            {
                reverseCopy(valuePtr + start * size, data, len);
            }
            reverseCopy(sendBuffer + (telegramTpciIndex(sendBuffer) + 6), valuePtr + start * size, len);
        }
        if (def->isEepromPointer())
            bcu->userEeprom->modified(true);
    }

    telegramAddApduLength(sendBuffer, len);
    return true;
}
