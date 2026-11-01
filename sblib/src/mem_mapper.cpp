/*
 * mem_mapper.cpp
 *
 *  Created on: Aug 16, 2015
 *      Author: Deti Fliegl <deti@fliegl.de>
 *      Author: Doumanix <doumanix@gmx.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 *
 */

#include <sblib/mem_mapper.h>
#include <sblib/internal/iap.h>
#include <sblib/utils.h>
#include <sys/param.h>
#include <cstring>

MemMapper::MemMapper(const uint32_t flashBase, const uint32_t flashSize, const bool autoAddPage) :
    allocationTablePtr(FLASH_BASE_ADDRESS + flashBase),
    allocationTablePageNumber(iapPageOfAddress(this->allocationTablePtr)),
    flashSize(flashSize),
    flashSizePages(flashSize / FLASH_PAGE_SIZE),
    autoAddPage(autoAddPage),
    ramBuffer{},
    writePage(0),
    lastAllocated(0),
    endianess(LITTLE_ENDIAN),
    flashMemModified(false),
    allocTableModified(false)
{
    verifyAllocTable();
}

void MemMapper::verifyAllocTable() const
{
    // The entries are addressed by their byte offset: with one byte entries (LPC11xx) this stays a
    // plain byte access, so neither the on-flash format nor the optimized code changes.
    uint8_t zeroCounter = 0;
    for (uint32_t i = 0; i < allocTableSize; i++)
    {
        if (reinterpret_cast<const AllocEntryType&>(allocationTablePtr[i * AllocEntryBytes]) != 0)
        {
            continue;
        }

        zeroCounter++;
        if (zeroCounter > 1)
        {
            // We found at least two zeros -> assume corrupted allocTable
            clearAllocTable();
            break;
        }
    }
}

void MemMapper::clearAllocTable() const
{
    // Stage the cleared alloc table in ramBuffer, discarding any pending data page
    flashMemModified = false;
    writePage = allocationTablePageNumber;
    memset(ramBuffer, InvalidAllocTableByte, ramBufferSize);
    allocTableModified = true;
    static_cast<void>(doFlashAllocTable()); // static cast just to avoid compiler warning about unused return value
}

void MemMapper::writeAllocTableEntry(const uint8_t index, const AllocEntryType value) const
{
    if (readAllocTableEntry(index) == value)
    {
        return;
    }
    // Load the alloc table into ramBuffer if it is not already there
    if (writePage != allocationTablePageNumber)
    {
        static_cast<void>(doFlashWriteTable()); // Flush any pending data page changes first
        writePage = allocationTablePageNumber;  // Set writePage to the alloc table page
        memcpy(ramBuffer, allocationTablePtr, ramBufferSize); // Copy alloc table from flash into ramBuffer
    }
    reinterpret_cast<AllocEntryType&>(ramBuffer[index * AllocEntryBytes]) = value ^ AllocEntryXorMask;
    allocTableModified = true;
}

MemMapper::AllocEntryType MemMapper::readAllocTableEntry(const uint8_t index) const
{
    if (writePage == allocationTablePageNumber)
    {
        // Alloc table is currently staged in ramBuffer, read from there
        return reinterpret_cast<const AllocEntryType&>(ramBuffer[index * AllocEntryBytes]) ^ AllocEntryXorMask;
    }

    return reinterpret_cast<const AllocEntryType&>(allocationTablePtr[index * AllocEntryBytes]) ^ AllocEntryXorMask;
}

void MemMapper::writeToFlashPage(const uint8_t* buffer, const uint32_t pageNumber)
{
    if (iapErasePage(pageNumber) != IAP_SUCCESS)
    {
        fatalError();
    }
    if (iapProgram(iapAddressOfPage(pageNumber), buffer, FLASH_PAGE_SIZE) != IAP_SUCCESS)
    {
        fatalError();
    }
}

bool MemMapper::doFlashAllocTable() const
{
    if (!allocTableModified)
    {
        return false;
    }

    if (writePage != allocationTablePageNumber)
    {
        return false;
    }

    // ramBuffer holds the modified alloc table
    writeToFlashPage(ramBuffer, writePage);
    allocTableModified = false;
    writePage = 0; // ramBuffer no longer holds a valid page
    return true;
}

bool MemMapper::doFlashWriteTable() const
{
    if (!flashMemModified)
    {
        return false;
    }

    if (writePage == allocationTablePageNumber)
    {
        return false;
    }

    writeToFlashPage(ramBuffer, writePage);
    flashMemModified = false;
    return true;
}

