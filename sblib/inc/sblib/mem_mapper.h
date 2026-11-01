/*
 * mem_mapper.h
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

#ifndef SBLIB_MEM_MAPPER_H_
#define SBLIB_MEM_MAPPER_H_

#include <sblib/platform.h>

/**
 * @brief Maps a 16-bit (0x0-0xffff) virtual address space onto pages of physical flash memory.
 *
 * @details
 * MemMapper provides a virtual memory abstraction over a region of on-chip flash memory.
 * The virtual address space is partitioned into 256 byte pages.
 * Entries are stored XOR-inverted (raw = value ^ @ref AllocEntryXorMask) so that an erased
 * flash entry is interpreted as "unallocated" (logical value 0).
 * Write operations are buffered in a RAM write buffer (@ref ramBuffer) and only
 * committed to flash on demand via @ref doFlash(). The allocation table is accessed
 * directly in flash for reads. Writes stage the full table page into @ref ramBuffer
 * before committing via IAP. If @ref autoAddPage is @c true,
 * a new physical flash page is allocated automatically when a write
 * targets an unmapped virtual page. Otherwise, pages must be pre-allocated
 * with @ref addRange() before writing.<br>
 * Physical flash layout of the managed memory region:
 * @code
 *   Offset 0 : allocation table (one flash page, @ref FLASH_PAGE_SIZE bytes)
 *   Offset FLASH_PAGE_SIZE: first usable data page
 * @endcode
 *
 * @note The physical start address of the managed region is @code FLASH_BASE_ADDRESS + allocationTablePtr @endcode.
 * @note One flash page is @c FLASH_PAGE_SIZE (256) bytes on supported targets.
 *
 * @warning The first flash page of the managed region is reserved for the <em>allocation
 *          table</em> that maps virtual page indices to physical flash page numbers.
 */
class MemMapper
{
public:
    /**
     * @brief Constructs a MemMapper for the given flash region.
     *
     * Loads the allocation table from flash and verifies its integrity. If the
     * table appears corrupted it is reset to unallocated.
     *
     * @param flashBase   Page-aligned offset (within the 16-bit address space)
     *                    that marks the start of the managed flash region
     *                    (e.g. @c 0xEF00. The effective first usable physical address is
     *                    <tt>0xF000</tt>.)
     * @param flashSize   Size of the managed region in bytes. Must be a
     *                    multiple of @c FLASH_PAGE_SIZE (256 bytes).
     * @param autoAddPage If @c true, writing to an unmapped virtual address
     *                    automatically allocates a new flash page for it.
     *                    If @c false (default), pages must be pre-allocated
     *                    with @ref addRange() before writing.
     * @warning The first flash page of the managed region is reserved for the <em>allocation
     *          table</em> which is internally used to map virtual page indices to physical flash page numbers.
     */
    explicit MemMapper(uint32_t flashBase, uint32_t flashSize, bool autoAddPage = false);

    /**
     * @brief Type of one allocation-table entry and the mask that encodes it in flash.
     *
     * The managed region of an RP2350 lies at the end of a flash with more than 256 pages, so a
     * physical page number does not fit into one byte there. The LPC11xx keeps the one byte
     * format and therefore its on-flash layout. @c MEMMAPPER_LARGE_INDICES selects the wide
     * format in host tests, which always run on the LPC11xx emulation.
     * @note One entry addresses the physical pages 0 to @c AllocEntryXorMask.
     * @note @ref ramBuffer and the managed region are page aligned, so the entries of an
     *       allocation table are aligned for this type.
     */
#if defined(SBLIB_PLATFORM_RP2350) || defined(MEMMAPPER_LARGE_INDICES)
    using AllocEntryType = uint16_t;
    static constexpr AllocEntryType AllocEntryXorMask = 0xffffu; ///< XOR mask of one entry in flash.
#else
    using AllocEntryType = uint8_t;
    static constexpr AllocEntryType AllocEntryXorMask = 0xffu;   ///< XOR mask of one entry in flash.
#endif

