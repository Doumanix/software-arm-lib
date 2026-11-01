/*
 * Flash model controls for host tests.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#ifndef SBLIB_TEST_RP2350_FLASH_SIM_H_
#define SBLIB_TEST_RP2350_FLASH_SIM_H_
#include <cstdint>
extern uint8_t FLASH[];
namespace sim
{
    void resetFlash();
    extern bool failErase;
    extern bool failProgram;
    extern unsigned eraseCalls;
    extern unsigned programCalls;
} // namespace sim
#endif
