/*
 * Tests of the location of the system interface object fields (load states, table addresses, serial number, order
 * info, MCBs, versions) in the user EEPROM of BCU2, BIM M112 (0x0701, 0x0705) and SYSTEM B (Issue #112).
 * A BIM M112 application starts with the group address table at 0x4000 and the association table at 0x4200, so the
 * fields must not lie within these tables.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include <protocol.h>
#include "prot_systemb_telegrams.h"

#include <sblib/eib/bcu2.h>
#include <sblib/eib/userEepromBCU2.h>
#include <cstring>

#include <catch.hpp> // If possible, include catch.hpp as last header

#define MANUFACTURER (0x0004)
#define DEVICE       (0x2060)
#define VERSION      (0x01)

#define OWN_KNX_ADDRESS (0xA000) // own address 10.0.0

namespace
{
    constexpr uint32_t groupTableAddress = 0x4000;
    constexpr uint32_t assocTableAddress = 0x4200;
    constexpr uint32_t assocTableLength = 129; //!< number of entries and 64 pairs of group address and com object index

    constexpr uint32_t loadStateCount = 8;
    constexpr uint32_t tableCount = 5;
    constexpr uint32_t orderLength = 10;

    // sets of values for the fields, the second one is written after the tables
    const uint8_t loadStates[2][loadStateCount] = {{1, 1, 1, 1, 0, 1, 0, 1}, {1, 1, 1, 1, 1, 1, 1, 1}};
    const uint16_t tableAddresses[2][tableCount] = {{0x4000, 0x4200, 0x4400, 0x4600, 0x4800},
                                                    {0x4010, 0x4210, 0x4410, 0x4610, 0x4810}};
    const uint8_t serials[2][6] = {{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}, {0x11, 0x12, 0x13, 0x14, 0x15, 0x16}};
    const uint8_t orders[2][orderLength] = {{0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9},
                                            {0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9}};
    const uint8_t orderInfos[2][orderLength] = {{0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9},
                                                {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9}};
    const uint8_t serviceControls[2] = {0x5A, 0xA5};

    uint32_t fieldsBase = 0; //!< expected address of the load states of the BCU under test

    UserEepromBCU2* eeprom()
    {
        return static_cast<UserEepromBCU2*>(bcuUnderTest->userEeprom);
    }

    uint32_t addressOf(const void* pointer)
    {
        const auto offset = static_cast<const uint8_t*>(pointer) - eeprom()->userEepromData;
        return eeprom()->startAddr() + static_cast<uint32_t>(offset);
    }

    void setFields(const uint32_t set)
    {
        UserEepromBCU2* e = eeprom();
        for (uint32_t idx = 0; idx < loadStateCount; idx++)
        {
            REQUIRE(e->setLoadState(idx, loadStates[set][idx]));
        }
        REQUIRE(e->setAddrTabAddr(tableAddresses[set][0]));
        REQUIRE(e->setAssocTabAddr(tableAddresses[set][1]));
        REQUIRE(e->setCommsTabAddr(tableAddresses[set][2]));
        REQUIRE(e->setEibObjAddr(tableAddresses[set][3]));
        REQUIRE(e->setCommsSeg0Addr(tableAddresses[set][4]));
        REQUIRE(e->setSerial(serials[set], sizeof(serials[set])));
        REQUIRE(e->setOrder(orders[set], orderLength));
        for (uint32_t idx = 0; idx < orderLength; idx++)
        {
            REQUIRE(e->setOrderInfo(idx, orderInfos[set][idx]));
        }
        e->serviceControl() = serviceControls[set];
    }

    void prepareFields([[maybe_unused]] void* state, [[maybe_unused]] unsigned int var)
    {
        setFields(0);
    }

    void rewriteFields([[maybe_unused]] void* state, [[maybe_unused]] unsigned int var)
    {
        setFields(1);
    }

    // the fields have the values of the set var
    void checkFields([[maybe_unused]] void* state, unsigned int set)
    {
        UserEepromBCU2* e = eeprom();
        for (uint32_t idx = 0; idx < loadStateCount; idx++)
        {
            INFO("load state " << idx);
            CHECK(e->loadState()[idx] == loadStates[set][idx]);
        }
        CHECK(e->addrTabAddr() == tableAddresses[set][0]);
        CHECK(e->assocTabAddr() == tableAddresses[set][1]);
        CHECK(e->commsTabAddr() == tableAddresses[set][2]);
        CHECK(e->eibObjAddr() == tableAddresses[set][3]);
        CHECK(e->commsSeg0Addr() == tableAddresses[set][4]);
        CHECK(memcmp(e->serial(), serials[set], sizeof(serials[set])) == 0);
        CHECK(memcmp(e->order(), orders[set], orderLength) == 0);
        CHECK(memcmp(e->orderInfo(), orderInfos[set], orderLength) == 0);
        CHECK(e->serviceControl() == serviceControls[set]);
    }

    // the group address table and the association table with 64 entries are unchanged
    void checkTables([[maybe_unused]] void* state, [[maybe_unused]] unsigned int var)
    {
        const uint8_t* data = eeprom()->userEepromData;
        const uint32_t start = eeprom()->startAddr();
        const uint8_t groupTable[8] = {0x04, 0xA0, 0x00, 0x08, 0x01, 0x08, 0x02, 0x10};
        CHECK(memcmp(data + (groupTableAddress - start), groupTable, sizeof(groupTable)) == 0);

        const uint8_t* assoc = data + (assocTableAddress - start);
        REQUIRE(assoc[0] == 64);
        for (uint32_t entry = 0; entry < 64; entry++)
        {
            INFO("association " << entry);
            CHECK(assoc[1 + 2 * entry] == entry + 1);
            CHECK(assoc[2 + 2 * entry] == entry);
        }
        for (uint32_t idx = assocTableLength; idx < 136; idx++)
        {
            CHECK(assoc[idx] == 0);
        }
    }

    // the getters return the fields at the expected addresses, var is 1 for the table addresses of the BCU2 layout
    void checkGetters([[maybe_unused]] void* state, unsigned int bcu2Layout)
    {
        UserEepromBCU2* e = eeprom();
        CHECK(addressOf(e->loadState()) == fieldsBase);
        CHECK(addressOf(&e->commsTabAddr()) == fieldsBase + 0x0C);
        CHECK(addressOf(&e->commsSeg0Addr()) == fieldsBase + 0x0E);
        CHECK(addressOf(&e->commsSeg1Addr()) == fieldsBase + 0x10);
        CHECK(addressOf(&e->eibObjAddr()) == fieldsBase + 0x12);
        CHECK(addressOf(&e->eibObjCount()) == fieldsBase + 0x14);
        CHECK(addressOf(&e->padding1()) == fieldsBase + 0x15);
        CHECK(addressOf(&e->serviceControl()) == fieldsBase + 0x16);
        CHECK(addressOf(&e->padding2()) == fieldsBase + 0x18);
        CHECK(addressOf(e->serial()) == fieldsBase + 0x1A);
        CHECK(addressOf(e->order()) == fieldsBase + 0x20);
        CHECK(addressOf(e->orderInfo()) == fieldsBase + 0x2A);
        if (bcu2Layout != 0)
        {
            CHECK(addressOf(&e->addrTabAddr()) == fieldsBase + 0x08);
            CHECK(addressOf(&e->assocTabAddr()) == fieldsBase + 0x0A);
        }
        else
        {
            // SYSTEM B has its own offsets for these two
            CHECK(addressOf(&e->addrTabAddr()) == 0x3321);
            CHECK(addressOf(&e->assocTabAddr()) == 0x3323);
        }
    }

    struct PropertyRow
    {
        uint8_t objectIdx;
        PropertyID propertyId;
        uint32_t address; //!< offset from the load states, an absolute address for the rows of SYSTEM B
    };

    // fields of the system interface objects, they follow the location of the load states in all classes
    const PropertyRow commonRows[] =
    {
        {OT_DEVICE, PID_LOAD_STATE_CONTROL, 0x00},
        {OT_DEVICE, PID_SERVICE_CONTROL, 0x16},
        {OT_DEVICE, PID_SERIAL_NUMBER, 0x1A},
        {OT_DEVICE, PID_HARDWARE_TYPE, 0x20},
        {OT_DEVICE, PID_ORDER_INFO, 0x2A},
        {OT_ADDR_TABLE, PID_LOAD_STATE_CONTROL, 0x01},
        {OT_ASSOC_TABLE, PID_LOAD_STATE_CONTROL, 0x02},
        {OT_APPLICATION, PID_LOAD_STATE_CONTROL, 0x03},
        {OT_APPLICATION, PID_TABLE_REFERENCE, 0x0C},
        {OT_INTERFACE_PROGRAM, PID_LOAD_STATE_CONTROL, 0x04},
        {OT_INTERFACE_PROGRAM, PID_TABLE_REFERENCE, 0x12},
        {5, PID_LOAD_STATE_CONTROL, 0x05},
        {5, PID_TABLE_REFERENCE, 0x0E},
    };

    // fields that SYSTEM B does not take over from the BCU2 layout
    const PropertyRow bcu2Rows[] =
    {
        {OT_ADDR_TABLE, PID_TABLE_REFERENCE, 0x08},
        {OT_ADDR_TABLE, PID_MCB_TABLE, 0x34},
        {OT_ASSOC_TABLE, PID_TABLE_REFERENCE, 0x0A},
        {OT_ASSOC_TABLE, PID_MCB_TABLE, 0x3C},
        {OT_APPLICATION, PID_MCB_TABLE, 0x44},
        {OT_INTERFACE_PROGRAM, PID_MCB_TABLE, 0x4C},
        {OT_INTERFACE_PROGRAM, PID_PROGRAM_VERSION, 0x5C},
        {5, PID_PROGRAM_VERSION, 0x61},
    };

    const PropertyRow systemBRows[] =
    {
        {OT_ADDR_TABLE, PID_TABLE_REFERENCE, 0x3321},
        {OT_ADDR_TABLE, PID_MCB_TABLE, 0x334D},
        {OT_ASSOC_TABLE, PID_TABLE_REFERENCE, 0x3323},
        {OT_ASSOC_TABLE, PID_MCB_TABLE, 0x3355},
    };

    void checkRow(const PropertyRow& row, const uint32_t base)
    {
        INFO("object " << static_cast<int>(row.objectIdx) << " property " << static_cast<int>(row.propertyId));
        PropertiesBCU2* properties = static_cast<BCU2*>(bcuUnderTest)->properties;
        const PropertyDef* def = properties->propertyDef(row.objectIdx, row.propertyId);
        REQUIRE(def != nullptr);
        CHECK(addressOf(def->valuePointer(bcuUnderTest)) == base + row.address);
    }

    // the property tables point to the fields, var is 1 for the BCU2 layout of all fields and 0 for SYSTEM B
    void checkProperties([[maybe_unused]] void* state, unsigned int bcu2Layout)
    {
        for (const PropertyRow& row : commonRows)
        {
            checkRow(row, fieldsBase);
        }
        if (bcu2Layout != 0)
        {
            for (const PropertyRow& row : bcu2Rows)
            {
                checkRow(row, fieldsBase);
            }
        }
        else
        {
            for (const PropertyRow& row : systemBRows)
            {
                checkRow(row, 0);
            }
        }
    }

    // the ETS writes the tables with the verify mode, a write is answered with the written data
    void tcSetup(Telegram* tel, uint16_t telCount)
    {
        bcuUnderTest->setOwnAddress(OWN_KNX_ADDRESS);
        telegramPreparation(bcuUnderTest, tel, telCount);
        bcuUnderTest->userRam->deviceControl() |= DEVCTRL_MEM_AUTO_RESPONSE;
    }

    // the fields are set, then the ETS writes the group address table and the association table with 64 entries over
    // the area of the old location (0x4270-0x42D5), the fields are checked, written again and the tables are checked
    // the memory addresses in the telegrams are BCU1 addresses, telegramPreparation() moves them to the start of the
    // EEPROM: 0x0200 is 0x4000 (group address table), 0x0400 is 0x4200 (association table), 0x0470 is 0x4270
    Telegram tableTelegrams[] =
    {
        {LOOP, 0, 0, 0, prepareFields, {0}},
        TEL_CONNECT,
        TEL_MEMORY_WRITE_8(0, 0x02, 0x00, 0x04, 0xA0, 0x00, 0x08, 0x01, 0x08, 0x02, 0x10),
        TEL_MEMORY_WRITE_8(1, 0x04, 0x00, 0x40, 0x01, 0x00, 0x02, 0x01, 0x03, 0x02, 0x04),
        TEL_MEMORY_WRITE_8(2, 0x04, 0x08, 0x03, 0x05, 0x04, 0x06, 0x05, 0x07, 0x06, 0x08),
        TEL_MEMORY_WRITE_8(3, 0x04, 0x10, 0x07, 0x09, 0x08, 0x0A, 0x09, 0x0B, 0x0A, 0x0C),
        TEL_MEMORY_WRITE_8(4, 0x04, 0x18, 0x0B, 0x0D, 0x0C, 0x0E, 0x0D, 0x0F, 0x0E, 0x10),
        TEL_MEMORY_WRITE_8(5, 0x04, 0x20, 0x0F, 0x11, 0x10, 0x12, 0x11, 0x13, 0x12, 0x14),
        TEL_MEMORY_WRITE_8(6, 0x04, 0x28, 0x13, 0x15, 0x14, 0x16, 0x15, 0x17, 0x16, 0x18),
        TEL_MEMORY_WRITE_8(7, 0x04, 0x30, 0x17, 0x19, 0x18, 0x1A, 0x19, 0x1B, 0x1A, 0x1C),
        TEL_MEMORY_WRITE_8(8, 0x04, 0x38, 0x1B, 0x1D, 0x1C, 0x1E, 0x1D, 0x1F, 0x1E, 0x20),
        TEL_MEMORY_WRITE_8(9, 0x04, 0x40, 0x1F, 0x21, 0x20, 0x22, 0x21, 0x23, 0x22, 0x24),
        TEL_MEMORY_WRITE_8(10, 0x04, 0x48, 0x23, 0x25, 0x24, 0x26, 0x25, 0x27, 0x26, 0x28),
        TEL_MEMORY_WRITE_8(11, 0x04, 0x50, 0x27, 0x29, 0x28, 0x2A, 0x29, 0x2B, 0x2A, 0x2C),
        TEL_MEMORY_WRITE_8(12, 0x04, 0x58, 0x2B, 0x2D, 0x2C, 0x2E, 0x2D, 0x2F, 0x2E, 0x30),
        TEL_MEMORY_WRITE_8(13, 0x04, 0x60, 0x2F, 0x31, 0x30, 0x32, 0x31, 0x33, 0x32, 0x34),
        TEL_MEMORY_WRITE_8(14, 0x04, 0x68, 0x33, 0x35, 0x34, 0x36, 0x35, 0x37, 0x36, 0x38),
        TEL_MEMORY_WRITE_8(15, 0x04, 0x70, 0x37, 0x39, 0x38, 0x3A, 0x39, 0x3B, 0x3A, 0x3C),
        TEL_MEMORY_WRITE_8(16, 0x04, 0x78, 0x3B, 0x3D, 0x3C, 0x3E, 0x3D, 0x3F, 0x3E, 0x40),
        TEL_MEMORY_WRITE_8(17, 0x04, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
        TEL_MEMORY_READ_8(18, 0x04, 0x70, 0x37, 0x39, 0x38, 0x3A, 0x39, 0x3B, 0x3A, 0x3C),
        TEL_MEMORY_READ_8(19, 0x04, 0x78, 0x3B, 0x3D, 0x3C, 0x3E, 0x3D, 0x3F, 0x3E, 0x40),
        TEL_MEMORY_READ_8(20, 0x04, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
        {LOOP, 0, 0, 0, checkFields, {0}},
        {LOOP, 0, 0, 0, checkTables, {0}},
        {LOOP, 0, 0, 0, rewriteFields, {0}},
        {LOOP, 0, 0, 1, checkFields, {0}},
        {LOOP, 0, 0, 0, checkTables, {0}},
        {LOOP, 0, 0, 1, checkGetters, {0}},
        {LOOP, 0, 0, 1, checkProperties, {0}},
        {END, 0, 0, 0, nullptr, {0}}
    };

    Telegram bcu2LayoutTelegrams[] =
    {
        {LOOP, 0, 0, 1, checkGetters, {0}},
        {LOOP, 0, 0, 1, checkProperties, {0}},
        {END, 0, 0, 0, nullptr, {0}}
    };

    Telegram systemBTelegrams[] =
    {
        {LOOP, 0, 0, 0, checkGetters, {0}},
        {LOOP, 0, 0, 0, checkProperties, {0}},
        {END, 0, 0, 0, nullptr, {0}}
    };

    Test_Case tableTestCase = {"fields and tables of a BIM M112", MANUFACTURER, DEVICE, VERSION, 0, nullptr, tcSetup,
                               nullptr, nullptr, nullptr, tableTelegrams};
    Test_Case bcu2TestCase = {"fields of BCU2", MANUFACTURER, DEVICE, VERSION, 0, nullptr, tcSetup,
                              nullptr, nullptr, nullptr, bcu2LayoutTelegrams};
    Test_Case systemBTestCase = {"fields of SYSTEM B", MANUFACTURER, DEVICE, VERSION, 0, nullptr, tcSetup,
                                 nullptr, nullptr, nullptr, systemBTelegrams};

} // namespace

TEST_CASE("The fields of the system interface objects of a BIM M112 do not overlap the tables",
          "[protocol][mask0701-offsets]")
{
    fieldsBase = 0x3F70; // 0x470 - 0x300 + 0x3F00
    executeTest(BCU_0701, &tableTestCase);
    executeTest(BCU_0705, &tableTestCase);
}

TEST_CASE("The fields of the system interface objects of BCU2 and SYSTEM B keep their location",
          "[protocol][mask0701-offsets]")
{
    fieldsBase = 0x470;
    executeTest(BCU_2, &bcu2TestCase);

    fieldsBase = 0x3670; // 0x3300 + 0x370
    executeTest(BCU_07B0, &systemBTestCase);
}
