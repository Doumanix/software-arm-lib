/*
 * Unit tests for class MemMapper
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <sblib/mem_mapper.h>
#include <iap_emu.h>
#include <cstring>
#include <iomanip>
#include <new>
#include <vector>
#include <sys/param.h>
#include <catch.hpp>

/**
 * \brief Flash region used by tests: 0xe000..0xefff (4 KB = 16 pages)
 * - Page 0xe0 = allocation table
 * - Pages 0xe1..0xef = 15 data pages
 */
constexpr uint32_t TEST_FLASH_BASE = 0xe000;
constexpr uint32_t TEST_FLASH_SIZE = 0x1000;

void resetFlash()
{
    IAP_Init_Flash(0xff);
}

/**
 * @brief Decodes one allocation-table entry of the region at @p flashBase directly from the flash.
 *
 * @param flashBase Offset of the managed region, as passed to the MemMapper constructor.
 * @param virtPage  Virtual page number, i.e. the index of the entry in the allocation table.
 * @return Physical page number the entry points to, 0 when the virtual page is unallocated.
 */
static uint32_t physicalPageOf(uint32_t flashBase, uint32_t virtPage)
{
    MemMapper::AllocEntryType raw = 0;
    memcpy(&raw, &FLASH[flashBase + (virtPage * sizeof(MemMapper::AllocEntryType))], sizeof(raw));
    return static_cast<uint32_t>(raw ^ MemMapper::AllocEntryXorMask);
}

/**
 * @brief Writes one encoded allocation-table entry of the region at @p flashBase.
 *
 * @param flashBase Offset of the managed region, as passed to the MemMapper constructor.
 * @param virtPage  Virtual page number, i.e. the index of the entry in the allocation table.
 * @param raw       Encoded entry, as it is stored in flash.
 */
static void writeRawAllocTableEntry(uint32_t flashBase, uint32_t virtPage, MemMapper::AllocEntryType raw)
{
    memcpy(&FLASH[flashBase + (virtPage * sizeof(MemMapper::AllocEntryType))], &raw, sizeof(raw));
}

/**
 * @brief Copies the whole simulated flash, to compare it with the state after a test.
 */
static std::vector<uint8_t> snapshotFlash()
{
    return std::vector<uint8_t>(FLASH, FLASH + FLASH_SIZE);
}

/**
 * @brief Requires that every allocated virtual page of the region points into its own data area.
 *
 * The first page of the region holds the allocation table, so a valid data page number lies in
 * [base page + 1, last page of the region]. An allocation-table entry that was truncated on its
 * way to flash points somewhere else, which is what this check catches.
 *
 * @param flashBase Offset of the managed region.
 * @param flashSize Size of the managed region in bytes.
 */
static void requireAllocatedPagesInsideRegion(uint32_t flashBase, uint32_t flashSize)
{
    const uint32_t basePage = flashBase / FLASH_PAGE_SIZE;
    const uint32_t lastPage = basePage + (flashSize / FLASH_PAGE_SIZE) - 1;
    for (uint32_t virtPage = 0; virtPage < MemMapper::allocTableSize; virtPage++)
    {
        const uint32_t physicalPage = physicalPageOf(flashBase, virtPage);
        if (physicalPage == 0)
        {
            continue; // unallocated
        }
        INFO("virtual page 0x" << std::hex << virtPage << " points to physical page 0x" << physicalPage
                << ", region is 0x" << basePage << "..0x" << lastPage);
        REQUIRE(physicalPage > basePage);
        REQUIRE(physicalPage <= lastPage);
    }
}

/**
 * @brief Requires that no byte outside [@p flashBase, @p flashBase + @p flashSize) changed.
 *
 * A readback alone does not prove the write location: a MemMapper that stores a truncated page
 * number reads the data back from the same wrong place it wrote it to. Comparing every byte
 * outside the managed region with @p before shows where the data page really went.
 *
 * @param before    Copy of the simulated flash taken before the test, see @ref snapshotFlash.
 * @param flashBase Offset of the managed region.
 * @param flashSize Size of the managed region in bytes.
 */
static void requireFlashUnchangedOutside(const std::vector<uint8_t>& before, uint32_t flashBase, uint32_t flashSize)
{
    REQUIRE(before.size() == static_cast<size_t>(FLASH_SIZE));
    uint32_t changed = FLASH_SIZE;
    for (uint32_t i = 0; i < FLASH_SIZE; i++)
    {
        if ((i >= flashBase) && (i < (flashBase + flashSize)))
        {
            continue;
        }
        if (before[i] != FLASH[i])
        {
            changed = i;
            break;
        }
    }
    INFO("first byte changed outside the managed region: 0x" << std::hex << changed);
    REQUIRE(changed == FLASH_SIZE);
}

/**
 * @brief Requires that the whole simulated flash is still byte identical to @p before.
 *
 * A rejected call must not have written anything, neither into the managed region nor into the
 * allocation table. Unlike @ref requireFlashUnchangedOutside this compares every byte, because
 * the effect of a missing bounds check lies inside the region.
 *
 * @param before Copy of the simulated flash taken before the call, see @ref snapshotFlash.
 */
