#ifndef SBLIB_KNX_USEREEPROM_SYSTEMB_H_
#define SBLIB_KNX_USEREEPROM_SYSTEMB_H_

#include <sblib/eib/userEepromMASK0701.h>
#include <algorithm>
#include <cstdint>

/**
 * The BCU SYSTEM B user EEPROM
 */
class UserEepromSYSTEMB : public UserEepromMASK0701
{
public:
    ///\todo check start of 0x3300, SYSTEM B has typically dynamic memory allocation
    static constexpr uint32_t eepromStart = 0x3300; //!< 0x3300-0x3EFF
    static constexpr uint32_t eepromSize = 3072;

    /**
     * The data relative allocation (KNX 03_05_03 3.5.1.2) hands out [allocationStart, allocationEnd) ascending.
     * Never allocated are the own head 0x3300-0x337F (table addresses, MCBs, versions) and the head inherited from
     * BCU2 0x3670-0x36D5 (load states, table addresses, serial number, order info, BCU2 MCBs and versions).
     * The range 0x3380-0x366F is unused so far.
     */
    static constexpr uint32_t allocationOffset = 0x400;
    static constexpr uint32_t allocationStart = eepromStart + allocationOffset; //!< 0x3700
    static constexpr uint32_t allocationEnd = eepromStart + eepromSize;         //!< 0x3F00, behind the last address

    explicit UserEepromSYSTEMB() : UserEepromSYSTEMB(eepromStart, eepromSize, 4096) {}

    [[nodiscard]] uint16_t& addrTabAddr() const override { return directAccess_16(addrTabAddrOffset); }
    [[nodiscard]] uint16_t& assocTabAddr() const override { return directAccess_16(assocTabAddrOffset); }
    [[nodiscard]] virtual uint8_t* addrTabMcb() const { return &directAccess_8(addrTabMcbOffset); }
    [[nodiscard]] virtual uint8_t* assocTabMcb() const { return &directAccess_8(assocTabMcbOffset); }
    [[nodiscard]] virtual uint8_t* commsTabMcb() const { return &directAccess_8(commsTabMcbOffset); }
    [[nodiscard]] virtual uint8_t* commsSeg0Mcb() const { return &directAccess_8(commsSeg0McbOffset); }
    [[nodiscard]] virtual uint8_t* eibObjMcb() const { return &directAccess_8(eibObjMcbOffset); }

    /// Setters for the table addresses, they write at the SYSTEM B offsets through the Memory API
    bool setAddrTabAddr(const uint16_t value) override { return setShadow16(addrTabAddrOffset, value); }
    bool setAssocTabAddr(const uint16_t value) override { return setShadow16(assocTabAddrOffset, value); }

