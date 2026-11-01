# Porting an LPC11xx application to the RP2350

Step by step, using the out8-bcu1 application (variants `o8_pico2tb_*`, Pico 2 test board) as the
example. Read [README.md](README.md) first for the build requirements and
[hardware-abstraction.md](hardware-abstraction.md) for what is shared between the platforms.

Reference material in this repository:

| Example | Shows |
|---|---|
| `examples/rp2350-smoke` | Minimal build and board check: LED, UART, timer interrupt, flash erase/program |
| `examples/rp2350-knx-bcu1` | Complete BCU1 device with the sblib `main()`, as an application would use it |
| `examples/rp2350-bcu-types` | Link check of BCU2, MASK0701, MASK0705 and SYSTEMB |

## 1. Keep one source tree

Port the application in place instead of copying it. Platform differences go behind
`SBLIB_PLATFORM_RP2350` / `SBLIB_PLATFORM_LPC11XX` (from `sblib/platform_detect.h`) and, for pin
tables, behind the board macro `SBLIB_BOARD_RP2350_PICO2_TESTBOARD`. In out8-bcu1 only three
places differ: the output pin table (`inc/config.h`), the GPIO access (`inc/outputs.h`) and the
random start delay (`src/app_out8.cpp`).

## 2. CMake: a separate RP2350 build file

Select the platform in the application's `CMakeLists.txt` and hand over to a second file, as
out8-bcu1 does:

```
set(SBLIB_MCU "LPC1115" CACHE STRING "Target microcontroller (LPC1115, RP2350)")
if(SBLIB_MCU STREQUAL "RP2350")
    include(${CMAKE_CURRENT_LIST_DIR}/out8-bcu1-rp2350.cmake)
    return()
endif()
```

The RP2350 file (`out8-bcu1-rp2350.cmake`) does, in this order:

1. include `<sblib>/cmake/rp2350/pico_sdk_setup.cmake` **before** `project()`,
2. `project(<name> LANGUAGES C CXX ASM ...)`, then `pico_sdk_init()`,
3. `add_subdirectory(<sblib>/sblib sblib)`, which defines `sblib_rp2350`,
4. reuse the source list of the LPC build and remove the LPC-only files (the LPC startup code and
   sources that use the LPC11xx ADC),
5. `add_executable(...)` and `target_link_libraries(... pico_stdlib sblib_rp2350)`,
6. turn stdio off if the UART pins are used otherwise (`pico_enable_stdio_uart(<target> 0)`),
7. post-build: `size`, and `objcopy` to `.hex` and `.bin`.

The Pico SDK toolchain does not set `CMAKE_SIZE`; the examples look for `arm-none-eabi-size` next
to the compiler. Define the build variants as a list of name and defines (out8-bcu1:
`o8_pico2tb_bi` with `BI_STABLE`, `o8_pico2tb_mono` with `OUT8_RP_MONOSTABLE`) and add one
executable per entry.

## 3. Presets

Add a configure preset `rp2350-pico2` next to the LPC presets. The variables are the same in
the sblib, the examples and out8-bcu1:

```
"SBLIB_MCU": "RP2350",
"SBLIB_BOARD": "pico2_testboard",
"PICO_PLATFORM": "rp2350-arm-s",
"PICO_BOARD": "pico2",
"PICO_SDK_PATH": "$env{PICO_SDK_PATH}"
```

Add the build presets `debug-rp2350-pico2` and `release-rp2350-pico2` (and a workflow if you
want both). Set `PICO_SDK_PATH`, `ARM_TOOLCHAIN_PATH` (or `MCUXPRESSO_TOOLCHAIN_PATH`) and
`PICOTOOL_DIR` in the environment.

## 4. `config.h` and the board header

- The sblib pin names (`PIN_EIB_TX`, `PIN_EIB_RX`, `PIN_PROG`, `PIN_RUN`, ...) come from the board
  header selected by `SBLIB_BOARD` (`sblib/inc/sblib/hardware/gpio_rp2350_pico2_testboard.h`). For
  your own hardware use `SBLIB_BOARD=custom` with `SBLIB_BOARD_HEADER=<file>`.
- `GPIO0` cannot be the programming pin: `BcuBase` treats the pin value 0 as "no programming pin".
- Pins reserved by the Pico 2 itself (GPIO23, 24, 25, 29) and the pins the board header already
  uses are not free. Free on the test board: GPIO2, 3, 6, 7, 10 to 21 and 26.
