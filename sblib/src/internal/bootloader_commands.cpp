/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 3 as
 published by the Free Software Foundation.
 ---------------------------------------------------------------------------*/

#include "sblib/internal/bootloader_commands.h"
#include "sblib/interrupt.h"
#include "sblib/eib/apci.h"
#include "sblib/platform.h"

static_assert(sizeof(BootloaderDescriptor) % FLASH_RAM_BUFFER_ALIGNMENT == 0,
        "Check 4 byte alignment of BootloaderDescriptor, don't use packed!");


/**
 * Bootloader magic erase = FactoryResetWithoutIndividualAddress in calimero-core
 */
constexpr RestartPDUMasterReset BOOTLOADER_MAGIC_ERASE = T_MASTERRESET_FACTORY_WO_IA;

/**
 * Bootloader magic channel
 */
constexpr uint8_t BOOTLOADER_MAGIC_CHANNEL = 255;

/**
 * "Unique" BootloaderDescriptor identifier in RAM.
 */
constexpr uint32_t UID_BOOTLOADER_DESCRIPTOR = 0x5E1FB055;

/**
 * Pointer to BootloaderDescriptor in RAM.
 * @warning The bootLoaderDescriptor is stored in RAM at 0x100000C0.
 *          The application RAM must start behind the bootLoaderDescriptor to avoid overwriting it.
 * @def bootLoaderDescriptor
 */
#ifdef IAP_EMULATION
    uint32_t data;
    uint32_t * magicWord = &data; // I'm here for the unit-tests
    BootloaderDescriptor testBootLoaderDescriptor;
    BootloaderDescriptor* bootLoaderDescriptor = &testBootLoaderDescriptor;
#elif defined(SBLIB_PLATFORM_RP2350)
    // There is no Selfbus bootloader for the RP2350 yet; 0x10000000 is the flash (XIP) here.
    // Descriptor and legacy magic word are kept in RAM that is not initialized at startup,
    // so a future bootloader could read them after a reset.
    static uint32_t __uninitialized_ram(magicWordStorage);
    static BootloaderDescriptor __uninitialized_ram(bootLoaderDescriptorStorage);
    uint32_t* magicWord = &magicWordStorage;
    BootloaderDescriptor* bootLoaderDescriptor = &bootLoaderDescriptorStorage;
#else
    auto magicWord = reinterpret_cast<uint32_t*>(0x10000000UL);
    auto bootLoaderDescriptor = reinterpret_cast<BootloaderDescriptor*>(0x100000C0UL);
    extern uint8_t __base_RAM; // NOLINT(*-reserved-identifier)
#endif

bool isRAMReserved()
{
#if !defined(IAP_EMULATION) && !defined(SBLIB_PLATFORM_RP2350)
    const auto ramBaseAddress = reinterpret_cast<uintptr_t>(&__base_RAM);
    if (ramBaseAddress >= (reinterpret_cast<uintptr_t>(bootLoaderDescriptor) + BOOTLOADER_DESCRIPTOR_SIZE))
    {
        return true;
    }
    else
    {
        return false;
    }
#else
    return true;
#endif
}

void initBootloaderDescriptor(const BootState newBootState, const uint16_t physicalAddressToUse,
    const uint32_t programmingButton, const uint32_t applicationId, const uint32_t applicationVersion)
{
    if (!isRAMReserved())
    {
        return;
    }

    bootLoaderDescriptor->guid = UID_BOOTLOADER_DESCRIPTOR;
    bootLoaderDescriptor->bootState = newBootState;
    bootLoaderDescriptor->physicalAddress = physicalAddressToUse;
    bootLoaderDescriptor->programmingButton = programmingButton;
    bootLoaderDescriptor->applicationId = applicationId;
    bootLoaderDescriptor->applicationVersion = applicationVersion;
}

const BootloaderDescriptor* getBootloaderDescriptor()
{
    if (!isRAMReserved())
    {
        return nullptr;
    }

    if (bootLoaderDescriptor->guid == UID_BOOTLOADER_DESCRIPTOR)
    {
        return bootLoaderDescriptor;
    }

    if (*magicWord == UID_BOOTLOADER_DESCRIPTOR)
    {
        // Stay compatible with old applications which don't have the setting of the BootloaderDescriptor
        // Use old behavior values, e.g. 15.15.192 as bootloader address
        initBootloaderDescriptor(BootState::BootLoader, DEFAULT_BL_KNX_ADDRESS, 0, 0, 0);
        clearDeprecatedMagicWord();
        return bootLoaderDescriptor;
    }

    return nullptr;
}

void disableInterruptsAndSetLegacyMagicWord()
{
    noInterrupts();
    *magicWord = UID_BOOTLOADER_DESCRIPTOR;
}

void clearDeprecatedMagicWord()
{
    *magicWord = 0;
}

void setBootloaderNewBootState(const BootState newBootState)
{
    initBootloaderDescriptor(newBootState, bootLoaderDescriptor->physicalAddress,
        bootLoaderDescriptor->programmingButton, bootLoaderDescriptor->applicationId,
        bootLoaderDescriptor->applicationVersion);
}

void setBootloaderNewPhysicalAddress(const uint16_t newPhysicalAddress)
{
    initBootloaderDescriptor(bootLoaderDescriptor->bootState, newPhysicalAddress,
        bootLoaderDescriptor->programmingButton, bootLoaderDescriptor->applicationId,
        bootLoaderDescriptor->applicationVersion);
}

void setBootloaderNewProgrammingPin(const uint32_t newProgrammingPin)
{
    initBootloaderDescriptor(bootLoaderDescriptor->bootState, bootLoaderDescriptor->physicalAddress,
        newProgrammingPin, bootLoaderDescriptor->applicationId,
        bootLoaderDescriptor->applicationVersion);
}

BootloaderDescriptor* debugOnlyBootloaderDescriptor()
{
    return bootLoaderDescriptor;
}

bool checkApciForMagicWord(const uint8_t eraseCode, const uint8_t channelNumber)
{
    // The special version of APCI_MASTER_RESET_PDU used by Selfbus bootloader
    // restart with parameters, special meaning of erase=7 and channel=255 for bootloader mode
    return eraseCode == BOOTLOADER_MAGIC_ERASE && channelNumber == BOOTLOADER_MAGIC_CHANNEL;
}