    /**
     * Setters for the memory control blocks (MCB) of the five tables, they write through the Memory API.
     *
     * @param tableIdx  0 address table, 1 association table, 2 communication objects table,
     *                  3 EIB object table, 4 communication objects segment 0
     * @return false if @p tableIdx is invalid or the write is rejected
     */
    bool setMcbSize(const uint32_t tableIdx, const uint32_t size)
    {
        if (tableIdx >= mcbCount)
            return false;

        const uint8_t bytes[4] = {static_cast<uint8_t>(size >> 24), static_cast<uint8_t>(size >> 16),
                                  static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size)};
        return write(startAddr() + mcbOffsets[tableIdx] + mcbSizeOffset, bytes, 4);
    }

    bool setMcbCrc(const uint32_t tableIdx, const uint16_t crc)
    {
        if (tableIdx >= mcbCount)
            return false;

        const uint8_t bytes[2] = {static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc)};
        return write(startAddr() + mcbOffsets[tableIdx] + mcbCrcOffset, bytes, 2);
    }

    /// Size of the table in its MCB, 0 if @p tableIdx is invalid
    [[nodiscard]] uint32_t mcbSize(const uint32_t tableIdx) const
    {
        if (tableIdx >= mcbCount)
        {
            return 0;
        }

        const uint8_t* bytes = &directAccess_8(mcbOffsets[tableIdx] + mcbSizeOffset);
        return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
               (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
    }

    /// Address of the table, 0 if it is not allocated or @p tableIdx is invalid
    [[nodiscard]] uint32_t tableAddr(const uint32_t tableIdx) const
    {
        switch (tableIdx)
        {
            case 0:
                return addrTabAddr();
            case 1:
                return assocTabAddr();
            case 2:
                return commsTabAddr();
            case 3:
                return eibObjAddr();
            case 4:
                return commsSeg0Addr();
            default:
                return 0;
        }
    }

    /// Sets the address of the table through the setters, false if @p tableIdx is invalid or the write is rejected
    bool setTableAddr(const uint32_t tableIdx, const uint16_t value)
    {
        switch (tableIdx)
        {
            case 0:
                return setAddrTabAddr(value);
            case 1:
                return setAssocTabAddr(value);
            case 2:
                return setCommsTabAddr(value);
            case 3:
                return setEibObjAddr(value);
            case 4:
                return setCommsSeg0Addr(value);
            default:
                return false;
        }
    }

    /**
     * Upper end of the segments of the tables in use (address != 0, size != 0), at least allocationStart. The table
     * @p exceptIdx is not counted, so that its segment is free for a new allocation.
     *
     * @param exceptIdx table to ignore, a value >= 5 ignores none
     * @return first address behind the topmost segment
     */
    [[nodiscard]] uint32_t allocatedEnd(const uint32_t exceptIdx = mcbCount) const
    {
        uint32_t end = allocationStart;
        for (uint32_t idx = 0; idx < mcbCount; idx++)
        {
            const uint32_t addr = tableAddr(idx);
            const uint32_t size = mcbSize(idx);
            if ((idx == exceptIdx) || (addr == 0) || (size == 0))
            {
                continue;
            }

            const uint64_t segmentEnd = static_cast<uint64_t>(addr) + size;
            if (segmentEnd > end)
            {
                end = static_cast<uint32_t>(std::min<uint64_t>(segmentEnd, UINT32_MAX));
            }
        }
        return end;
    }

    /**
     * Start of the lowest segment of another table at or above @p address, size 0 segments are not counted.
     *
     * @param exceptIdx table to ignore, a value >= 5 ignores none
     * @return the start address or allocationEnd if there is none
     */
    [[nodiscard]] uint32_t nextSegmentStart(const uint32_t address, const uint32_t exceptIdx = mcbCount) const
    {
        uint32_t next = allocationEnd;
        for (uint32_t idx = 0; idx < mcbCount; idx++)
        {
            const uint32_t addr = tableAddr(idx);
            if ((idx != exceptIdx) && (addr != 0) && (mcbSize(idx) != 0) && (addr >= address) && (addr < next))
            {
                next = addr;
            }
        }
        return next;
    }

protected:
    UserEepromSYSTEMB(const uint32_t start, const uint32_t size, const uint32_t flashSize)
       :
        UserEepromMASK0701(start, size, flashSize, 0) {}

private:
    friend class PropertiesSYSTEMB;

    static constexpr uint32_t addrTabAddrOffset = 0x21;  //!< 0x3321-0x3322
    static constexpr uint32_t assocTabAddrOffset = 0x23; //!< 0x3323-0x3324

    static constexpr uint32_t addrTabMcbOffset = 0x4D;   //!< 0x334D-0x3354
    static constexpr uint32_t assocTabMcbOffset = 0x55;  //!< 0x3355-0x335C
    static constexpr uint32_t commsTabMcbOffset = 0x5D;  //!< 0x335D-0x3364
    static constexpr uint32_t eibObjMcbOffset = 0x65;    //!< 0x3365-0x336C
    static constexpr uint32_t commsSeg0McbOffset = 0x6D; //!< 0x336D-0x3374
    static constexpr uint32_t mcbCount = 5;
    static constexpr uint32_t mcbSizeOffset = 2; //!< Size of the table, 4 bytes big endian, within the MCB
    static constexpr uint32_t mcbCrcOffset = 6;  //!< CRC16 of the table, 2 bytes big endian, within the MCB
    static constexpr uint32_t mcbOffsets[mcbCount] = {addrTabMcbOffset, assocTabMcbOffset, commsTabMcbOffset,
                                                      eibObjMcbOffset, commsSeg0McbOffset};
    static constexpr uint32_t eibObjVerOffset = 0x75;    //!< 0x3375-0x3379 Application program 1 version
    static constexpr uint32_t commsSeg0VerOffset = 0x7A; //!< 0x337A-0x337E Application program 2 version
    static constexpr uint32_t commsSeg0PeiTypeOffset = 0x7F; //!< 0x337F Application program 2 PEI type

    ///\todo These two asserts fail, see UserEeprom::directAccess_16 for details.
    ///      The offset must be 2-byte aligned to avoid unaligned memory access.
    //static_assert(addrTabAddrOffset   % 2 == 0, "addrTabAddrOffset must be 2-byte aligned");
    //static_assert(assocTabAddrOffset   % 2 == 0, "assocTabAddrOffset must be 2-byte aligned");
};

#endif /* SBLIB_KNX_USEREEPROM_SYSTEMB_H_ */
