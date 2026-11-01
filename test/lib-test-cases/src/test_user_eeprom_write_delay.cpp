#include "knx/test_user_eeprom.h"
#include <sblib/timer.h>
#include <iap_emu.h>
#include <cstdint>
#include <cstdio>
#include <catch.hpp>

namespace
{
    void observe(TestUserEeprom& eeprom, const char* scenario, const uint32_t now, const bool expected)
    {
        setMillis(now);
        const bool actual = eeprom.writeDelayElapsed();
        std::printf("ROW,%s,%u,%d,%d\n", scenario, now, actual, expected);
        INFO(scenario << " now=" << now);
        CHECK(actual == expected);
    }
}

TEST_CASE("EEPROM delay strict historical deadline", "[eeprom-delay]")
{
    IAP_Init_Flash(0xff);
    TestUserEeprom eeprom(0x100, 256, 256);
    observe(eeprom, "unmodified-start", 0, false);
    observe(eeprom, "unmodified-later", 100, false);
    eeprom.modified(true);
    CHECK(eeprom.isModified());
    observe(eeprom, "modified-0ms", 100, false);
    observe(eeprom, "modified-49ms", 149, false);
    observe(eeprom, "modified-50ms", 150, false);
    observe(eeprom, "modified-51ms", 151, true);
    eeprom.modified(false);
    CHECK_FALSE(eeprom.isModified());
    observe(eeprom, "cleared", 152, false);
    delete[] eeprom.userEepromData;
}

TEST_CASE("EEPROM delay renewed by every modification", "[eeprom-delay]")
{
    IAP_Init_Flash(0xff);
    TestUserEeprom eeprom(0x100, 256, 256);
    setMillis(100);
    eeprom.modified(true);
    setMillis(149);
    eeprom.modified(true);
    observe(eeprom, "renewed-0ms", 149, false);
    observe(eeprom, "renewed-49ms", 198, false);
    observe(eeprom, "renewed-50ms", 199, false);
    observe(eeprom, "renewed-51ms", 200, true);
    delete[] eeprom.userEepromData;
}

TEST_CASE("EEPROM delay across unsigned and signed boundaries", "[eeprom-delay]")
{
    const uint32_t starts[] = { UINT32_MAX - 25, INT32_MAX - 25, INT32_MAX - 75 };
    for (const uint32_t start : starts)
    {
        IAP_Init_Flash(0xff);
        TestUserEeprom eeprom(0x100, 256, 256);
        setMillis(start);
        eeprom.modified(true);
        observe(eeprom, "boundary-0ms", start, false);
        observe(eeprom, "boundary-49ms", start + 49, false);
        observe(eeprom, "boundary-50ms", start + 50, false);
        observe(eeprom, "boundary-51ms", start + 51, true);
        observe(eeprom, "boundary-100ms", start + 100, true);
        delete[] eeprom.userEepromData;
    }
}

TEST_CASE("Explicit EEPROM write bypasses delay", "[eeprom-delay]")
{
    IAP_Init_Flash(0xff);
    TestUserEeprom eeprom(0x100, 256, 256);
    setMillis(100);
    eeprom.modified(true);
    observe(eeprom, "explicit-before-write", 100, false);
    const int callsBefore = iap_calls[I_RAM2FLASH];
    eeprom.writeUserEeprom();
    CHECK(iap_calls[I_RAM2FLASH] > callsBefore);
    CHECK_FALSE(eeprom.isModified());
    observe(eeprom, "explicit-after-write", 100, false);
    delete[] eeprom.userEepromData;
}
