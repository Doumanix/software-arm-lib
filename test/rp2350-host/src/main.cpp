/*
 *  main.cpp - Host unit tests of the RP2350 port (Catch main), see ../README.md
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */
#include <stdexcept>

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

// sblib fatalError() (sblib/utils.h): a test failure instead of the blinking LED
void fatalError()
{
    throw std::runtime_error("fatalError() called");
}