    /** @brief Size of one allocation-table entry in bytes. */
    static constexpr int32_t AllocEntryBytes = sizeof(AllocEntryType);

    /**
     * @brief Number of entries in the allocation table (one per virtual page).
     *
     * The table always occupies exactly one flash page: 256 entries of one byte on the LPC11xx,
     * 128 entries of two bytes on the RP2350.
     * @note The number of entries limits the virtual address space to @c allocTableSize pages.
     */
    static constexpr uint32_t allocTableSize = FLASH_PAGE_SIZE / sizeof(AllocEntryType);

    /** Default constructor is not allowed. */
    MemMapper() = delete;

    /** Default Virtual destructor */
    virtual ~MemMapper() = default;

    /**
     * @brief Error codes returned by MemMapper operations.
     */
    enum class Error: int8_t
    {
        Success        =  0,  ///< Operation completed successfully.
        InvalidAddress = -1,  ///< Virtual address is out of range or not page-aligned.
        NotMapped      = -2,  ///< Virtual address has no associated flash page.
        OutOfMemory    = -4,  ///< No more flash pages are available for allocation.
        InvalidLength  = -8,  ///< Length invalid.
    };

    /**
     * @brief Bitmask flags indicating which flash areas were written.
     */
    enum FlashResult: uint8_t
    {
        FlashedNothing         = 0, ///< Nothing needed to be written to flash.
        FlashedAllocationTable = 1, ///< The allocation table page was written to flash.
        FlashedWriteBuffer     = 2, ///< The data write buffer (one data page) was written to flash.
    };

    /**
     * @brief Writes a single byte to a virtual address.
     *
     * The write is buffered in RAM and not immediately committed to flash.
     * Call @ref doFlash() to persist. If the target page is not yet mapped
     * and @ref autoAddPage is @c true, a new flash page is allocated first.
     *
     * @param virtAddress 16-bit virtual address to write to.
     * @param data        Byte value to write.
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     */
    Error writeMem(uint32_t virtAddress, uint8_t data);

    /**
     * @brief Writes an array of bytes starting at a virtual address.
     *
     * Calls @ref writeMem() for each byte in sequence. Stops and returns the
     * error code immediately if any individual write fails.
     *
     * @param virtAddress 16-bit virtual start address.
     * @param data        Pointer to the source data buffer.
     * @param length      Number of bytes to write.
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     */
    virtual Error writeMemPtr(uint32_t virtAddress, uint8_t* data, uint32_t length);

    /**
     * @brief Reads a single byte from a virtual address.
     *
     * If @p forceFlash is @c true, pending writes are flushed to flash before
     * reading. Otherwise the RAM write buffer is consulted when the requested page is currently buffered.
     *
     * @param virtAddress 16-bit virtual address to read from.
     * @param data        Reference that receives the read byte. Set to @c 0x00
     *                    on error.
     * @param forceFlash  Flush pending writes to flash before reading
     *                    (default: @c false).
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     */
    Error readMem(uint32_t virtAddress, uint8_t& data, bool forceFlash = false) const;

    /**
     * @brief Reads an array of bytes starting at @p virtAddress.
     *
     * Calls @ref readMem() for each byte in sequence. Stops and returns the
     * error code immediately if any individual read fails.
     *
     * @param virtAddress 16-bit virtual start address.
     * @param data        Pointer to the destination data buffer.
     * @param length      Number of bytes to read.
     * @param forceFlash  Flush pending writes to flash before reading.
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     */
    virtual Error readMemPtr(uint32_t virtAddress, uint8_t* data, uint32_t length, bool forceFlash);

