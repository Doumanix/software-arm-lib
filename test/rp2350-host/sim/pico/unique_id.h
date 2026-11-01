/*
 * Deterministic board identity for host tests.
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#ifndef SBLIB_TEST_RP2350_UNIQUE_ID_H_
#define SBLIB_TEST_RP2350_UNIQUE_ID_H_
#include <cstdint>
#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8
struct pico_unique_board_id_t
{
    uint8_t id[8];
};
inline void pico_get_unique_board_id(pico_unique_board_id_t* id)
{
    for (uint8_t i = 0; i < 8; ++i)
    {
        id->id[i] = i + 1;
    }
}
#endif