int32_t MemMapper::doFlash() const
{
    int result = FlashedNothing;
    // Flush alloc table first. It uses ramBuffer so it must be done first
    if (doFlashAllocTable())
    {
        result = FlashedAllocationTable;
    }
    if (doFlashWriteTable())
    {
        result |= FlashedWriteBuffer;
    }
    return result;
}

MemMapper::Error MemMapper::allocatePage(const uint32_t virtPage)
{
    if (lastAllocated == 0)
    {
        for (uint32_t i = 0; i < allocTableSize; i++)
        {
            const uint32_t entry = readAllocTableEntry(i);
            if (entry > lastAllocated)
            {
                lastAllocated = entry;
            }
        }
    }

    if (lastAllocated == (allocationTablePageNumber + flashSizePages - 1))
    {
        return Error::OutOfMemory; // we are out of memory
    }

    uint32_t newPage;
    if (lastAllocated == 0)
    {
        newPage = allocationTablePageNumber + 1;
    }
    else
    {
        newPage = lastAllocated + 1;
    }
    lastAllocated = newPage;

    // Stage the alloc table update in ramBuffer and commit it to flash
    writeAllocTableEntry(virtPage, newPage);
    static_cast<void>(doFlashAllocTable()); // flushes ramBuffer (alloc table) to flash

    // Now ramBuffer is free for the new data page
    writePage = newPage;
    memset(ramBuffer, 0, ramBufferSize);
    return Error::Success;
}

MemMapper::Error MemMapper::addRange(const uint32_t virtAddress, const uint32_t length)
{
    // Check that length is non zero
    if (length == 0)
    {
        return Error::InvalidLength;
    }

    // Check that length is a multiple of flash page size
    if ((length & (FLASH_PAGE_SIZE - 1)) != 0)
    {
        return Error::InvalidLength;
    }

    // Check that address is aligned with flash page size
    if (virtAddress & (FLASH_PAGE_SIZE - 1))
    {
        return Error::InvalidAddress;
    }

    const uint32_t virtPage = virtAddress >> 8;
    if (virtPage > allocTableSize - 1)
    {
        return Error::InvalidAddress;
    }

    const uint32_t pages = length >> 8;

    // The whole range must fit into the allocation table, not only its first page. The start page
    // is at most allocTableSize - 1 here, so the remaining capacity cannot underflow; comparing
    // the page count against it keeps the check free of an addition that could overflow.
    if (pages > (allocTableSize - virtPage))
    {
        return Error::InvalidAddress;
    }

    for (uint32_t page = virtPage; page < (pages + virtPage); page++)
    {
        const AllocEntryType flashPageNum = readAllocTableEntry(page);
        if (flashPageNum != 0)
        {
            continue; // page already allocated
        }

        // not yet allocated in flash memory
        const Error result = allocatePage(page);
        if (result != Error::Success)
        {
            return result;
        }
    }
    static_cast<void>(doFlash());
    return Error::Success;
}

MemMapper::Error MemMapper::getFlashPageNum(const uint32_t virtAddress, uint32_t* flashPageNumber) const
{
    const uint32_t virtPage = virtAddress >> 8;
    if (virtPage >= allocTableSize)
    {
        return Error::InvalidAddress;
    }
    *flashPageNumber = readAllocTableEntry(virtPage);
    return Error::Success;
}

uint32_t MemMapper::virtualAddressToIndex(const uint32_t virtAddress)
{
    return virtAddress & 0xff;
}

MemMapper::Error MemMapper::writeMem(const uint32_t virtAddress, const uint8_t data)
{
    uint32_t flashPageNum;
    Error result = getFlashPageNum(virtAddress, &flashPageNum);
    if (result != Error::Success)
    {
        return result;
    }

    if (writePage != flashPageNum)
    {
        static_cast<void>(doFlash());
        writePage = flashPageNum;
        if (writePage != 0)
        {
            // swap flash page into write buffer
            memcpy(ramBuffer, iapAddressOfPage(writePage), ramBufferSize);
        }
    }

    if (flashPageNum == 0)
    {
        // not yet allocated in flash memory
        if (autoAddPage)
        {
            result = allocatePage(virtAddress >> 8);
            if (result != Error::Success)
            {
                return result;
            }
        }
    }
    ramBuffer[virtualAddressToIndex(virtAddress)] = data;
    flashMemModified = true;

    return Error::Success;
}

MemMapper::Error MemMapper::writeMemPtr(const uint32_t virtAddress, uint8_t* data, const uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
    {
        const Error result = writeMem(virtAddress + i, data[i]);
        if (result != Error::Success)
        {
            return result;
        }
    }
    return Error::Success;
}

