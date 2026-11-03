/*
 * com_objectsSYSTEMB.cpp
 *
 *  Created on: 19.11.2021
 *      Author: dridders
 *      Author: Doumanix <doumanix@gmx.de>
 */

#include <sblib/eib/com_objectsSYSTEMB.h>
#include <sblib/eib/apci.h>
#include <sblib/eib/systemb.h>
#include <sblib/bits.h>
#include <sblib/eib/typesSYSTEMB.h>

int ComObjectsSYSTEMB::objectCount()
{
    // Spec: Resources 4.18.6 Group Object Table - Realization Type 7, the number of group objects has 2 octets
    const uint8_t* table = objectConfigTable();
    if (table == nullptr)
    {
        return (0);
    }
    return (makeWord(table[0], table[1]));
}

int ComObjectsSYSTEMB::objectSize(const int objno)
{
    // KNX spec v2.1 3/5/1 p. 178 (section 4.12.5.2.4.1.4)
    // The size of the object types 6...20 in bytes
    const uint8_t objectTypeSizes[15] = {1, 1, 2, 3, 4, 6, 8, 10, 14, 5, 7, 9, 11, 12, 13};

    const int type = objectType(objno);
    if (type < BIT_7)
        return 1;
    if (type < 21)
        return objectTypeSizes[type - BIT_7];
    if (type < 255)
        return (type - 6);
    return 252;
}

uint8_t* ComObjectsSYSTEMB::objectValuePtr(const int objno)
{
    int ramAddr = bcu->userRam->startAddr() + 2;
    for (int i = 0; i < objno; i++)
        ramAddr += objectSize(i);
    return ((BcuDefault*)bcu)->userMemoryPtr(ramAddr);
}

/*
 *  A Group read/write Telegram on the bus or a
 *  Telegram transmitt-request (object read/write)from the app was received
 *
 *  From bus: Called from bus::ProcessTelegram function triggered by the BCUbase-loop function.
 *  From app: Called from sendGroupWriteTelegram or sendGroupReadTelegram
 *
 *  Scan the AssociationTable for the received GrpAdr for a valid association to a local object.
 *
 *  In order to avoid an endless loop by updating we need to skip the triggering object from the app.
 *  In case of a received bus request the triggering objectnumber was set to an invalid value (-1).
 *
 *
 *  @return void
 *
 */
void ComObjectsSYSTEMB::processGroupTelegram(const uint16_t addr, const int apci, uint8_t* tel, const int trg_objno)
{
    //
    // Spec: Resources 4.11.4 Group Object Association Table - Realization Type 6
    //
    const ComConfig* configTab = &objectConfig(0);
    const int numObjs = objectCount();
    const uint8_t* assocTab = bcu->addrTables->assocTable();
    const int endAssoc = 2 + makeWord(assocTab[0], assocTab[1]) * 4; // length field has 2 octets and each entry has 4 octets on SYSTEM B

    // Convert the group address into the index into the group address table
    const int gapos = bcu->addrTables->indexOfAddr(addr);
    if (gapos < 0)
        return;

    // Loop over all entries in the association table, as one group address
    // could be assigned to multiple com-objects.
    for (int idx = 2; idx < endAssoc; idx += 4)
    {
        // Check if grp-address index in assoc table matches the dest grp address index
        int gadest = makeWord(assocTab[idx], assocTab[idx + 1]); // get destination group address index
        if (gapos == gadest) // We found an association for our addr
        {
            // The association table holds the ASAP, which starts at 1, the object number of the application at 0
            const int objno = makeWord(assocTab[idx + 2], assocTab[idx + 3]) - 1;
            if ((objno < 0) || (objno >= numObjs))
                continue; // ASAP not in the group object table
            if (objno == trg_objno)
                continue; // no update of the object triggered by the app

            const int objConf = configTab[objno].config;

            if (apci == APCI_GROUP_VALUE_WRITE_PDU || apci == APCI_GROUP_VALUE_RESPONSE_PDU)
            {
                // Check if communication and write are enabled
                if ((objConf & COMCONF_WRITE_COMM) == COMCONF_WRITE_COMM)
                    processGroupWriteTelegram(objno, tel); // set update flag and update value of object
            }
            else if (apci == APCI_GROUP_VALUE_READ_PDU)
            {
                // Check if communication and read are enabled
                if ((objConf & COMCONF_READ_COMM) == COMCONF_READ_COMM)
                    // we received read-request from bus - so send response back and search for more associations
                    sendGroupWriteTelegram(objno, addr, true);
            }
        }
    }
}

int ComObjectsSYSTEMB::firstObjectAddr(const int objno) const
{
    // Spec: Resources 4.11.4 Group Object Association Table - Realization Type 6
    const uint8_t* assocTab = bcu->addrTables->assocTable();
    const uint8_t* addrTab = bcu->addrTables->addrTable();
    if ((assocTab == nullptr) || (addrTab == nullptr))
    {
        return (0);
    }

    const int addrCount = bcu->addrTables->addrCount();
    const int endAssoc = 2 + makeWord(assocTab[0], assocTab[1]) * 4; // 2 octets length, 4 octets per entry

    for (int idx = 2; idx < endAssoc; idx += 4)
    {
        // The association table holds the ASAP, which starts at 1, the object number of the application at 0
        if (makeWord(assocTab[idx + 2], assocTab[idx + 3]) != objno + 1)
        {
            continue;
        }

        // The address table has a 2 octet length field and no individual address, index 1 is the first group address
        const int addressTablePos = makeWord(assocTab[idx], assocTab[idx + 1]);
        if ((addressTablePos < 1) || (addressTablePos > addrCount))
        {
            continue;
        }

        const uint8_t* addr = addrTab + (addressTablePos << 1);
        return (makeWord(addr[0], addr[1]));
    }
    return (0);
}

uint8_t* ComObjectsSYSTEMB::objectConfigTable()
{
    const uint8_t* addr = (uint8_t*) &((SYSTEMB*)bcu)->userEeprom->commsTabAddr();
    return ((BcuDefault*)bcu)->userMemoryPtr(makeWord(*(addr + 1), *addr));
}

uint8_t* ComObjectsSYSTEMB::objectFlagsTable()
{
    const uint8_t* configTable = objectConfigTable();
    if (le_ptr == LITTLE_ENDIAN)
        return ((BcuDefault*)bcu)->userMemoryPtr(makeWord(configTable[2], configTable[1]));

    return ((BcuDefault*)bcu)->userMemoryPtr(makeWord(configTable[1], configTable[2]));
}

inline const ComConfig& ComObjectsSYSTEMB::objectConfig(const int objno)
{
    return (*(const ComConfigSYSTEMB*) (objectConfigTable() + 2 + objno * sizeof(ComConfigSYSTEMB))).baseConfig;
}
