/*
 * Chip identity stub for host tests.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#ifndef SBLIB_TEST_RP2350_SYSINFO_H_
#define SBLIB_TEST_RP2350_SYSINFO_H_
#include <cstdint>
struct sim_sysinfo_t
{
    uint32_t chip_id = 0x2350;
};
inline sim_sysinfo_t sim_sysinfo;
#define sysinfo_hw (&sim_sysinfo)
#endif
