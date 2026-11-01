/*
 *  iap.cpp - In-application programming of the flash, RP2350 implementation.
 *
 *  Implements the LPC11xx IAP interface (sblib/internal/iap.h) with the Pico SDK flash
 *  functions for the external QSPI flash (W25Q, 4 KiB sectors, 256 byte pages).
 *
 *  - Erase and program only inside the persistent storage at the end of the flash
 *    (linker symbols __sblib_storage_start/__sblib_storage_end, see linker/rp2350_storage.ld).
 *    Everything else returns IAP_INVALID_SECTOR / IAP_DST_ADDR_NOT_MAPPED.
 *  - The flash can't be read while it is erased or programmed. Interrupts are disabled during
 *    the flash operation; the SDK flash functions run from RAM. Only core 0 is used.
 *    A sector erase takes typ. 45 ms (max. 400 ms), in which no interrupt is served.
 *  - The flash can only erase whole sectors. iapErasePage() therefore reads the sector, erases
 *    it and writes back the pages that are not erased. If the power fails in between, the other
 *    pages of the sector are lost (the LPC11xx erases single pages).
 *  - Erase and program are verified by reading back the flash.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include "sblib/internal/iap.h"
#include "sblib/platform.h"

#include <cstring>

// Must be included after sblib/platform.h, see there
#include <hardware/flash.h>
#include <hardware/structs/sysinfo.h>
#include <hardware/sync.h>
#include <pico/unique_id.h>

static_assert(FLASH_SECTOR_SIZE == 0x1000 && FLASH_PAGE_SIZE == 0x100, "sblib and Pico SDK flash geometry differ");

// Persistent storage, defined in linker/rp2350_storage.ld
extern "C" uint8_t __sblib_storage_start[];
extern "C" uint8_t __sblib_storage_end[];

namespace
{
    constexpr unsigned int PagesPerSector = FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE;

    // Buffer for the read-modify-write of a sector in iapErasePageRange()
    __attribute__((aligned(FLASH_RAM_BUFFER_ALIGNMENT))) uint8_t sectorBuffer[FLASH_SECTOR_SIZE];

    bool isInStorage(const uint8_t* address, const uint32_t size)
    {
        return size != 0 && address >= __sblib_storage_start && address < __sblib_storage_end &&
               size <= static_cast<uint32_t>(__sblib_storage_end - address);
    }

    bool isInFlash(const uint8_t* address)
    {
        return address >= FLASH_BASE_ADDRESS && address < FLASH_BASE_ADDRESS + iapFlashSize();
    }

    uint32_t flashOffset(const uint8_t* address)
    {
        return static_cast<uint32_t>(address - FLASH_BASE_ADDRESS);
    }

    bool isErased(const uint8_t* data, const uint32_t size)
    {
        for (uint32_t i = 0; i < size; ++i)
        {
            if (data[i] != 0xff)
            {
                return false;
            }
        }
        return true;
    }

    IAP_Status eraseRange(const uint8_t* address, const uint32_t size)
    {
        if ((flashOffset(address) % FLASH_SECTOR_SIZE) != 0 || (size % FLASH_SECTOR_SIZE) != 0)
        {
            return IAP_INVALID_SECTOR;
        }

        if (!isInStorage(address, size))
        {
            return IAP_INVALID_SECTOR;
        }

        const uint32_t irqState = save_and_disable_interrupts();
        flash_range_erase(flashOffset(address), size);
        restore_interrupts(irqState);

        if (isErased(address, size))
        {
            return IAP_SUCCESS;
        }
        return IAP_SECTOR_NOT_BLANK;
    }

    IAP_Status programRange(uint8_t* rom, const uint8_t* ram, const uint32_t size)
    {
        if ((flashOffset(rom) % FLASH_PAGE_SIZE) != 0)
        {
            return IAP_DST_ADDR_ERROR;
        }

        if ((reinterpret_cast<uintptr_t>(ram) % FLASH_RAM_BUFFER_ALIGNMENT) != 0 || isInFlash(ram))
        {
            return IAP_SRC_ADDR_ERROR; // the source can't be read from the flash while programming
        }

        if (size == 0 || (size % FLASH_PAGE_SIZE) != 0)
        {
            return IAP_COUNT_ERROR;
        }

        if (!isInStorage(rom, size))
        {
            return IAP_DST_ADDR_NOT_MAPPED;
        }

        const uint32_t irqState = save_and_disable_interrupts();
        flash_range_program(flashOffset(rom), ram, size);
        restore_interrupts(irqState);

        IAP_Status status = IAP_SUCCESS;
        if (memcmp(rom, ram, size) != 0)
        {
            status = IAP_COMPARE_ERROR;
        }
        return status;
    }

} // namespace

IAP_Status iapEraseSector(const unsigned int sector)
{
    return iapEraseSectorRange(sector, sector);
}

IAP_Status iapEraseSectorRange(const unsigned int startSector, const unsigned int endSector)
{
    if (endSector < startSector)
    {
        return IAP_INVALID_SECTOR;
    }

    return eraseRange(iapAddressOfSector(startSector), (endSector - startSector + 1) * FLASH_SECTOR_SIZE);
}

IAP_Status iapErasePage(const unsigned int pageNumber)
{
    return iapErasePageRange(pageNumber, pageNumber);
}

IAP_Status iapErasePageRange(const unsigned int startPageNumber, const unsigned int endPageNumber)
{
    if (endPageNumber < startPageNumber)
    {
        return IAP_INVALID_SECTOR;
    }

    const unsigned int startSector = startPageNumber / PagesPerSector;
    const unsigned int endSector = endPageNumber / PagesPerSector;

    for (unsigned int sector = startSector; sector <= endSector; ++sector)
    {
        uint8_t* sectorAddress = iapAddressOfSector(sector);
        if (!isInStorage(sectorAddress, FLASH_SECTOR_SIZE))
        {
            return IAP_INVALID_SECTOR;
        }

        // Keep the pages of the sector that are not erased
        memcpy(sectorBuffer, sectorAddress, FLASH_SECTOR_SIZE);
        const unsigned int firstPage = sector * PagesPerSector;
        for (unsigned int page = firstPage; page < firstPage + PagesPerSector; ++page)
        {
            if (page >= startPageNumber && page <= endPageNumber)
            {
                memset(sectorBuffer + (page - firstPage) * FLASH_PAGE_SIZE, 0xff, FLASH_PAGE_SIZE);
            }
        }

        IAP_Status rc = eraseRange(sectorAddress, FLASH_SECTOR_SIZE);
        if (rc != IAP_SUCCESS)
        {
            return rc;
        }

        // Write back the kept pages
        for (unsigned int idx = 0; idx < PagesPerSector; ++idx)
        {
            const uint8_t* pageData = sectorBuffer + idx * FLASH_PAGE_SIZE;
            if (!isErased(pageData, FLASH_PAGE_SIZE))
            {
                rc = programRange(sectorAddress + idx * FLASH_PAGE_SIZE, pageData, FLASH_PAGE_SIZE);
                if (rc != IAP_SUCCESS)
                {
                    return rc;
                }
            }
        }
    }

    return IAP_SUCCESS;
}

IAP_Status iapProgram(uint8_t* rom, const uint8_t* ram, const unsigned int size)
{
    return programRange(rom, ram, size);
}

IAP_Status iapReadUID(uint8_t* uid)
{
    // 64 bit unique chip id (from OTP on the RP2350), the remaining bytes are 0
    pico_unique_board_id_t boardId;
    pico_get_unique_board_id(&boardId);

    static_assert(PICO_UNIQUE_BOARD_ID_SIZE_BYTES <= IAP_UID_LENGTH, "unique id too long");
    memset(uid, 0, IAP_UID_LENGTH);
    memcpy(uid, boardId.id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
    return IAP_SUCCESS;
}

IAP_Status iapReadPartID(unsigned int* partId)
{
    // JEDEC manufacturer, part number and revision of the RP2350
    *partId = sysinfo_hw->chip_id;
    return IAP_SUCCESS;
}

unsigned int iapSectorOfAddress(const uint8_t* address)
{
    return static_cast<unsigned int>((address - FLASH_BASE_ADDRESS) / FLASH_SECTOR_SIZE);
}

unsigned int iapPageOfAddress(const uint8_t* address)
{
    return static_cast<unsigned int>((address - FLASH_BASE_ADDRESS) / FLASH_PAGE_SIZE);
}

uint8_t* iapAddressOfPage(const unsigned int page)
{
    return (page * FLASH_PAGE_SIZE) + FLASH_BASE_ADDRESS;
}

uint8_t* iapAddressOfSector(const unsigned int sector)
{
    return (sector * FLASH_SECTOR_SIZE) + FLASH_BASE_ADDRESS;
}

unsigned int iapFlashSize()
{
    // Flash size of the board (Pico SDK board header), same value as LENGTH(FLASH) in the linker script
    return PICO_FLASH_SIZE_BYTES;
}
