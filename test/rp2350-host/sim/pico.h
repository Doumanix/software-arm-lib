/*
 * Pico SDK pico.h for host tests: what the original hardware/pio_instructions.h of the SDK needs (see pico_sim.h)
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once
#include "pico_sim.h"

#define PARAM_ASSERTIONS_ENABLED(x) 0
#define valid_params_if(x, test) ((void)0)
