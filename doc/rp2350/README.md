# RP2350 / RP2354 support of the sblib

Overview of the RP2350 port: what is supported, how it is selected and built, and where the
details are documented. The sblib is licensed under the GPL version 3 (see `LICENSE` in the
repository root); components of the Pico SDK are covered in
[third-party-notices.md](third-party-notices.md).

| Document | Content |
|---|---|
| [porting-guide.md](porting-guide.md) | Step by step: bring an LPC11xx application to the RP2350 |
| [hardware-abstraction.md](hardware-abstraction.md) | Which headers are shared, which implementation exists per platform |
| [tp1-timing.md](tp1-timing.md) | Measured TP1 bus timing on the test board |
| [third-party-notices.md](third-party-notices.md) | Pico SDK and picotool licenses, distribution of binaries |

## Scope

The RP2350 build contains the complete KNX stack (BCU1, BCU2, MASK0701, MASK0705, SYSTEMB), the
bus timer emulation, the persistent storage and the digital pin functions. Not ported yet are
the ADC, I2C, SPI and serial drivers and the I2C sensor drivers that depend on them; they are
listed in `SBLIB_RP2350_EXCLUDED_SRC` in `sblib/sblib.cmake`.

## Selecting the platform

| CMake variable | Values | Meaning |
|---|---|---|
| `SBLIB_MCU` | `LPC1115` (default), `RP2350` | Target microcontroller. `RP2350` builds the library target `sblib_rp2350` against the Pico SDK; the LPC1115 targets are not created. |
| `SBLIB_BOARD` | `pico2_testboard` (default), `custom` | Pin mapping. `custom` needs `SBLIB_BOARD_HEADER`, the file name of an own board header found via the include path. |
| `PICO_PLATFORM` | `rp2350-arm-s` (default) | Cortex-M33, secure. |
| `PICO_BOARD` | `pico2` | Pico SDK board (flash size, clocks). Expected for `pico2_testboard`. |
| `SBLIB_RP2350_PIO_BUS` | `ON` (default), `OFF` | KNX bus by PIO, see below. |
| `SBLIB_RP2350_TIMER_STATS` | `OFF` (default) | Diagnostic counters of the timer emulation and of `Bus::pause()`. |
| `SBLIB_RP2350_STORAGE_SIZE` | `16384` (default) | Size of the persistent storage at the end of the flash in bytes, multiple of 4096. |

In C++ code the platform is detected by `sblib/platform_detect.h`, which defines exactly one of
`SBLIB_PLATFORM_LPC11XX` and `SBLIB_PLATFORM_RP2350`. The board defines
`SBLIB_BOARD_RP2350_PICO2_TESTBOARD` (or the header named by `SBLIB_BOARD_HEADER`).

## Build

Requirements: CMake 3.28 or newer, Ninja, an arm-none-eabi GCC (the same toolchain can build
LPC1115 and RP2350), the external Pico SDK **2.2.0** and picotool 2.1.1 or newer.

| Environment variable | Used for |
|---|---|
| `PICO_SDK_PATH` | Pico SDK 2.2.0 checkout (CMake variable or environment; the SDK import file can fetch release 2.2.0 if none is given) |
| `ARM_TOOLCHAIN_PATH` or `MCUXPRESSO_TOOLCHAIN_PATH` | arm-none-eabi toolchain directory, becomes `PICO_TOOLCHAIN_PATH` |
| `PICOTOOL_DIR` | Directory with `picotoolConfig.cmake`; without it the SDK tries to build picotool from source |

An application includes `cmake/rp2350/pico_sdk_setup.cmake` **before** its `project()` call, calls
`pico_sdk_init()` after it, adds the `sblib` directory and links `sblib_rp2350` together with
`pico_stdlib`. The presets named `rp2350-pico2` (in `sblib/`, in the examples and in the
applications) set the variables of the table above. Example:

```
cmake --preset rp2350-pico2
cmake --build --preset release-rp2350-pico2
```

Minimal examples: `examples/rp2350-smoke` (board check, timer, flash), `examples/rp2350-knx-bcu1`
(complete BCU1 device), `examples/rp2350-bcu-types` (link check of BCU2, MASK0701, MASK0705 and
SYSTEMB).

## KNX bus: PIO or GPIO

The bus code (`sblib/src/eib/bus.cpp`) is shared with the LPC11xx and uses the LPC11xx timer
interface. On the RP2350 `sblib/src/rp2350/timer.cpp` emulates that interface from the 1 MHz
system timer.

- **PIO bus (`SBLIB_RP2350_PIO_BUS=ON`, default):** a PIO state machine detects the falling edges
  of the RX pin and DMA copies the system time of each edge, so the capture value does not depend
  on the interrupt latency. A PIO state machine also generates the TX pulses.
- **GPIO bus (`OFF`):** RX edges come from a GPIO interrupt, TX pulses from a hardware alarm
  interrupt. The capture value then includes the interrupt latency.

Time critical interrupt code of the timer emulation runs from RAM. Measured timing is in
[tp1-timing.md](tp1-timing.md).

## Storage

`sblib/linker/rp2350_storage.ld` reserves the last `SBLIB_RP2350_STORAGE_SIZE` bytes of the flash.
The link fails if the firmware reaches into that area. The area holds `UserEeprom` (last sector) and
the `MemMapper` region. `sblib/src/rp2350/iap.cpp` implements the LPC11xx IAP interface on the
Pico SDK flash functions and erases or programs only inside this area. The flash geometry is
4 KiB sectors and 256 byte pages. Because the managed region lies at the end of a flash with more
than 256 pages, a `MemMapper` allocation table entry is wider than one byte there (see
`mem_mapper.h`).

## Tests and CI

- `test/rp2350-host`: unit tests of the RP2350 sources on the development host against a small
  simulation of the Pico SDK (IAP, pins, timer emulation, the unchanged `bus.cpp` on a simulated
  bus line). Run with `cmake --workflow --preset host-test`. The test programs are built with and
  without `SBLIB_RP2350_PIO_BUS`. Details: `test/rp2350-host/README.md`.
- `test/lib-test-cases`: the library test cases (x64 host build).
- `.github/workflows/rp235x-ci.yml`: builds and runs the host tests, builds the RP2350 examples
  and the RP2350 variants of the out8-bcu1 application (Arm GNU Toolchain 13.2.rel1, Pico SDK
  2.2.0, picotool 2.2.0).
