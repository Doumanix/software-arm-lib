/******************************************************************************
 * @addtogroup SBLIB_MAIN_GROUP Main Group Description
 * @defgroup SBLIB_SUB_GROUP_1 Sub Group 1 Description
 * @ingroup SBLIB_MAIN_GROUP
 * @brief   
 * @details 
 *
 *
 * @{
 *
 * @file   memory.h
 * @author Darthyson <darth@maptrack.de>
 ******************************************************************************/

/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 3 as
 published by the Free Software Foundation.
 ---------------------------------------------------------------------------*/

#ifndef SBLIB_KNX_MEMORY_H_
#define SBLIB_KNX_MEMORY_H_

#include <cstdint>

/**
 * Class for basic memory operations.
 */
class Memory
{
public:
    Memory() = delete;
    virtual ~Memory() = default;
    Memory(uint32_t start, uint32_t size);

    /**
     * Access the memory like an ordinary array
     *
     * @param address  The address of the data byte to access.
     * @return The data byte at the given address.
     * @note @ref startAddress will be subtracted from @ref address
     */
    virtual uint8_t& operator[](uint32_t address) const = 0;

    /**
     * Access the memory like an ordinary array
     *
     * @param address  The address of the data byte to access.
     * @return The data byte at the given address.
     * @note @ref startAddress will be subtracted from @ref address
     */
    virtual uint8_t& operator[](uint32_t address) = 0;

    /**
     * Gets a byte from the memory at given @ref address
     *
     * @param address   Address to get the byte from
     * @return          Content as byte at the given @ref address
     * @note @ref startAddress will be subtracted from @ref address
     */
    [[nodiscard]] virtual uint8_t getUInt8(uint32_t address) const = 0;

    /**
     * Gets a word from the memory at given @ref address and address + 1
     *
     * @param address   Address to get the byte from
     * @return          Content as word at the @ref address
     * @note @ref startAddress will be subtracted from @ref address
     */
    [[nodiscard]] virtual uint16_t getUInt16(uint32_t address) const = 0;

    /**
     * Gets a double word (big endian, like @ref getUInt16) from the memory at given @ref address up to address + 3
     *
     * @param address   Address to get the first byte from
     * @return          Content as double word at the @ref address, 0 if the range is not accessible
     * @note @ref startAddress will be subtracted from @ref address
     */
    [[nodiscard]] virtual uint32_t getUInt32(uint32_t address) const = 0;

    /**
     * Copies @ref count bytes from the memory at given @ref address to @ref dst
     *
     * @param address   Address of the first byte to copy
     * @param dst       Destination buffer, must be at least @ref count bytes large
     * @param count     Number of bytes to copy
     * @return          True if the whole range is accessible and was copied, otherwise false
     *                  and nothing is copied. Unlike @ref operator[] this does not end in fatalError().
     * @note @ref startAddress will be subtracted from @ref address
     */
    [[nodiscard]] virtual bool read(uint32_t address, uint8_t* dst, uint32_t count) const = 0;

    /**
     * Checks, if an @ref address is accessible in the memory
     *
     * @param address   Address to check for valid access
     * @return          True if @ref address is accessible, otherwise false
     */
    [[nodiscard]] virtual bool inRange(uint32_t address) const;

    /**
     * Checks, if a given address range is accessible in the memory
     *
     * @param start     Start address of the range to check
     * @param end       End address of the range to check
     * @return          True if range is accessible, otherwise false
     */
    [[nodiscard]] virtual bool inRange(uint32_t start, uint32_t end) const;

    /**
     * Get the start address of the memory
     *
     * @return Start address of the memory
     */
    [[nodiscard]] uint32_t startAddr() const;

    /**
     * Get the end address of the memory
     *
     * @return end address of the memory
     */
    [[nodiscard]] uint32_t endAddr() const;

    /**
     * Get the size of the memory
     *
     * @return size of the memory in bytes
     */
    [[nodiscard]] uint32_t size() const;

protected:
    /**
     * Checks, if @ref count bytes starting at @ref address (absolute, not yet normalized) are accessible
     *
     * @param address   Address of the first byte
     * @param count     Number of bytes
     * @return          True if all bytes are accessible, overflow safe. An empty range is always valid.
     */
    [[nodiscard]] bool rangeValid(uint32_t address, uint32_t count) const;

    uint32_t startAddress; //!< virtual start address of the memory
    uint32_t endAddress;   //!< virtual end address of the memory
    uint32_t sizeTotal;    //!< size in bytes of the memory

    /**
     * Changes the given absolute memory address to the real internal index
     *
     * @param address The absolute memory address
     */
    void normalizeAddress(uint32_t* address) const { (*address -= startAddress); }
};


#endif /* SBLIB_KNX_MEMORY_H_ */
/** @}*/
