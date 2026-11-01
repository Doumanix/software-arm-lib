/*
 * rp2350-bcu-types - link check of the BCU types BCU2, MASK0701, MASK0705 and SYSTEMB on the RP2350.
 *
 * One firmware per BCU type, selected by the macro BCU_TYPE_BCU2, BCU_TYPE_MASK0701,
 * BCU_TYPE_MASK0705 or BCU_TYPE_SYSTEMB (see CMakeLists.txt). Uses the main() of the sblib:
 * setup() creates the BCU and calls begin(), then bcu.loop() and loop() or loop_noapp().
 * No communication objects are used.
 *
 * MASK0701, MASK0705 and SYSTEMB also get a MemMapper. It uses the persistent storage at the
 * end of the flash (linker/rp2350_storage.ld) in front of the last sector, which holds the
 * UserEeprom, and maps 0x300 bytes behind the UserEeprom range.
 *
 * PIN_RUN blinks slowly while the application runs, fast otherwise.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#if defined(BCU_TYPE_BCU2)
#   include <sblib/eibBCU2.h>
#elif defined(BCU_TYPE_MASK0701)
#   include <sblib/eibMASK0701.h>
#elif defined(BCU_TYPE_MASK0705)
#   include <sblib/eibMASK0705.h>
#elif defined(BCU_TYPE_SYSTEMB)
#   include <sblib/eibSYSTEMB.h>
#else
#   error "No BCU type macro defined"
#endif

#include <sblib/digital_pin.h>
#include <sblib/io_pin_names.h>
#include <sblib/timeout.h>

#if !defined(BCU_TYPE_BCU2)
#   include <sblib/mem_mapper.h>
#   include <sblib/platform.h>
#endif

constexpr int Manufacturer = 0x4c; // Selfbus test manufacturer
constexpr int DeviceType = 0x0001;
constexpr int AppVersion = 1;

#if defined(BCU_TYPE_BCU2)
BCU2 bcu = BCU2();
#elif defined(BCU_TYPE_MASK0701)
MASK0701 bcu = MASK0701();
#elif defined(BCU_TYPE_MASK0705)
MASK0705 bcu = MASK0705();
#elif defined(BCU_TYPE_SYSTEMB)
SYSTEMB bcu = SYSTEMB();
#endif

static Timeout blinkTimeout;

#if !defined(BCU_TYPE_BCU2)
// Persistent storage, defined in linker/rp2350_storage.ld
extern "C" uint8_t __sblib_storage_start[];
extern "C" uint8_t __sblib_storage_end[];

constexpr uint32_t MappedSize = 0x300; // bytes mapped behind the UserEeprom range

/**
 * @brief Set up a MemMapper in the persistent storage in front of the UserEeprom sector.
 */
static void setupMemMapper()
{
    const uint32_t storageSize = static_cast<uint32_t>(__sblib_storage_end - __sblib_storage_start);
    if (storageSize < 2 * FLASH_SECTOR_SIZE)
    {
        fatalError(); // no room for a MemMapper beside the UserEeprom sector
    }

    const uint32_t base = static_cast<uint32_t>(__sblib_storage_start - FLASH_BASE_ADDRESS);
    static MemMapper memMapper(base, storageSize - FLASH_SECTOR_SIZE);
    bcu.setMemMapper(&memMapper);
    const uint32_t mappedStart = bcu.userEeprom->startAddr() + bcu.userEeprom->size();
    if (memMapper.addRange(mappedStart, MappedSize) != MemMapper::Error::Success)
    {
        fatalError();
    }
}
#endif

static void blink(const unsigned int periodMs)
{
    if (blinkTimeout.expired())
    {
        digitalWrite(PIN_RUN, !digitalRead(PIN_RUN));
        blinkTimeout.start(periodMs);
    }
}

BcuBase* setup()
{
    pinMode(PIN_RUN, OUTPUT);
    bcu.begin(Manufacturer, DeviceType, AppVersion);
#if !defined(BCU_TYPE_BCU2)
    setupMemMapper();
#endif
    blinkTimeout.start(1);
    return &bcu;
}

void loop()
{
    blink(1000);
}

void loop_noapp()
{
    blink(200);
}