static void requireFlashUnchanged(const std::vector<uint8_t>& before)
{
    REQUIRE(before.size() == static_cast<size_t>(FLASH_SIZE));
    uint32_t changed = FLASH_SIZE;
    for (uint32_t i = 0; i < FLASH_SIZE; i++)
    {
        if (before[i] != FLASH[i])
        {
            changed = i;
            break;
        }
    }
    INFO("first changed flash byte: 0x" << std::hex << changed);
    REQUIRE(changed == FLASH_SIZE);
}

/**
 * @brief Requires that the virtual pages up to @p lastVirtPage of the region are unallocated.
 *
 * @param flashBase     Offset of the managed region, as passed to the MemMapper constructor.
 * @param firstVirtPage First virtual page number to check.
 * @param lastVirtPage  Last virtual page number to check (inclusive).
 */
static void requirePagesUnallocated(uint32_t flashBase, uint32_t firstVirtPage, uint32_t lastVirtPage)
{
    for (uint32_t virtPage = firstVirtPage; virtPage <= lastVirtPage; virtPage++)
    {
        INFO("virtual page 0x" << std::hex << virtPage);
        REQUIRE(physicalPageOf(flashBase, virtPage) == 0);
    }
}

TEST_CASE("MemMapper::", "[MemMapper]")
{
    SECTION("constructor")
    {
        SECTION("clean flash: all virtual pages start unmapped")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.isMapped(0x0000) == false);
            REQUIRE(mm.isMapped(0x0100) == false);
            // This checks only start and end address mapping
            REQUIRE(mm.isMappedRange(TEST_FLASH_BASE, TEST_FLASH_BASE + TEST_FLASH_SIZE - 1) == false);
            // This checks all pages within the range
            for (uint32_t i = TEST_FLASH_BASE; i < TEST_FLASH_BASE + TEST_FLASH_SIZE; i += FLASH_PAGE_SIZE)
            {
                REQUIRE(mm.isMapped(i) == false);
            }
            REQUIRE(mm.isMapped(0xe000) == false);
            REQUIRE(mm.isMapped(0xefff) == false);
            REQUIRE(mm.isMapped(0xffff) == false);
        }

        SECTION("single zero entry in allocTable is not treated as corrupt")
        {
            resetFlash();
            constexpr uint32_t MAPPED_PAGE = 5;
            // A raw entry of 0 decodes to AllocEntryXorMask, so the virtual page reads as mapped
            // (with one byte entries on the LPC11xx: physical page 0xff (255))
            writeRawAllocTableEntry(TEST_FLASH_BASE, MAPPED_PAGE, 0);
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.isMapped(MAPPED_PAGE * FLASH_PAGE_SIZE) == true);
        }

        SECTION("two zero entries in allocTable triggers corruption reset")
        {
            resetFlash();
            constexpr uint32_t FIRST_ZERO_MAPPED = 0;
            constexpr uint32_t SECOND_ZERO_MAPPED = 1;
            writeRawAllocTableEntry(TEST_FLASH_BASE, FIRST_ZERO_MAPPED, 0);
            writeRawAllocTableEntry(TEST_FLASH_BASE, SECOND_ZERO_MAPPED, 0);
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.isMapped(FIRST_ZERO_MAPPED * FLASH_PAGE_SIZE) == false);
            REQUIRE(mm.isMapped(SECOND_ZERO_MAPPED * FLASH_PAGE_SIZE) == false);
        }
    }

    SECTION("addRange", "[MemMapper]")
    {
        resetFlash();
        auto* mm = new MemMapper(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);

        SECTION("length zero returns INVALID_LENGTH")
        {
            REQUIRE(mm->addRange(0x0000, 0) == MemMapper::Error::InvalidLength);
        }

        SECTION("length not a multiple of page size returns INVALID_LENGTH")
        {
            REQUIRE(mm->addRange(0x0000, 1)  == MemMapper::Error::InvalidLength);
            REQUIRE(mm->addRange(0x0000, FLASH_PAGE_SIZE - 1) == MemMapper::Error::InvalidLength);
            REQUIRE(mm->addRange(0x0000, FLASH_PAGE_SIZE + 1) == MemMapper::Error::InvalidLength);
        }

        SECTION("unaligned address returns INVALID_ADDRESS")
        {
            REQUIRE(mm->addRange(0x0042, FLASH_PAGE_SIZE) == MemMapper::Error::InvalidAddress);
        }

        SECTION("address beyond 16-bit space returns INVALID_ADDRESS")
        {
            REQUIRE(mm->addRange(0x10000, FLASH_PAGE_SIZE) == MemMapper::Error::InvalidAddress);
        }

        SECTION("valid page-aligned call succeeds")
        {
            for (uint32_t i = 0; i < TEST_FLASH_SIZE; i += FLASH_PAGE_SIZE)
            {
                delete mm;
                resetFlash();
                mm = new MemMapper(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);

                // -FLASH_PAGE_SIZE because one page is reserved for the allocTable
                for (uint32_t j = FLASH_PAGE_SIZE; j < TEST_FLASH_SIZE - FLASH_PAGE_SIZE; j += FLASH_PAGE_SIZE)
                {
                    INFO("i = 0x" << std::hex << i << ", j = 0x" << j);
                    REQUIRE(mm->addRange(i, j) == MemMapper::Error::Success);
                }
            }
        }

        SECTION("calling addRange twice on the same range is idempotent")
        {
            REQUIRE(mm->addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm->addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm->isMapped(0x0000) == true);
        }

        SECTION("multiple consecutive pages all become mapped")
        {
            REQUIRE(mm->addRange(0x0000, 3 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm->isMapped(0x0000) == true);
            REQUIRE(mm->isMapped(0x0100) == true);
            REQUIRE(mm->isMapped(0x0200) == true);
            REQUIRE(mm->isMapped(0x0300) == false); // one past the allocated range
        }
    }

    SECTION("writeMem / readMem", "[MemMapper]")
    {
        SECTION("read from unmapped address returns NOT_MAPPED and clears data")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            uint8_t data = 0xAB;
            REQUIRE(mm.readMem(0x0042, data) == MemMapper::Error::NotMapped);
            REQUIRE(data == 0x00);
        }

        SECTION("write to unmapped address returns SUCCESS but read still returns NOT_MAPPED")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.writeMem(0x0042, 0xAB) == MemMapper::Error::Success);
            uint8_t data = 0;
            REQUIRE(mm.readMem(0x0042, data) == MemMapper::Error::NotMapped);
        }

        SECTION("write and read on a page mapped via addRange")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.writeMem(0x0042, 0xAB) == MemMapper::Error::Success);
            uint8_t data = 0;
            REQUIRE(mm.readMem(0x0042, data) == MemMapper::Error::Success);
            REQUIRE(data == 0xAB);
        }

        SECTION("second write to same address returns the latest value")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.writeMem(0x0010, 0xAA) == MemMapper::Error::Success);
            REQUIRE(mm.writeMem(0x0010, 0xBB) == MemMapper::Error::Success);
            uint8_t data = 0;
            REQUIRE(mm.readMem(0x0010, data) == MemMapper::Error::Success);
            REQUIRE(data == 0xBB);
        }

        SECTION("write and read boundary addresses within a page")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            uint8_t data = 0;

            REQUIRE(mm.writeMem(0x0000, 0x00) == MemMapper::Error::Success);
            REQUIRE(mm.readMem(0x0000, data)  == MemMapper::Error::Success);
            REQUIRE(data == 0x00);

            REQUIRE(mm.writeMem(0x00FF, 0xFF) == MemMapper::Error::Success);
            REQUIRE(mm.readMem(0x00FF, data)  == MemMapper::Error::Success);
            REQUIRE(data == 0xFF);
        }

        SECTION("negative virtual address returns INVALID_ADDRESS")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.writeMem(-1, 0x42) == MemMapper::Error::InvalidAddress);
            uint8_t data = 0xAB;
            REQUIRE(mm.readMem(-1, data)  == MemMapper::Error::InvalidAddress);
            REQUIRE(data == 0x00);
        }

        SECTION("virtual address beyond 16-bit space returns INVALID_ADDRESS")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.writeMem(0x10000, 0x42) == MemMapper::Error::InvalidAddress);
            uint8_t data = 0xAB;
            REQUIRE(mm.readMem(0x10000, data)  == MemMapper::Error::InvalidAddress);
            REQUIRE(data == 0x00);
        }
    }

    // ---------------------------------------------------------------------------

    SECTION("autoAddPage", "[MemMapper]")
    {
        SECTION("write and read succeed without an explicit addRange call")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, true);
            REQUIRE(mm.writeMem(0x0042, 0xCD) == MemMapper::Error::Success);
            uint8_t data = 0;
            REQUIRE(mm.readMem(0x0042, data) == MemMapper::Error::Success);
            REQUIRE(data == 0xCD);
        }

        SECTION("isMapped returns true for any valid address")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, true);
            REQUIRE(mm.isMapped(0x0000) == true);
            REQUIRE(mm.isMapped(0x1234) == true);
            REQUIRE(mm.isMapped(0xFF00) == true);
        }

        SECTION("sequential writes on the same page all read back correctly")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, true);
            for (uint8_t i = 0; i < 16; i++)
            {
                REQUIRE(mm.writeMem(0x0000 + i, i) == MemMapper::Error::Success);
            }
            for (uint8_t i = 0; i < 16; i++)
            {
                uint8_t data = 0;
                REQUIRE(mm.readMem(0x0000 + i, data) == MemMapper::Error::Success);
                REQUIRE(data == i);
            }
        }
    }

    // ---------------------------------------------------------------------------

    SECTION("isMapped", "[MemMapper]")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);

        SECTION("unmapped virtual pages return false")
        {
            REQUIRE(mm.isMapped(0x0000) == false);
            REQUIRE(mm.isMapped(0x0100) == false);
        }

        SECTION("all offsets within a mapped page return true")
        {
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.isMapped(0x0000) == true);
            REQUIRE(mm.isMapped(0x0080) == true);
            REQUIRE(mm.isMapped(0x00FF) == true);
        }

        SECTION("adjacent page that was not mapped returns false")
        {
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.isMapped(0x0100) == false);
        }
    }

    // ---------------------------------------------------------------------------

    SECTION("isMappedRange", "[MemMapper]")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);

        SECTION("both endpoints unmapped returns false")
        {
            REQUIRE(mm.isMappedRange(0x0000, 0x0100) == false);
        }

        SECTION("start mapped but end unmapped returns false")
        {
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.isMappedRange(0x0000, 0x0100) == false);
        }

        SECTION("both endpoints on the same mapped page returns true")
        {
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.isMappedRange(0x0000, 0x00FF) == true);
        }

        SECTION("endpoints on two separately mapped pages returns true")
        {
            REQUIRE(mm.addRange(0x0000, 2 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.isMappedRange(0x0000, 0x0100) == true);
        }
    }

    // ---------------------------------------------------------------------------

    SECTION("setUInt8 / getUInt8", "[MemMapper]")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);

        SECTION("store and retrieve a byte")
        {
            REQUIRE(mm.setUInt8(0x0010, 0xAB) == MemMapper::Error::Success);
            REQUIRE(mm.getUInt8(0x0010) == 0xAB);
        }

        SECTION("zero is stored and retrieved correctly")
        {
            REQUIRE(mm.setUInt8(0x0000, 0x00) == MemMapper::Error::Success);
            REQUIRE(mm.getUInt8(0x0000) == 0x00);
        }

        SECTION("0xFF is stored and retrieved correctly")
        {
            REQUIRE(mm.setUInt8(0x00FF, 0xFF) == MemMapper::Error::Success);
            REQUIRE(mm.getUInt8(0x00FF) == 0xFF);
        }
    }

    // ---------------------------------------------------------------------------

    SECTION("setUInt16 / getUInt16 endianness", "[MemMapper]")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);

        SECTION("LITTLE_ENDIAN (default): LSB at lower address")
        {
            mm.setEndianess(LITTLE_ENDIAN);
            REQUIRE(mm.setUInt16(0x0010, 0x1234) == MemMapper::Error::Success);
            REQUIRE(mm.getUInt16(0x0010) == 0x1234);
            REQUIRE(mm.getUInt8(0x0010) == 0x34); // LSB
            REQUIRE(mm.getUInt8(0x0011) == 0x12); // MSB
        }

        SECTION("BIG_ENDIAN: MSB at lower address")
        {
            mm.setEndianess(BIG_ENDIAN);
            REQUIRE(mm.setUInt16(0x0010, 0x1234) == MemMapper::Error::Success);
            REQUIRE(mm.getUInt16(0x0010) == 0x1234);
            REQUIRE(mm.getUInt8(0x0010) == 0x12); // MSB
            REQUIRE(mm.getUInt8(0x0011) == 0x34); // LSB
        }
    }

    // ---------------------------------------------------------------------------

    SECTION("setUInt32 / getUInt32 endianness", "[MemMapper]")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);

        SECTION("LITTLE_ENDIAN: LSB at lowest address")
        {
            mm.setEndianess(LITTLE_ENDIAN);
            REQUIRE(mm.setUInt32(0x0010, 0x12345678) == MemMapper::Error::Success);
            REQUIRE(mm.getUInt32(0x0010) == 0x12345678u);
            REQUIRE(mm.getUInt8(0x0010) == 0x78);
            REQUIRE(mm.getUInt8(0x0011) == 0x56);
            REQUIRE(mm.getUInt8(0x0012) == 0x34);
            REQUIRE(mm.getUInt8(0x0013) == 0x12);
        }

        SECTION("BIG_ENDIAN: MSB at lowest address")
        {
            mm.setEndianess(BIG_ENDIAN);
            REQUIRE(mm.setUInt32(0x0010, 0x12345678) == MemMapper::Error::Success);
            REQUIRE(mm.getUInt32(0x0010) == 0x12345678u);
            REQUIRE(mm.getUInt8(0x0010) == 0x12);
            REQUIRE(mm.getUInt8(0x0011) == 0x34);
            REQUIRE(mm.getUInt8(0x0012) == 0x56);
            REQUIRE(mm.getUInt8(0x0013) == 0x78);
        }
    }

    // ---------------------------------------------------------------------------

    SECTION("writeMemPtr / readMemPtr", "[MemMapper]")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);

        SECTION("write and read back a short array within one page")
        {
            uint8_t writeData[] = {0x11, 0x22, 0x33, 0x44, 0x55};
            uint8_t readData[5] = {};
            REQUIRE(mm.writeMemPtr(0x0010, writeData, 5) == MemMapper::Error::Success);
            REQUIRE(mm.readMemPtr(0x0010, readData, 5, false)   == MemMapper::Error::Success);
            REQUIRE(memcmp(writeData, readData, 5) == 0);
        }

        SECTION("write and read back an entire page (256 bytes)")
        {
            uint8_t writeData[FLASH_PAGE_SIZE];
            uint8_t readData[FLASH_PAGE_SIZE] = {};
            for (int i = 0; i < FLASH_PAGE_SIZE; i++)
                writeData[i] = static_cast<uint8_t>(i);
            REQUIRE(mm.writeMemPtr(0x0000, writeData, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.readMemPtr(0x0000, readData, FLASH_PAGE_SIZE, false)   == MemMapper::Error::Success);
            REQUIRE(memcmp(writeData, readData, FLASH_PAGE_SIZE) == 0);
        }

        SECTION("write straddling page boundary flushes first page and writes to second")
        {
            REQUIRE(mm.addRange(0x0100, FLASH_PAGE_SIZE) == MemMapper::Error::Success);

            uint8_t writeData[4] = {0xAA, 0xBB, 0xCC, 0xDD};
            // Last 2 bytes of page 0, first 2 bytes of page 1
            REQUIRE(mm.writeMemPtr(0x00FE, writeData, 4) == MemMapper::Error::Success);
            uint8_t readData[4] = {};
            REQUIRE(mm.readMemPtr(0x00FE, readData, 4, false)   == MemMapper::Error::Success);
            REQUIRE(memcmp(writeData, readData, 4) == 0);
        }
    }

    SECTION("memoryPtr", "[MemMapper]")
    {
        SECTION("returns nullptr for an unmapped address")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.memoryPtr(0x0000, false) == nullptr);
        }

        SECTION("returns nullptr for a negative (invalid) address")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.memoryPtr(-1, false) == nullptr);
        }

        SECTION("returns a valid pointer that reads back the written value")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.writeMem(0x0010, 0xAB) == MemMapper::Error::Success);
            const uint8_t* ptr = mm.memoryPtr(0x0010, false);
            REQUIRE(ptr != nullptr);
            REQUIRE(*ptr == 0xAB);
        }
    }

    SECTION("operator[]", "[MemMapper]")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);

        SECTION("reads correct value from a mapped address")
        {
            REQUIRE(mm.writeMem(0x0020, 0x42) == MemMapper::Error::Success);
            REQUIRE(mm[0x0020] == 0x42);
        }
    }

    SECTION("doFlash persists data across instances", "[MemMapper]")
    {
        resetFlash();
        {
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.writeMem(0x0042, 0xAB) == MemMapper::Error::Success);
            REQUIRE(mm.doFlash() == MemMapper::FlashedWriteBuffer); // must have flushed the dirty page
        }

        // A new MemMapper on the same region must find the persisted value.
        MemMapper mm2(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm2.isMapped(0x0000) == true);
        uint8_t data = 0;
        REQUIRE(mm2.readMem(0x0042, data) == MemMapper::Error::Success);
        REQUIRE(data == 0xAB);
    }

    SECTION("out of memory", "[MemMapper]")
    {
        // TEST_FLASH_SIZE = 0x1000 → 16 pages: 1 alloc table + 15 data pages.

        SECTION("filling all 15 available data pages succeeds")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.addRange(0, 15 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            for (int page = 0; page < 15; page++)
            {
                REQUIRE(mm.isMapped(page * FLASH_PAGE_SIZE) == true);
            }
        }

        SECTION("allocating a 16th data page returns OUT_OF_MEMORY")
        {
            resetFlash();
            MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
            REQUIRE(mm.addRange(0, 15 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.addRange(15 * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE) == MemMapper::Error::OutOfMemory);
        }
    }
}

