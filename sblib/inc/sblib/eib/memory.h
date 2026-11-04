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
    [[nodiscard]] uint8_t getUInt8(uint32_t address) const;

    /**
     * Gets a word from the memory at given @ref address and address + 1
     *
     * @param address   Address to get the byte from
     * @return          Content as word at the @ref address
     * @note @ref startAddress will be subtracted from @ref address
     */
    [[nodiscard]] uint16_t getUInt16(uint32_t address) const;

    /**
     * Gets a double word (big endian, like @ref getUInt16) from the memory at given @ref address up to address + 3
     *
     * @param address   Address to get the first byte from
     * @return          Content as double word at the @ref address, 0 if the range is not accessible
     * @note @ref startAddress will be subtracted from @ref address
     */
    [[nodiscard]] uint32_t getUInt32(uint32_t address) const;

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
    [[nodiscard]] bool read(uint32_t address, uint8_t* dst, uint32_t count) const;

    /**
     * Sets a byte in the memory at given @ref address
     *
     * @param address   Address of the byte
     * @param value     New content
     * @return          True if @ref address is accessible and the byte was set, otherwise false
     * @note @ref startAddress will be subtracted from @ref address
     */
    bool setUInt8(uint32_t address, uint8_t value);

    /**
     * Sets a word (big endian, like @ref getUInt16) in the memory at given @ref address and address + 1
     *
     * @param address   Address of the first byte
     * @param value     New content
     * @return          True if the range is accessible and the word was set, otherwise false and nothing is changed
     * @note @ref startAddress will be subtracted from @ref address
     */
    bool setUInt16(uint32_t address, uint16_t value);

    /**
     * Sets a double word (big endian, like @ref getUInt32) in the memory at given @ref address up to address + 3
     *
     * @param address   Address of the first byte
     * @param value     New content
     * @return          True if the range is accessible and the double word was set, otherwise false and nothing is changed
     * @note @ref startAddress will be subtracted from @ref address
     */
    bool setUInt32(uint32_t address, uint32_t value);

    /**
     * Copies @ref count bytes from @ref src to the memory at given @ref address
     *
     * @param address   Address of the first byte to set
     * @param src       Source buffer with at least @ref count bytes
     * @param count     Number of bytes to copy
     * @return          True if the whole range is accessible and was copied, otherwise false and nothing is changed
     * @note @ref startAddress will be subtracted from @ref address
     */
    bool write(uint32_t address, const uint8_t* src, uint32_t count);

    /**
     * Sets @ref count bytes of the memory at given @ref address to @ref value
     *
     * @param address   Address of the first byte to set
     * @param count     Number of bytes to set
     * @param value     Value for every byte
     * @return          True if the whole range is accessible and was set, otherwise false and nothing is changed
     * @note @ref startAddress will be subtracted from @ref address
     */
    bool fill(uint32_t address, uint32_t count, uint8_t value);

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
     * Backing store of the memory, set by the derived class. The access functions getUInt8/16/32, read,
     * setUInt8/16/32, write and fill work on it, they are not virtual to keep the vtables small.
     */
    uint8_t* data = nullptr;

    /**
     * Called by write and fill after the content was changed, may be nullptr.
     * A function pointer instead of a virtual function, so no vtable entry is added.
     */
    void (*changedHook)(Memory* memory) = nullptr;

    /**
     * Changes the given absolute memory address to the real internal index
     *
     * @param address The absolute memory address
     */
    void normalizeAddress(uint32_t* address) const { (*address -= startAddress); }
};


#endif /* SBLIB_KNX_MEMORY_H_ */
/** @}*/
