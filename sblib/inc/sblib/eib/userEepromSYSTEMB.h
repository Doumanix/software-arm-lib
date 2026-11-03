#ifndef SBLIB_KNX_USEREEPROM_SYSTEMB_H_
#define SBLIB_KNX_USEREEPROM_SYSTEMB_H_

#include <sblib/eib/userEepromMASK0701.h>

/**
 * The BCU SYSTEM B user EEPROM
 */
class UserEepromSYSTEMB : public UserEepromMASK0701
{
public:
    ///\todo check start of 0x3300, SYSTEM B has typically dynamic memory allocation
    explicit UserEepromSYSTEMB() : UserEepromSYSTEMB(0x3300, 3072, 4096) {}

    [[nodiscard]] uint16_t& addrTabAddr() const override { return directAccess_16(addrTabAddrOffset); }
    [[nodiscard]] uint16_t& assocTabAddr() const override { return directAccess_16(assocTabAddrOffset); }
    [[nodiscard]] virtual uint8_t* addrTabMcb() const { return &directAccess_8(addrTabMcbOffset); }
    [[nodiscard]] virtual uint8_t* assocTabMcb() const { return &directAccess_8(assocTabMcbOffset); }
    [[nodiscard]] virtual uint8_t* commsTabMcb() const { return &directAccess_8(commsTabMcbOffset); }
    [[nodiscard]] virtual uint8_t* commsSeg0Mcb() const { return &directAccess_8(commsSeg0McbOffset); }
    [[nodiscard]] virtual uint8_t* eibObjMcb() const { return &directAccess_8(eibObjMcbOffset); }

protected:
    UserEepromSYSTEMB(const uint32_t start, const uint32_t size, const uint32_t flashSize)
       :
        UserEepromMASK0701(start, size, flashSize) {}

private:
    friend class PropertiesSYSTEMB;

    static constexpr uint32_t addrTabAddrOffset = 0x21;  //!< 0x3321-0x3322
    static constexpr uint32_t assocTabAddrOffset = 0x23; //!< 0x3323-0x3324

    static constexpr uint32_t addrTabMcbOffset = 0x4D;   //!< 0x334D-0x3354
    static constexpr uint32_t assocTabMcbOffset = 0x55;  //!< 0x3355-0x335C
    static constexpr uint32_t commsTabMcbOffset = 0x5D;  //!< 0x335D-0x3364
    static constexpr uint32_t eibObjMcbOffset = 0x65;    //!< 0x3365-0x336C
    static constexpr uint32_t commsSeg0McbOffset = 0x6D; //!< 0x336D-0x3374
    static constexpr uint32_t eibObjVerOffset = 0x75;    //!< 0x3375-0x3379 Application program 1 version
    static constexpr uint32_t commsSeg0VerOffset = 0x7A; //!< 0x337A-0x337E Application program 2 version
    static constexpr uint32_t commsSeg0PeiTypeOffset = 0x7F; //!< 0x337F Application program 2 PEI type

    ///\todo These two asserts fail, see UserEeprom::directAccess_16 for details.
    ///      The offset must be 2-byte aligned to avoid unaligned memory access.
    //static_assert(addrTabAddrOffset   % 2 == 0, "addrTabAddrOffset must be 2-byte aligned");
    //static_assert(assocTabAddrOffset   % 2 == 0, "assocTabAddrOffset must be 2-byte aligned");
};

#endif /* SBLIB_KNX_USEREEPROM_SYSTEMB_H_ */
