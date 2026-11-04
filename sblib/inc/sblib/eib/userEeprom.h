/*
 * userEeprom.h
 *
 *  Created on: 19.11.2021
 *      Author: dridders
 */

#ifndef SBLIB_KNX_USEREEPROM_H_
#define SBLIB_KNX_USEREEPROM_H_

#include <sblib/eib/memory.h>
#include <sblib/platform.h>


/**
 * The user EEPROM
 * @details Can be accessed by name, like userEeprom.manuDataH() and as an array, like
 *          userEeprom[addr]. Please note that the @ref startAddress is subtracted.
 *          E.g. userEeprom[0x107] is the correct address for userEeprom.version() not userEeprom[0x07].
 *
 * @note KNX Spec. 3.0 9/4/1 Basic and System Components
 *      - BCU 1 (256 bytes), 3.1.10.3.1 p.13ff
 *      - BCU 2 (1024 bytes), 5.1.2.12.5 p.45ff
 *      - BIM M112, not in Spec. 3.0 some information in 06 Profiles 4.2.10 User EEPROM p. 41
 * @note KNX Spec. 3.0 06 Profiles 4.2.10 User EEPROM p. 41
 *      - BCU 1 0x119 - 0x1fe (only the range of the UserEEPROM, not the whole EEPROM)
 *      - BCU 2 0x119 - 0x46f (only the range of the UserEEPROM, not the whole EEPROM)
 *      - BIM M112 0x4000 - 0xcfff
 */
class UserEeprom : public Memory
{
public:
    // Delete default and assigment constructors
    UserEeprom() = delete;
    UserEeprom(const UserEeprom&) = delete;
    UserEeprom(UserEeprom&&) = delete;
    UserEeprom& operator=(const UserEeprom&) = delete;
    UserEeprom& operator=(UserEeprom&&) = delete;

    explicit UserEeprom(uint32_t start, uint32_t size, uint32_t flashSize);

    /**
     * The RAM shadow of the user EEPROM.
     *
     * @warning Will become private once all accesses go through the API. Use new accesses only through
     *          getUInt8/16/32, read, setUInt8/16/32, write and fill.
     */
    ///\todo Enable deprecation warning after making userEepromData private
    //[[deprecated("Use getUInt8, getUInt16, getUInt32 or the [] operator.")]]
    alignas(FLASH_RAM_BUFFER_ALIGNMENT) uint8_t* userEepromData; // must be word aligned, otherwise iapProgram will fail

    [[nodiscard]] virtual uint8_t& optionReg() const = 0;
    [[nodiscard]] virtual uint8_t& manuDataH() const = 0;
    [[nodiscard]] virtual uint8_t& manuDataL() const = 0;
    [[nodiscard]] virtual uint8_t& manufacturerH() const = 0;
    [[nodiscard]] virtual uint8_t& manufacturerL() const = 0;
    [[nodiscard]] virtual uint8_t& deviceTypeH() const = 0;
    [[nodiscard]] virtual uint8_t& deviceTypeL() const = 0;
    [[nodiscard]] virtual uint8_t& version() const = 0;
    [[nodiscard]] virtual uint8_t& checkLimit() const = 0;
    [[nodiscard]] virtual uint8_t& appPeiType() const = 0;
    [[nodiscard]] virtual uint8_t& syncRate() const = 0;
    [[nodiscard]] virtual uint8_t& portCDDR() const = 0;
    [[nodiscard]] virtual uint8_t& portADDR() const = 0;
    [[nodiscard]] virtual uint8_t& runError() const = 0;
    [[nodiscard]] virtual uint8_t& routeCnt() const = 0;
    [[nodiscard]] virtual uint8_t& maxRetransmit() const = 0;
    [[nodiscard]] virtual uint8_t& confDesc() const = 0;
    [[nodiscard]] virtual uint8_t& assocTabPtr() const = 0;
    [[nodiscard]] virtual uint8_t& commsTabPtr() const = 0;
    [[nodiscard]] virtual uint8_t& usrInitPtr() const = 0;
    [[nodiscard]] virtual uint8_t& usrProgPtr() const = 0;