    /**
     * @brief Pre-allocates flash pages for a virtual address range.
     *
     * For each 256-byte virtual page within [@p virtAddress,
     * @p virtAddress + @p length) that is not yet mapped, a new physical flash
     * page is allocated and the allocation table is immediately committed to
     * flash. Already-mapped pages are skipped.
     *
     * The whole range must lie inside the @c allocTableSize virtual pages that the allocation
     * table can name. Nothing is read, written or allocated before that is verified: a range
     * that ends behind the table is rejected with @ref Error::InvalidAddress, exactly like a
     * start page behind the table.
     *
     * @param virtAddress Page-aligned 16-bit virtual start address.
     *                    Must be a multiple of @c FLASH_PAGE_SIZE.
     * @param length      Size of the range in bytes. Must be non-zero and a
     *                    multiple of @c FLASH_PAGE_SIZE.
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     * @retval Error::InvalidAddress if @p virtAddress is not page-aligned or
     *                               the page index exceeds the allocation table size.
     * @retval Error::InvalidLength  if @p length is zero or not a multiple of
     *                               @c FLASH_PAGE_SIZE.
     * @retval Error::OutOfMemory    if there are not enough free flash pages.
     */
    Error addRange(uint32_t virtAddress, uint32_t length);

    /**
     * @brief Flushes all pending in-RAM changes to flash.
     *
     * Commits the allocation table and/or the data write buffer to flash if
     * either has been modified since the last flush.
     *
     * @return Bitmask of @ref FlashResult flags:
     *         - @ref FlashedNothing         – nothing needed to be written.
     *         - @ref FlashedAllocationTable – allocation table was committed.
     *         - @ref FlashedWriteBuffer     – data page buffer was committed.
     */
    int32_t doFlash() const;

    /**
     * @brief Sets the byte order used by the multi-byte accessor methods.
     *
     * @param value @c BIG_ENDIAN or @c LITTLE_ENDIAN.
     */
    void setEndianess(uint32_t value);

    /**
     * @brief Reads an unsigned byte from a virtual address.
     *
     * @param virtAddress 16-bit virtual address of the byte to read.
     * @return The byte value at @p virtAddress, or @c 0 if the address is not
     *         mapped or an error occurs.
     */
    uint8_t getUInt8(uint32_t virtAddress) const;

    /**
     * @brief The Array index operator returns a reference to the byte at a virtual address.
     *
     * Equivalent to <tt>*memoryPtr(nIndex, true)</tt>.
     *
     * @param nIndex 16-bit virtual address used as the index.
     * @return Reference to the byte at @p nIndex.
     * @warning Behaviour is undefined if @p nIndex is not mapped to a flash page.
     */
    uint8_t& operator[](uint32_t nIndex) const;

    /**
     * @brief Reads an unsigned 16-bit value from a virtual address.
     *
     * @param virtAddress 16-bit virtual address of the 16-bit word.
     * @return The unsigned 16-bit value respecting the current endianness, or @c 0 on error.
     */
    uint16_t getUInt16(uint32_t virtAddress) const;

    /**
     * @brief Reads an unsigned 32-bit value from a virtual address.
     *
     * @param virtAddress 16-bit virtual address of the 32-bit word.
     * @return The unsigned 32-bit value respecting the current endianness, or @c 0 on error.
     */
    uint32_t getUInt32(uint32_t virtAddress) const;

    /**
     * @brief Writes an unsigned byte to a virtual address.
     *
     * @param virtAddress 16-bit virtual address.
     * @param data        Byte value to write.
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     */
    Error setUInt8(uint32_t virtAddress, uint8_t data);

    /**
     * @brief Writes an unsigned 16-bit value to a virtual address.
     *
     * @param virtAddress 16-bit virtual address.
     * @param data        16-bit value to write.
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     */
    Error setUInt16(uint32_t virtAddress, uint16_t data);

    /**
     * @brief Writes an unsigned 32-bit value to a virtual address.
     *
     * @param virtAddress 16-bit virtual address.
     * @param data        32-bit value to write.
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     */
    Error setUInt32(uint32_t virtAddress, uint32_t data);

