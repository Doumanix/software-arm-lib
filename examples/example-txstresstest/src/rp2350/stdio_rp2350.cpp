/*
 * stdio_rp2350.cpp - stdout of the KNX-Tx stress test example on the RP2350 (Pico 2 test board).
 *
 * The report lines of the group telegram mode (printf in src/app_main.cpp) go to UART1
 * (TX GPIO8, 115200 baud, see CMakeLists.txt). The sblib main() does not initialize the stdio
 * of the Pico SDK, so this is done before main().
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include <pico/stdio.h>

namespace
{
    /**
     * @brief Initialize the stdio of the Pico SDK before main() (runs with the static constructors).
     */
    [[gnu::constructor]] void initStdio()
    {
        stdio_init_all();
    }
} // namespace
