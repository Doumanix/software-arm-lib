#ifndef SBLIB_KNX_USEREEPROM_MASK0701_H_
#define SBLIB_KNX_USEREEPROM_MASK0701_H_

#include <sblib/eib/userEepromBCU2.h>


/** Number of interface objects supported */
constexpr uint8_t INTERFACE_OBJECT_COUNT = 8;

/**
 * The BCU BIM M112 MaskVersion 0x0701 User EEPROM
 * @details Can be accessed by name, like userEeprom.manuDataH() and as an array, like
 *          userEeprom[addr]. Please note that the @ref startAddress is subtracted.
 *          E.g. userEeprom[0x107] is the correct address for userEeprom.version() not userEeprom[0x07].
 *
 * @note KNX Spec. 3.0 06 Profiles 4.2.10 User EEPROM p.41
 *       - User EEPROM BIM M112: 0x4000 - 0xcfff (36864 bytes / 36kB)
 */
class UserEepromMASK0701 : public UserEepromBCU2
{
public:
    /// BIM112 UserEeprom range is always 0x4000 - 0xcfff and in most cases starts with the group address table
    /// We add 0x100 before it (0x3f00-0x3fff), the load states, table addresses, ... are saved at 0x3f70-0x3fd5,
    /// see Issue #112 https://github.com/selfbus/software-arm-lib/issues/112
    explicit UserEepromMASK0701() : UserEepromMASK0701(0x3f00, 3072, 4096) {}

    /// Bytes the fields of the system interface objects (load states, table addresses, serial number, ...) are moved
    /// down from the BCU2 layout: 0x370-0x3d5 + 0x3f00 - 0x300 = 0x3f70-0x3fd5, below the application at 0x4000
    static constexpr uint16_t fieldsShiftMask0701 = 0x300;

protected:
    // BIM M112 (Mask 0x0701) has a fixed start and size of the EEPROM, so make constructor protected
    UserEepromMASK0701(const uint32_t start, const uint32_t size, const uint32_t flashSize,
                       const uint16_t shift = fieldsShiftMask0701)
       :
        UserEepromBCU2(start, size, flashSize, shift) {}
};

#endif /* SBLIB_KNX_USEREEPROM_MASK0701_H_ */