    /**
     * @brief Returns a raw pointer to the byte at a virtual address.
     *
     * If the target page is currently buffered in the write buffer and
     * @p forceFlash is @c false, a pointer into the RAM write buffer is
     * returned. Otherwise the write buffer is first committed to flash and
     * the returned pointer addresses the physical flash directly.
     *
     * @param virtAddress 16-bit virtual address.
     * @param forceFlash  If @c true (default), flush the write buffer to flash
     *                    first so the pointer addresses physical flash.
     * @return Pointer to the byte at @p virtAddress, or @c nullptr if the
     *         address is invalid or not mapped.
     * @warning If @c forceFlash is @c false, pointer arithmic usage on the return pointer may cause access violations.
     */
    uint8_t* memoryPtr(uint32_t virtAddress, bool forceFlash = true) const;

    /**
     * @brief Checks whether a virtual address is mapped to a flash page.
     *
     * When @ref autoAddPage is @c true this method always returns @c true.
     *
     * @param virtAddress 16-bit virtual address to check.
     * @return @c true if @p virtAddress is mapped to a physical flash page,
     *         @c false otherwise.
     */
    virtual bool isMapped(uint32_t virtAddress);

    /**
     * @brief Checks whether both endpoints of a virtual address range are mapped.
     *
     * @param virtStartAddress 16-bit virtual start address of the range.
     * @param virtEndAddress   16-bit virtual end address of the range (inclusive).
     * @return @c true if both endpoints are mapped, @c false otherwise.
     */
    virtual bool isMappedRange(uint32_t virtStartAddress, uint32_t virtEndAddress);

private:
    /**
     * @brief Allocates the next available physical flash page for a virtual page.
     *
     * Scans the allocation table to determine @ref lastAllocated (the highest physical page in use),
     * then assigns the following physical page to @p virtPage, records it in the allocation table,
     * zeroes @ref ramBuffer, and sets @ref writePage to the new page.
     *
     * @param virtPage Virtual page index
     * @return @ref Error::Success on success, @ref Error::OutOfMemory if the managed flash region is exhausted.
     */
    Error allocatePage(uint32_t virtPage);

    /**
     * @brief Looks up the physical flash page number for a virtual address.
     *
     * @param virtAddress     16-bit virtual address.
     * @param flashPageNumber Output: receives the physical page number, or 0 if the virtual page is not mapped.
     * @return @ref Error::Success, or @ref Error::InvalidAddress if @p virtAddress is outside the addressable range.
     */
    Error getFlashPageNum(uint32_t virtAddress, uint32_t* flashPageNumber) const;

    /**
     * @brief Reads a multi-byte unsigned integer from a virtual address.
     *
     * Assembles bytes in the order defined by @ref endianess.
     *
     * @param virtAddress 16-bit virtual start address.
     * @param length      Number of bytes to read.
     * @return Assembled unsigned integer value, or @c 0 on error.
     */
    uint32_t getUIntX(uint32_t virtAddress, uint32_t length) const;

    /**
     * @brief Writes a multi-byte unsigned integer to a virtual address.
     *
     * Writes bytes in the order defined by @ref endianess.
     *
     * @param virtAddress 16-bit virtual start address.
     * @param length      Number of bytes to write.
     * @param data        Value to write.
     * @return @ref Error::Success on success, otherwise an @ref Error code.
     */
    Error setUIntX(uint32_t virtAddress, uint32_t length, uint32_t data);

    /**
     * Pointer to the start of the managed flash region starting with the allocation table.
     * @note Allocation table is located at offset 0
     * @note Usable data starts from offset @code allocationTablePtr + allocTableSize @endcode
     * */
    const uint8_t* allocationTablePtr;
    /**
     * @brief Physical page number of @ref allocationTablePtr which holds the allocation table.
     */
    const uint32_t allocationTablePageNumber;
    const uint32_t flashSize;      ///< Total size of the managed flash region in bytes.
    const uint32_t flashSizePages; ///< Total size of the managed region in pages (@ref flashSize / @c FLASH_PAGE_SIZE).
    const bool autoAddPage;        ///< When @c true, unmapped virtual pages are allocated automatically on write.

    static constexpr uint8_t InvalidAllocTableByte = 0xff; ///< Raw flash byte representing an unallocated entry.

