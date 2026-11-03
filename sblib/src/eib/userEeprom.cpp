/*
 * userEeprom.cpp
 *
 *  Created on: 19.11.2021
 *      Author: dridders
 *      Author: Doumanix <doumanix@gmx.de>
 */

#include <sblib/eib/userEeprom.h>
#include <sblib/internal/iap.h>
#include <sblib/interrupt.h>
#include <sblib/timer.h>
#include <sblib/bits.h>
#include <cstring>


uint32_t UserEeprom::flashSize() const
{
    return userEepromFlashSize;
}

uint32_t UserEeprom::numEepromPages() const
{
    return FLASH_SECTOR_SIZE / flashSize();
}

uint8_t* UserEeprom::lastEepromPage() const
{
    return flashSectorAddress() + flashSize() * (numEepromPages() - 1);
}

uint8_t* UserEeprom::flashSectorAddress() const
{
    return (FLASH_BASE_ADDRESS + iapFlashSize() - FLASH_SECTOR_SIZE);
}

uint8_t* UserEeprom::findValidPage() const
{
    const uint8_t* firstPage = FLASH_BASE_ADDRESS + iapFlashSize() - FLASH_SECTOR_SIZE;
    uint8_t* page = lastEepromPage();

    while (page >= firstPage)
    {
        if (page[size() - 1] != 0xff) ///\todo check more then  only one byte for 0xff (empty flash)
            return page;

        page -= userEepromFlashSize;
    }

    return nullptr; // no valid page found
}

void UserEeprom::readUserEeprom()
{
    uint8_t* page = findValidPage();

    if (page)
        memcpy(userEepromData, page, size());
    else
        memset(userEepromData, 0, size()); ///\todo should filling with zeros indicate a readError? if yes, then it should be somewhere reported

    modified(false);
}

void UserEeprom::writeUserEeprom()
{
    if (!isModified())
    {
        return;
    }

    noInterrupts();

    uint8_t* page = findValidPage();
    if (page == lastEepromPage())
    {
        page = flashSectorAddress();
    }
    else if (page)
        page += userEepromFlashSize;
    else
    {
        page = flashSectorAddress();
    }

    if (page == flashSectorAddress())
    {
        // Erase the sector
        const int sectorId = iapSectorOfAddress(page);
        const IAP_Status rc = iapEraseSector(sectorId);
        if (rc != IAP_SUCCESS)
        {
            fatalError(); // erasing failed
        }
    }

    userEepromData[size() - 1] = 0; // mark the page as in use

    for (uint32_t i = 0; i < size(); i += 1024)
    {
        int chunk = size() - i;
        if (chunk > 1024)
        {
            chunk = 1024;
        }
        IAP_Status rc = iapProgram(page + i, userEepromData + i, chunk);
        if (rc != IAP_SUCCESS)
        {
            fatalError(); // flashing failed
        }
    }

    modified(false);
    interrupts();
}

UserEeprom::UserEeprom(const uint32_t start, const uint32_t size, const uint32_t flashSize) :
    Memory(start, size),
    userEepromData(new uint8_t[size]()),
    userEepromFlashSize(flashSize)
{
    readUserEeprom();
}

uint8_t& UserEeprom::directAccess_8(const uint32_t index) const
{
    if (!inRange(startAddr() + index))
    {
        fatalError();
    }
    return this->userEepromData[index];
}

uint16_t& UserEeprom::directAccess_16(const uint32_t index) const
{
    return reinterpret_cast<uint16_t&>(directAccess_8(index));
}

uint8_t& UserEeprom::operator[](uint32_t address) const
{
    normalizeAddress(&address);
    return userEepromData[address];
}

uint8_t& UserEeprom::operator[](const uint32_t address)
{
    /// 1. cast is static_cast<const UserEeprom&> to call operator[](..) const
    /// 2. cast is const_cast<uint8_t&>(..) to remove the const from the return of the operator[](...) const
    return const_cast<uint8_t&>(static_cast<const UserEeprom&>(*this).operator[](address));
}

uint8_t UserEeprom::getUInt8(uint32_t address) const
{
    normalizeAddress(&address);
    if (address > (size() - 1))
    {
        return 0;
    }
    return userEepromData[address];
}

uint16_t UserEeprom::getUInt16(uint32_t address) const
{
    normalizeAddress(&address);
    if ((address + 1) > (size() - 1))
    {
        return 0;
    }
    return makeWord(userEepromData[address], userEepromData[address + 1]);
}

uint32_t UserEeprom::getUInt32(uint32_t address) const
{
    if (!rangeValid(address, 4))
    {
        return 0;
    }
    normalizeAddress(&address);
    uint32_t value = makeWord(userEepromData[address], userEepromData[address + 1]);
    value <<= 16;
    value |= makeWord(userEepromData[address + 2], userEepromData[address + 3]);
    return value;
}

bool UserEeprom::read(uint32_t address, uint8_t* dst, const uint32_t count) const
{
    if (!rangeValid(address, count))
    {
        return false;
    }
    normalizeAddress(&address);
    memcpy(dst, userEepromData + address, count);
    return true;
}

bool UserEeprom::setUInt8(const uint32_t address, const uint8_t value)
{
    return write(address, &value, 1);
}

bool UserEeprom::setUInt16(const uint32_t address, const uint16_t value)
{
    const uint8_t bytes[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return write(address, bytes, sizeof(bytes));
}

bool UserEeprom::setUInt32(const uint32_t address, const uint32_t value)
{
    const uint8_t bytes[4] = {static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                               static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return write(address, bytes, sizeof(bytes));
}

bool UserEeprom::write(uint32_t address, const uint8_t* src, const uint32_t count)
{
    if (!rangeValid(address, count))
    {
        return false;
    }
    normalizeAddress(&address);
    if (memcmp(userEepromData + address, src, count) != 0)
    {
        memcpy(userEepromData + address, src, count);
        modified(true);
    }
    return true;
}

bool UserEeprom::fill(uint32_t address, const uint32_t count, const uint8_t value)
{
    if (!rangeValid(address, count))
    {
        return false;
    }
    normalizeAddress(&address);
    bool changed = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (userEepromData[address + i] != value)
        {
            userEepromData[address + i] = value;
            changed = true;
        }
    }
    if (changed)
    {
        modified(true);
    }
    return true;
}

void UserEeprom::modified(const bool newModified)
{
    userEepromModified = newModified;
    if (userEepromModified)
    {
        writeUserEepromTime = millis() + 50;
    }
    else
    {
        writeUserEepromTime = 0;
    }
}

bool UserEeprom::writeDelayElapsed() const
{
    const uint32_t timeSinceDeadline = millis() - writeUserEepromTime;
    return userEepromModified && timeSinceDeadline != 0 && timeSinceDeadline < 0x80000000u;
}

bool UserEeprom::isModified() const
{
    return userEepromModified;
}
