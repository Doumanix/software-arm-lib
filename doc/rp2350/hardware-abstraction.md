# Hardware abstraction of the sblib: LPC11xx and RP2350

Description of the current state. The sblib was written for the NXP LPC11xx; the RP2350 port
keeps the LPC11xx interfaces and provides a second implementation behind them. This document
does not describe a planned restructuring.

## Platform and board selection

| Item | LPC11xx | RP2350 |
|---|---|---|
| Platform macro (`sblib/platform_detect.h`) | `SBLIB_PLATFORM_LPC11XX`, from compiler definition `__LPC11XX__` (also used for host builds with CPU emulation) | `SBLIB_PLATFORM_RP2350`, from `PICO_RP2350` set by the Pico SDK |
| CMake target | `sblib_arm`, `sblib_x86`, `sblib_x64` | `sblib_rp2350` |
| Source list (`sblib/sblib.cmake`) | `SBLIB_SRC` | `SBLIB_RP2350_SRC`: `SBLIB_SRC` plus the platform files, minus the excluded files |
| Pin names (`sblib/io_pin_names.h`) | `hardware/gpio_lpc11xx.h` (`_gnax` or `_fused_io` selected by macro) | `hardware/gpio_rp2350_pico2_testboard.h`, or the header named by `SBLIB_BOARD_HEADER` |
| Register access (`sblib/platform.h`) | CMSIS `LPC11xx.h` | Pico SDK headers only (`hardware/gpio.h`, `irq.h`, `sync.h`) |

## Shared code

Compiled unchanged for both platforms:

- the KNX stack in `sblib/inc/sblib/eib/` and `sblib/src/eib/` (BCU types, telegram layers, address
  and communication object tables, properties, `bus.cpp`, `UserEeprom`, hardware descriptor),
- `mem_mapper.cpp` and the platform independent helpers of `SBLIB_CORE_SRC` (`debounce`,
  `ring_buffer`, `print`, `stream`, `utils`, ...),
- the public interfaces `digital_pin.h`, `timer.h`, `interrupt.h`, `internal/iap.h`, `platform.h`.

## Interfaces with one implementation per platform

| Interface (shared header) | LPC11xx implementation | RP2350 implementation |
|---|---|---|
| Digital pins (`digital_pin.h`, `ioports.h`) | `src/lpc11xx/digital_pin.cpp`, `digital_pin_port.cpp`, `digital_pin_pulse.cpp` | `src/rp2350/digital_pin.cpp` |
| Timers (`timer.h`, class `Timer`) | `src/timer.cpp` (hardware timers) | `src/rp2350/timer.cpp` (emulation on the 1 MHz system timer; PIO and DMA for the bus) |
| Time (`millis()`, `delay()`, `delayMicroseconds()`) | `src/timer.cpp` (SysTick) | `src/rp2350/time.cpp` (64 bit counter of TIMER0, no interrupt) |
| Flash programming (`internal/iap.h`) | `src/iap.cpp` (IAP ROM calls) | `src/rp2350/iap.cpp` (Pico SDK flash functions, storage boundary checks) |
| Platform services (`platform.h`: `NVIC_SystemReset()`, `SystemCoreClock`, `hwRandom()`) | `src/lpc11xx/platform.cpp` | `src/rp2350/platform.cpp` (watchdog reboot, `get_rand_32()`) |
| Startup and linker script | application (for example `cr_startup_lpc11xx.cpp` and a generated `.ld`) | Pico SDK runtime plus `linker/rp2350_storage.ld` |

On the RP2350 the pin modes of the LPC11xx (`PULL_UP`, `PULL_DOWN`, `REPEATER_MODE`,
`OUTPUT_MATCH`, ...) are mapped to the RP2350 pad configuration. `HYSTERESIS` is ignored and
open drain outputs are not supported (`src/rp2350/digital_pin.cpp`). `OUTPUT_MATCH` is emulated
by the `Timer` class.

## Not available on the RP2350 yet

`SBLIB_RP2350_EXCLUDED_SRC` removes the ADC (`analog_pin.cpp`), I2C (`i2c.cpp` and the sensor
drivers based on it), SPI, serial (`serial.cpp`, `serial0.cpp`, `soft_uart.cpp`) and the
`graphical_eadogs` LCD driver from the RP2350 library. Applications must not use them there;
application code that needs them is guarded by `SBLIB_PLATFORM_RP2350` or
`SBLIB_PLATFORM_LPC11XX`.

## Application side

Applications keep one source tree and select per platform with the platform and board macros,
for example the output pin table and the GPIO access of the out8-bcu1 application. Pin
numbers come from the board header (`PIN_EIB_TX`, `PIN_EIB_RX`, `PIN_PROG`, `PIN_RUN`, ...) or
from the application's own tables.
