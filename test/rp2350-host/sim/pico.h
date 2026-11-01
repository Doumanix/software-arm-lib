// Pico SDK pico.h for host tests: what the original hardware/pio_instructions.h of the SDK needs (see pico_sim.h)
#pragma once
#include "pico_sim.h"

#define PARAM_ASSERTIONS_ENABLED(x) 0
#define valid_params_if(x, test) ((void)0)
