#ifndef SBLIB_KNX_USEREEPROM_MASK0705_H_
#define SBLIB_KNX_USEREEPROM_MASK0705_H_

#include <sblib/eib/userEepromMASK0701.h>

/**
 * The BCU BIM112 MaskVersion 0x0705 user EEPROM
 */
class UserEepromMASK0705 : public UserEepromMASK0701
{
public:
    /// BIM112 UserEeprom range is always 0x4000 - 0xcfff and in most cases starts with the group address table
    /// We add 0x100 before it (0x3f00-0x3fff), the load states, table addresses, ... are saved at 0x3f70-0x3fd5,
    /// see Issue #112 https://github.com/selfbus/software-arm-lib/issues/112
    explicit UserEepromMASK0705() : UserEepromMASK0705(0x3f00, 3072, 4096) {}

protected:
    // BIM M112 (Mask 0x0705) has a fixed start and size of the EEPROM, so make constructor protected
    UserEepromMASK0705(const uint32_t start, const uint32_t size, const uint32_t flashSize)
       :
        UserEepromMASK0701(start, size, flashSize) {}
};

#endif /* SBLIB_KNX_USEREEPROM_MASK0705_H_ */