TEST_CASE("allocating a 16th data page returns OUT_OF_MEMORY")
{
    resetFlash();
    MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
    REQUIRE(mm.addRange(0, 15 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
    REQUIRE(mm.addRange(15 * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE) == MemMapper::Error::OutOfMemory);
}

#if defined(MEMMAPPER_LARGE_INDICES)
/**
 * Region for the tests with wide allocation-table entries: the last 8 KiB of the simulated flash.
 * Its base page lies above 255, so an entry truncated to 8 bit points outside the region.
 */
constexpr uint32_t HIGH_FLASH_SIZE = 0x2000;
constexpr uint32_t HIGH_FLASH_BASE = FLASH_SIZE - HIGH_FLASH_SIZE;
#endif

TEST_CASE("MemMapper writes its data pages inside the managed region", "[MemMapper][location]")
{
    SECTION("region at the usual test base")
    {
        resetFlash();
        const std::vector<uint8_t> before = snapshotFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0x0000, 4 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
        REQUIRE(mm.writeMem(0x0042, 0xAB) == MemMapper::Error::Success);
        REQUIRE(mm.doFlash() == MemMapper::FlashedWriteBuffer);
        requireAllocatedPagesInsideRegion(TEST_FLASH_BASE, TEST_FLASH_SIZE);
        requireFlashUnchangedOutside(before, TEST_FLASH_BASE, TEST_FLASH_SIZE);
    }

#if defined(MEMMAPPER_LARGE_INDICES)
    SECTION("region above physical page 255")
    {
        resetFlash();
        const std::vector<uint8_t> before = snapshotFlash();
        MemMapper mm(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0x0000, 4 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
        REQUIRE(mm.writeMem(0x0042, 0xAB) == MemMapper::Error::Success);
        REQUIRE(mm.doFlash() == MemMapper::FlashedWriteBuffer);
        requireAllocatedPagesInsideRegion(HIGH_FLASH_BASE, HIGH_FLASH_SIZE);
        requireFlashUnchangedOutside(before, HIGH_FLASH_BASE, HIGH_FLASH_SIZE);
    }
#endif
}

#if defined(MEMMAPPER_LARGE_INDICES)
TEST_CASE("MemMapper in a region above physical page 255", "[MemMapper][location]")
{
    SECTION("clean flash: no virtual page is mapped")
    {
        resetFlash();
        MemMapper mm(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);
        REQUIRE(mm.isMapped(0x0000) == false);
        REQUIRE(mm.isMapped(HIGH_FLASH_SIZE - FLASH_PAGE_SIZE) == false);
    }

    SECTION("data written before a restart is read back from the allocated page")
    {
        resetFlash();
        {
            MemMapper mm(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);
            REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
            REQUIRE(mm.writeMem(0x0042, 0xAB) == MemMapper::Error::Success);
            REQUIRE(mm.doFlash() == MemMapper::FlashedWriteBuffer);
        }

        // The table must name the first data page of the region, not a truncated page number
        const uint32_t physicalPage = physicalPageOf(HIGH_FLASH_BASE, 0);
        REQUIRE(physicalPage == ((HIGH_FLASH_BASE / FLASH_PAGE_SIZE) + 1));
        REQUIRE(FLASH[(physicalPage * FLASH_PAGE_SIZE) + 0x42] == 0xAB);

        MemMapper restarted(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);
        REQUIRE(restarted.isMapped(0x0000) == true);
        uint8_t data = 0;
        REQUIRE(restarted.readMem(0x0042, data) == MemMapper::Error::Success);
        REQUIRE(data == 0xAB);
    }

    SECTION("a flushed data page leaves the flash outside the region unchanged")
    {
        resetFlash();
        const std::vector<uint8_t> before = snapshotFlash();
        MemMapper mm(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0x0000, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
        REQUIRE(mm.writeMem(0x0042, 0xAB) == MemMapper::Error::Success);
        REQUIRE(mm.doFlash() == MemMapper::FlashedWriteBuffer);
        requireFlashUnchangedOutside(before, HIGH_FLASH_BASE, HIGH_FLASH_SIZE);
    }

    SECTION("automatically allocated pages stay inside the region")
    {
        resetFlash();
        const std::vector<uint8_t> before = snapshotFlash();
        MemMapper mm(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, true);
        REQUIRE(mm.writeMem(0x0042, 0xEF) == MemMapper::Error::Success);
        REQUIRE(mm.writeMem(0x0142, 0xCD) == MemMapper::Error::Success);
        REQUIRE(mm.doFlash() == MemMapper::FlashedWriteBuffer);
        uint8_t data = 0;
        REQUIRE(mm.readMem(0x0042, data, true) == MemMapper::Error::Success);
        REQUIRE(data == 0xEF);
        REQUIRE(mm.readMem(0x0142, data, true) == MemMapper::Error::Success);
        REQUIRE(data == 0xCD);
        requireAllocatedPagesInsideRegion(HIGH_FLASH_BASE, HIGH_FLASH_SIZE);
        requireFlashUnchangedOutside(before, HIGH_FLASH_BASE, HIGH_FLASH_SIZE);
    }

    SECTION("all data pages of the region fit, the next one is out of memory")
    {
        resetFlash();
        MemMapper mm(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);
        constexpr uint32_t dataPages = (HIGH_FLASH_SIZE / FLASH_PAGE_SIZE) - 1; // page 0 is the table
        REQUIRE(mm.addRange(0, dataPages * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
        for (uint32_t page = 0; page < dataPages; page++)
        {
            REQUIRE(mm.isMapped(page * FLASH_PAGE_SIZE) == true);
        }
        requireAllocatedPagesInsideRegion(HIGH_FLASH_BASE, HIGH_FLASH_SIZE);
        REQUIRE(mm.addRange(dataPages * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE) == MemMapper::Error::OutOfMemory);
    }

    SECTION("the last entry of the allocation table is the last usable virtual page")
    {
        resetFlash();
        MemMapper mm(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);
        constexpr uint32_t lastVirtPage = MemMapper::allocTableSize - 1;
        REQUIRE(mm.addRange(lastVirtPage * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
        REQUIRE(mm.isMapped(lastVirtPage * FLASH_PAGE_SIZE) == true);
        REQUIRE(mm.addRange(MemMapper::allocTableSize * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
    }
}
#endif

TEST_CASE("MemMapper rejects a range behind the end of the allocation table", "[MemMapper][bounds]")
{
    // The test region holds one allocation-table page and dataPages data pages, so a range of
    // dataPages virtual pages needs exactly the whole region: a range that is one page longer can
    // only be rejected by the bounds check, not by a missing flash page.
    constexpr uint32_t dataPages = (TEST_FLASH_SIZE / FLASH_PAGE_SIZE) - 1;
    constexpr uint32_t lastVirtPage = MemMapper::allocTableSize - 1;
    constexpr uint32_t firstVirtPage = MemMapper::allocTableSize - dataPages;

    SECTION("a range that ends at the last table entry is accepted")
    {
        resetFlash();
        const std::vector<uint8_t> before = snapshotFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm.addRange(firstVirtPage * FLASH_PAGE_SIZE, dataPages * FLASH_PAGE_SIZE) ==
                MemMapper::Error::Success);
        for (uint32_t virtPage = firstVirtPage; virtPage <= lastVirtPage; virtPage++)
        {
            INFO("virtual page 0x" << std::hex << virtPage);
            REQUIRE(mm.isMapped(virtPage * FLASH_PAGE_SIZE) == true);
        }
        requireAllocatedPagesInsideRegion(TEST_FLASH_BASE, TEST_FLASH_SIZE);
        requireFlashUnchangedOutside(before, TEST_FLASH_BASE, TEST_FLASH_SIZE);
    }

    SECTION("a range that is one page longer than the table end is rejected")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        const std::vector<uint8_t> before = snapshotFlash();
        REQUIRE(mm.addRange(firstVirtPage * FLASH_PAGE_SIZE, (dataPages + 1) * FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
        requireFlashUnchanged(before);
        requirePagesUnallocated(TEST_FLASH_BASE, 0, lastVirtPage);
    }

    SECTION("ranges that reach past the table end are rejected before anything is allocated")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        const std::vector<uint8_t> before = snapshotFlash();
        // Pages before the end of the table and pages of the range; every pair is too long. With
        // 128 table entries (RP2350) the pair 8/20 is the range 120/20 of the finding.
        constexpr uint32_t ranges[][2] = {{16, 20}, {8, 20}, {4, 8}, {2, 3}, {1, 2}};
        for (const auto& range : ranges)
        {
            const uint32_t virtPage = MemMapper::allocTableSize - range[0];
            INFO("start page 0x" << std::hex << virtPage << ", pages 0x" << range[1]);
            REQUIRE(mm.addRange(virtPage * FLASH_PAGE_SIZE, range[1] * FLASH_PAGE_SIZE) ==
                    MemMapper::Error::InvalidAddress);
        }
        requireFlashUnchanged(before);
        requirePagesUnallocated(TEST_FLASH_BASE, 0, lastVirtPage);
    }

    SECTION("a start page behind the table is rejected")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        const std::vector<uint8_t> before = snapshotFlash();
        REQUIRE(mm.addRange(MemMapper::allocTableSize * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
        REQUIRE(mm.addRange(MemMapper::allocTableSize * FLASH_PAGE_SIZE, dataPages * FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
        REQUIRE(mm.addRange(0x10000, FLASH_PAGE_SIZE) == MemMapper::Error::InvalidAddress);
        requireFlashUnchanged(before);
        requirePagesUnallocated(TEST_FLASH_BASE, 0, lastVirtPage);
    }

    SECTION("very large page aligned lengths are rejected")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        const std::vector<uint8_t> before = snapshotFlash();
        // Every length is a multiple of the page size, so only the end of the range can reject
        // it. The page count of the last one is 0xffffff, the largest that length can express.
        constexpr uint32_t lengths[] = {0x1000000, 0xffff00, 0x80000000, 0xffffff00};
        for (const uint32_t length : lengths)
        {
            INFO("length 0x" << std::hex << length);
            REQUIRE(mm.addRange(0, length) == MemMapper::Error::InvalidAddress);
            REQUIRE(mm.addRange(lastVirtPage * FLASH_PAGE_SIZE, length) == MemMapper::Error::InvalidAddress);
        }
        REQUIRE(mm.addRange(lastVirtPage * FLASH_PAGE_SIZE, 2 * FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
        requireFlashUnchanged(before);
        requirePagesUnallocated(TEST_FLASH_BASE, 0, lastVirtPage);
    }

    SECTION("alignment and zero length keep their error code")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        const std::vector<uint8_t> before = snapshotFlash();
        REQUIRE(mm.addRange(0, 0) == MemMapper::Error::InvalidLength);
        REQUIRE(mm.addRange(0, FLASH_PAGE_SIZE - 1) == MemMapper::Error::InvalidLength);
        REQUIRE(mm.addRange(0, FLASH_PAGE_SIZE + 1) == MemMapper::Error::InvalidLength);
        REQUIRE(mm.addRange(0x0042, FLASH_PAGE_SIZE) == MemMapper::Error::InvalidAddress);
        // The length is judged first, also at the end of the table where the range is too long
        REQUIRE(mm.addRange(lastVirtPage * FLASH_PAGE_SIZE, 0) == MemMapper::Error::InvalidLength);
        REQUIRE(mm.addRange(lastVirtPage * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE + 1) ==
                MemMapper::Error::InvalidLength);
        REQUIRE(mm.addRange((lastVirtPage * FLASH_PAGE_SIZE) + 1, FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
        REQUIRE(mm.addRange(0x0042, 0xffff01) == MemMapper::Error::InvalidLength);
        requireFlashUnchanged(before);
        requirePagesUnallocated(TEST_FLASH_BASE, 0, lastVirtPage);
    }

    SECTION("a rejected range leaves the mapped pages and the object state untouched")
    {
        resetFlash();
        MemMapper mm(TEST_FLASH_BASE, TEST_FLASH_SIZE, false);
        REQUIRE(mm.addRange(0, 2 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
        REQUIRE(mm.writeMem(0x0042, 0xAB) == MemMapper::Error::Success);
        mm.setEndianess(BIG_ENDIAN);
        REQUIRE(mm.setUInt16(0x0110, 0x1234) == MemMapper::Error::Success);
        const uint32_t secondDataPage = physicalPageOf(TEST_FLASH_BASE, 1);
        const std::vector<uint8_t> before = snapshotFlash();

        REQUIRE(mm.addRange(lastVirtPage * FLASH_PAGE_SIZE, 2 * FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
        requireFlashUnchanged(before);

        // writePage and flashMemModified survived: the staged data page is still committed
        REQUIRE(mm.doFlash() == MemMapper::FlashedWriteBuffer);
        uint8_t data = 0;
        REQUIRE(mm.readMem(0x0042, data) == MemMapper::Error::Success);
        REQUIRE(data == 0xAB);
        // endianess survived: the byte order of the multi byte accessors is unchanged
        REQUIRE(mm.getUInt16(0x0110) == 0x1234);
        REQUIRE(mm.getUInt8(0x0110) == 0x12);
        // lastAllocated survived: the next data page of the region is allocated
        REQUIRE(mm.addRange(2 * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE) == MemMapper::Error::Success);
        REQUIRE(physicalPageOf(TEST_FLASH_BASE, 2) == (secondDataPage + 1));
        REQUIRE(mm.isMapped(0x0000) == true);
        REQUIRE(mm.isMapped(0x0100) == true);
        REQUIRE(mm.isMapped(0x0200) == true);
        requireAllocatedPagesInsideRegion(TEST_FLASH_BASE, TEST_FLASH_SIZE);
    }

#if defined(MEMMAPPER_LARGE_INDICES)
    SECTION("the range of 20 pages from virtual page 120 is rejected")
    {
        resetFlash();
        MemMapper mm(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);
        const std::vector<uint8_t> before = snapshotFlash();
        // Virtual page 120 lies inside the 128 entry table, 20 pages reach 12 pages behind its end
        REQUIRE(mm.addRange(120 * FLASH_PAGE_SIZE, 20 * FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
        requireFlashUnchanged(before);
        requirePagesUnallocated(HIGH_FLASH_BASE, 0, MemMapper::allocTableSize - 1);

        // The eight pages up to the last entry are accepted, the page behind it is not
        REQUIRE(mm.addRange(120 * FLASH_PAGE_SIZE, 8 * FLASH_PAGE_SIZE) == MemMapper::Error::Success);
        for (uint32_t virtPage = 120; virtPage < MemMapper::allocTableSize; virtPage++)
        {
            REQUIRE(mm.isMapped(virtPage * FLASH_PAGE_SIZE) == true);
        }
        requireAllocatedPagesInsideRegion(HIGH_FLASH_BASE, HIGH_FLASH_SIZE);
        REQUIRE(mm.addRange(MemMapper::allocTableSize * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);
    }

    SECTION("a rejected range does not write past the object")
    {
        resetFlash();
        // Guard bytes around the object: an index behind the end of the table addresses
        // ramBuffer[index * AllocEntryBytes], which on the RP2350 leaves the 256 byte buffer,
        // overwrites the members behind it and finally the memory behind the object.
        // A local class must not have static data members, so the guard size is a local constant.
        constexpr uint32_t guardSize = 512;
        struct GuardedMapper
        {
            uint8_t before[guardSize];
            alignas(MemMapper) uint8_t object[sizeof(MemMapper)];
            uint8_t after[guardSize];
        };
        GuardedMapper guarded{};
        memset(guarded.before, 0xa5, guardSize);
        memset(guarded.after, 0xa5, guardSize);
        MemMapper* mm = new (guarded.object) MemMapper(HIGH_FLASH_BASE, HIGH_FLASH_SIZE, false);

        REQUIRE(mm->addRange(127 * FLASH_PAGE_SIZE, 32 * FLASH_PAGE_SIZE) ==
                MemMapper::Error::InvalidAddress);

        uint32_t changed = guardSize;
        for (uint32_t i = 0; i < guardSize; i++)
        {
            if ((guarded.before[i] != 0xa5) || (guarded.after[i] != 0xa5))
            {
                changed = i;
                break;
            }
        }
        INFO("first changed guard byte: 0x" << std::hex << changed);
        REQUIRE(changed == guardSize);
        mm->~MemMapper();
    }
#endif
}