- Application pin tables are guarded by the board macro. out8-bcu1 uses an eight-channel table
  for the bi-stable variant (ON pins GPIO2, 3, 6, 7, 10 to 13; OFF pins GPIO14 to 21) and
  produces `#error` for RP2350 boards without a table, so a new board cannot silently use the
  wrong pins.
- Options the RP2350 does not support yet must fail at compile time (out8-bcu1 rejects
  `HAND_ACTUATION`, `BUSFAIL` and `ZERO_DETECT` on the Pico 2 test board).

## 5. Code changes

- **GPIO access:** use the portable sblib functions (`pinMode()`, `digitalWrite()`,
  `portWriteMask()`). `LPC_GPIO` and `gpioPorts[]` exist only on the LPC11xx. On the RP2350
  `portWriteMask(0, mask, value)` addresses GPIO0 to 31 and port 1 GPIO32 to 47.
- **Drivers not ported:** ADC, I2C, SPI and serial are not in the RP2350 library. Replace the use
  (out8-bcu1 does not read an ADC pin for its random start delay; it uses `hwRandom()`), or
  exclude the file in the RP2350 build as out8-bcu1 does for its bus voltage code.
- **Startup:** do not link the LPC startup code or linker script. The Pico SDK runtime and the
  sblib `main()` are used; the sblib adds the storage region to the linker script.
- **Flash and RAM:** flash layout is not hard coded. Persistent data goes through `UserEeprom` and
  `MemMapper` in the storage area at the end of the flash (`SBLIB_RP2350_STORAGE_SIZE`).

## 6. Pitfalls

**Internal pull-downs (RP2350-E9).** The RP2350 has a silicon erratum (E9, see the Raspberry Pi
RP2350 datasheet) that makes the internal pull-down unreliable on an input. Do not rely on a
floating input or an internal pull-down to read a defined low level or to gather entropy: the
out8-bcu1 comment in `inc/config.h` replaces the floating-ADC-pin trick with `hwRandom()` for
this reason. Where an input needs a defined level, use an external resistor; see the datasheet
for the values.

**Flash pauses.** The flash cannot be read while it is erased or programmed. `iap.cpp` disables
interrupts around each flash operation and the Pico SDK flash functions run from RAM. A
program cycle therefore stops the bus ISR for its duration. `BcuDefault::flushUserMemory()` calls
`Bus::pause()` before `UserEeprom::writeUserEeprom()` so that no telegram is cut in half. A pause
longer than about 85 ms (three repetitions of the sender) can lose a telegram, as the host test
documents (`test/rp2350-host`, `pause()`/`resume()` cases). Keep flash writes short and do not start your own long flash operations from the application.
The diagnostic counters of `Bus::pause()` are available with `SBLIB_RP2350_TIMER_STATS`.

**Interrupt code in RAM.** Code that runs in the bus-timing path must not be fetched from flash,
otherwise a cache miss changes the pulse timing. The sblib timer emulation uses
`__not_in_flash_func` (macro `TIMER_RAM_FUNC` in `src/rp2350/timer.cpp`) and takes its alarm
interrupts directly from the vector table in RAM. Application interrupt handlers that are
time critical should be placed the same way. `Bus::pause()` and the other bus code in `bus.cpp`
run from flash; this is covered by the host tests with slow interrupt handlers.

**Pin function of the TX pin.** With `SBLIB_RP2350_PIO_BUS` the `OUTPUT_MATCH` pin mode of
`PIN_EIB_TX` selects the PIO function; with the GPIO bus it is a plain output. Do not configure
`PIN_EIB_TX` or `PIN_EIB_RX` in the application.

**Open drain outputs** do not exist on the RP2350 (`fatalError()` in `digital_pin.cpp`). The LPC
workaround for `PIO0_5` in the out8-bcu1 pin table is therefore not needed there.

**Bus power-fail detection** (`BUSFAIL`, ADC based) is not available.

## 7. Tests

1. Build the library tests and the RP2350 host tests (`test/lib-test-cases`, `test/rp2350-host`,
   see [README.md](README.md)); both run on the development host without hardware.
2. Build every RP2350 variant of your application in Release and Debug. Add an example-style
   link check if you add a new BCU type.
3. Add application tests that run against the LPC emulation on the host, as for the LPC build.
4. Only then flash a test board and compare the bus behaviour with the LPC11xx device, see
   [tp1-timing.md](tp1-timing.md) for how the timing of a test board was measured.