MemMapper::Error MemMapper::readMem(const uint32_t virtAddress, uint8_t& data, const bool forceFlash) const
{
    uint32_t flashPageNum;
    const Error result = getFlashPageNum(virtAddress, &flashPageNum);

    if (result != Error::Success)
    {
        data = 0x00;
        return result;
    }
    if (forceFlash)
    {
        static_cast<void>(doFlash());
    }
    if (flashPageNum == 0)
    {
        data = 0x00;
        return Error::NotMapped;
    }

    if ((flashPageNum == writePage) && !forceFlash)
    {
        data = ramBuffer[virtualAddressToIndex(virtAddress)];
    }
    else
    {
        data = iapAddressOfPage(flashPageNum)[virtualAddressToIndex(virtAddress)];
    }
    return Error::Success;
}

MemMapper::Error MemMapper::readMemPtr(const uint32_t virtAddress, uint8_t* data, const uint32_t length,
    const bool forceFlash)
{
    for (uint32_t i = 0; i < length; i++)
    {
        const Error result = readMem(virtAddress + i, data[i], forceFlash);
        if (result != Error::Success)
        {
            return result;
        }
    }
    return Error::Success;
}

bool MemMapper::isMapped(const uint32_t virtAddress)
{
    if (autoAddPage)
    {
        return true;
    }

    uint32_t pageNum;
    const Error result = getFlashPageNum(virtAddress, &pageNum);
    return (result == Error::Success) && (pageNum != 0);
}

bool MemMapper::isMappedRange(const uint32_t virtStartAddress, const uint32_t virtEndAddress)
{
    return isMapped(virtStartAddress) && isMapped(virtEndAddress);
}

uint8_t* MemMapper::memoryPtr(const uint32_t virtAddress, const bool forceFlash) const
{
    uint32_t flashPageNum;
    const Error result = getFlashPageNum(virtAddress, &flashPageNum);

    if (result != Error::Success)
    {
        return nullptr;
    }
    if (forceFlash)
    {
        static_cast<void>(doFlash());
    }
    if (flashPageNum == 0)
    {
        return nullptr;
    }
    if ((flashPageNum == writePage) && !forceFlash)
    {
        return ramBuffer + virtualAddressToIndex(virtAddress);
    }
    return iapAddressOfPage(flashPageNum) + virtualAddressToIndex(virtAddress);
}

uint8_t MemMapper::getUInt8(const uint32_t virtAddress) const
{
    uint8_t value;
    if (readMem(virtAddress, value) == Error::Success)
    {
        return value;
    }
    return 0;
}

uint8_t& MemMapper::operator[](const uint32_t nIndex) const
{
    return memoryPtr(nIndex)[0];
}

uint32_t MemMapper::getUIntX(const uint32_t virtAddress, const uint32_t length) const
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < length; i++)
    {
        uint32_t address;
        if (endianess == BIG_ENDIAN)
            address = virtAddress + i;
        else
            address = virtAddress + length - i - 1;
        uint8_t b;
        readMem(address, b);
        value <<= 8;
        value |= b;
    }
    return value;
}

uint16_t MemMapper::getUInt16(const uint32_t virtAddress) const
{
    return getUIntX(virtAddress, sizeof(uint16_t));
}

uint32_t MemMapper::getUInt32(const uint32_t virtAddress) const
{
    return getUIntX(virtAddress, sizeof(uint32_t));
}

MemMapper::Error MemMapper::setUInt8(const uint32_t virtAddress, const uint8_t data)
{
    return writeMem(virtAddress, data);
}

MemMapper::Error MemMapper::setUIntX(const uint32_t virtAddress, const uint32_t length, uint32_t data)
{
    auto result = Error::InvalidAddress;
    for (uint32_t i = 0; i < length; i++)
    {
        uint32_t address;
        if (endianess == BIG_ENDIAN)
        {
            address = virtAddress + length - i - 1;
        }
        else
        {
            address = virtAddress + i;
        }
        result = writeMem(address, data & 0xff);
        if (result != Error::Success)
        {
            return result;
        }
        data >>= 8;
    }
    return result;
}

MemMapper::Error MemMapper::setUInt16(const uint32_t virtAddress, const uint16_t data)
{
    return setUIntX(virtAddress, sizeof(uint16_t), data);
}

MemMapper::Error MemMapper::setUInt32(const uint32_t virtAddress, const uint32_t data)
{
    return setUIntX(virtAddress, sizeof(uint32_t), data);
}

void MemMapper::setEndianess(const uint32_t value)
{
    endianess = value;
}