    [[nodiscard]] virtual uint8_t& addrTabSize() const = 0;
    [[nodiscard]] virtual uint8_t* addrTab() const = 0;

    /**
     * Access the user EEPROM like an ordinary array.
     *
     * @param address  The address of the data byte to access.
     * @return The data byte.
     */
    uint8_t& operator[](uint32_t address) const override;

    /**
     * Access the user EEPROM like an ordinary array.
     *
     * @param address  The address of the data byte to access.
     * @return The data byte.
     */
    uint8_t& operator[](uint32_t address) override;

    /**
     * @note The write functions of @ref Memory (setUInt8/16/32, write and fill) mark the user EEPROM as modified
     *       (see @ref modified) only if at least one byte really changes. Writing the current content keeps the
     *       flash untouched.
     */

    /**
     * Mark/unmark the user EEPROM as modified. The EEPROM will be written to flash when the
     * bus is idle, all telegrams are processed, and no direct data connection is open.
     *
     * @param newModified Set true to mark the eeprom as modified
     *
     */
    void modified(bool newModified);

    /**
     * Test if the user EEPROM is modified.
     */
    [[nodiscard]] bool isModified() const;

    [[nodiscard]] bool writeDelayElapsed() const;

    [[nodiscard]] uint32_t flashSize() const;

    [[nodiscard]] uint32_t numEepromPages() const;
    [[nodiscard]] uint8_t* lastEepromPage() const;
    [[nodiscard]] uint8_t* flashSectorAddress() const;

    /**
     * If user-eeprom is modified, changes are written to the mcu's flash
     * @warn While the eeprom is written, all interrupts are disabled.
     */
    void writeUserEeprom();
    void readUserEeprom();

protected:
    /**
     * Finds the last valid page in the flash sector.
     *
     * @details The search is done backwards from the end of the mcu flash in  @ref FLASH_SECTOR_SIZE steps.
     *
     * @return If successful: number of the last valid flash page, otherwise 0
     */
    [[nodiscard]] uint8_t* findValidPage() const;

    /**
     * @brief Access the internal @ref userEepromData array directly by raw index, without
     *        any @ref startAddress offset subtraction.
     *
     * @param index  Zero-based index into @ref userEepromData. Must satisfy
     *               <tt>startAddress + index</tt> within the valid array range,
     *               otherwise @ref fatalError() is called.
     * @return Reference to the byte at @ref userEepromData[index].
     * @warning This method is intended for internal use only.
     */
    [[nodiscard]] uint8_t& directAccess_8(uint32_t index) const;

    /**
     * @brief Access the internal @ref userEepromData array directly by raw index, without
     *        any @ref startAddress offset subtraction.
     *
     * @note  Internally delegates to @ref directAccess_8() and reinterprets the result
     *        as a 16-bit reference via @c reinterpret_cast (little-endian).
     *
     * @warning **Alignment is mandatory.**
     *          The target platform (ARM Cortex-M0 / LPC11xx) does **not** support
     *          unaligned multibyte memory accesses. Passing an odd @p index will
     *          cause a **HardFault** at runtime, and is also Undefined Behavior
     *          in C++ (strict aliasing / alignment rules).\n
     *          Always verify the offset with a compile-time check before use, e.g.:
     *          @code
     *          static_assert(myOffset % 2 == 0, "myOffset must be 2-byte aligned");
     *          @endcode
     *          The base pointer @ref userEepromData is guaranteed to be aligned to
     *          @ref FLASH_RAM_BUFFER_ALIGNMENT, so only @p index itself needs to be even.
     *
     * @param index  Zero-based, **even** index into @ref userEepromData pointing to the
     *               low byte of the 16-bit value. Must satisfy
     *               <tt>startAddress + index</tt> within the valid address range,
     *               otherwise @ref fatalError() is called.
     * @return       Reference to the two bytes at @ref userEepromData[index] interpreted
     *               as @c uint16_t.
     * @warning This method is intended for internal use only.
     */
    [[nodiscard]] uint16_t& directAccess_16(uint32_t index) const;

    bool userEepromModified = false;
    uint32_t writeUserEepromTime = 0;

    const uint32_t userEepromFlashSize;
};


#endif /* SBLIB_KNX_USEREEPROM_H_ */
