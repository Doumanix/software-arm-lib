# Third-party components of the RP2350 port

The RP2350/RP2354 support of the sblib uses the following third-party software. It is only used
when building with `SBLIB_MCU=RP2350`; LPC1115 builds do not contain or need it.

| Component | Location | License | Used for |
|---|---|---|---|
| Raspberry Pi Pico SDK 2.2.0 | external `PICO_SDK_PATH`, release `2.2.0` of [raspberrypi/pico-sdk](https://github.com/raspberrypi/pico-sdk) | BSD-3-Clause, see `<PICO_SDK_PATH>/LICENSE.TXT` | hardware access, startup, linker scripts; linked into every RP2350 firmware |
| picotool 2.2.0 | not in this repository (installed, or fetched by the Pico SDK at build time from [raspberrypi/picotool](https://github.com/raspberrypi/picotool)) | BSD-3-Clause | host tool of the build (ELF to UF2, binary info); not part of the firmware |

The Pico SDK bundles further components under their own licenses (e.g. TinyUSB, CMSIS, lwIP,
mbedtls in `<PICO_SDK_PATH>/lib`). The sblib does not use them.

## Distributing RP2350 firmware

An RP2350 firmware image built with the sblib contains code of the Pico SDK. The BSD-3-Clause
license requires that a binary distribution reproduces the copyright notice, the list of
conditions and the disclaimer of `<PICO_SDK_PATH>/LICENSE.TXT` in the documentation or other
materials provided with the distribution. Copyright notice of the Pico SDK:

> Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.

The sblib itself is licensed under the GNU General Public License version 3 (see `LICENSE`).

`cmake/rp2350/pico_sdk_import.cmake` is derived from SDK 2.2.0 and retains its BSD-3-Clause notice. Local changes pin the fetch to 2.2.0, disable SDK submodules, check the exact SDK version and support headers-only host tests.