    /** @brief RAM write buffer for the currently active flash page. */
    alignas(FLASH_RAM_BUFFER_ALIGNMENT) mutable uint8_t ramBuffer[FLASH_PAGE_SIZE];

    /** @brief Size of @ref ramBuffer in bytes. */
    static constexpr uint32_t ramBufferSize = sizeof(ramBuffer)/sizeof(ramBuffer[0]);

    static_assert(allocTableSize <= ramBufferSize, "Allocation table must fit in RAM write buffer");

    mutable uint32_t writePage; ///< Physical page number loaded in @ref ramBuffer (@ref allocationTablePageNumber = alloc table, 0 = empty).
    uint32_t lastAllocated;     ///< Physical page number of the last allocated flash page (0 if none allocated yet).
    uint32_t endianess;         ///< Byte order for multi-byte accessors: @c BIG_ENDIAN or @c LITTLE_ENDIAN.

    mutable bool flashMemModified;   ///< @c true when @ref ramBuffer holds a modified data page not yet committed to flash.
    mutable bool allocTableModified; ///< @c true when @ref ramBuffer holds a modified allocation table not yet committed to flash.

    /**
     * @brief Writes the allocation table to flash if it has been modified.
     *
     * Flushes any pending data page first, then writes @ref ramBuffer
     * (which holds the modified allocation table) to @ref allocationTablePageNumber.
     * @return @c true if the table was written, @c false otherwise.
     */
    bool doFlashAllocTable() const;

    /**
     * @brief Writes the data write buffer to flash if it has been modified.
     * @return @c true if the buffer was written, @c false otherwise.
     */
    bool doFlashWriteTable() const;

    /**
     * @brief Erases a flash page and programs it with the contents of @p buffer.
     *
     * @param buffer     Pointer to the @c FLASH_PAGE_SIZE byte buffer to write.
     * @param pageNumber Physical flash page number to write.
     * @warning Calls @c fatalError() if the IAP erase or program operation fails.
     */
    static void writeToFlashPage(const uint8_t* buffer, uint32_t pageNumber) ;

    /**
     * @brief Resets the allocation table to unallocated.
     *
     * Copies the allocation table from flash into @ref ramBuffer, fills it with
     * @ref InvalidAllocTableByte, and marks @ref allocTableModified so the
     * updated table is committed on the next @ref doFlash().
     */
    void clearAllocTable() const;

    /**
     * @brief Verifies the allocation table in flash and resets it if corrupted.
     *
     * Reads directly from flash. Scans for raw zero entries (which would represent
     * the physical page @c AllocEntryXorMask, an unlikely valid value). Finding more than
     * one such entry is treated as table corruption (e.g. from a partial flash erase),
     * and @ref clearAllocTable() is called to recover.
     *
     * @note A more thorough check should verify that no physical page number appears more than once in the table.
     */
    void verifyAllocTable() const;

    /**
     * @brief Writes a single entry to the allocation table.
     *
     * Copies the allocation table from flash into @ref ramBuffer (if not already
     * loaded), modifies the entry XOR-inverted (@ref AllocEntryXorMask) to match the raw
     * flash representation where an erased entry means "unallocated", and sets
     * @ref allocTableModified if the value actually changes.
     *
     * @param index Entry index equal to the virtual page number.
     * @param value Physical flash page number to store.
     */
    void writeAllocTableEntry(uint8_t index, AllocEntryType value) const;

    /**
     * @brief Reads a single entry from the allocation table directly in flash.
     *
     * Returns the XOR-inverted raw flash entry, yielding 0 for an unallocated
     * entry and the physical page number for an allocated one.
     *
     * @param index Entry index equal to the virtual page number.
     * @return Physical flash page number, or @c 0 if the entry is unallocated.
     */
    AllocEntryType readAllocTableEntry(uint8_t index) const;

    /**
     * @brief Returns the byte offset within a page from a virtual address.
     *
     * @param virtAddress A virtual address.
     * @return The byte offset within the page.
     */
    static uint32_t virtualAddressToIndex(uint32_t virtAddress);
};

#endif /* SBLIB_MEM_MAPPER_H_ */
