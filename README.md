# software-arm-lib
Repository for the ARM library for NXP LPC1115 microcontroller.

## Unit tests of the sblib
For unit testing of the sblib, the following projects must be imported into the workspace and opened:  
- [lib-test-cases](test/lib-test-cases)  
- [sblib-test](test/sblib)  
- [sblib](sblib)  
- [Catch](Catch)

## RP2350 / RP2354 (in development)

Build with `SBLIB_MCU=RP2350` (presets `rp2350-pico2`, see `examples/rp2350-*`). The Pico SDK is an
external dependency, exactly version 2.2.0 (commit `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779`).
Clone [raspberrypi/pico-sdk](https://github.com/raspberrypi/pico-sdk) at tag `2.2.0` without submodules,
then set `PICO_SDK_PATH` to that directory (CMake `-D` takes precedence over the environment).
Alternatively configure with `-DPICO_SDK_FETCH_FROM_GIT=ON`; only tag `2.2.0` is accepted and SDK
submodules are not fetched. In CLion set the environment in the RP CMake profile and use a fresh
build directory after changing SDK location. LPC builds do not need the SDK. RP host tests also
use `PICO_SDK_PATH`, without importing the SDK cross compiler. Host unit tests of the RP2350 port:
[test/rp2350-host](test/rp2350-host). Licenses of the third-party components and what to include
when distributing RP2350 firmware: [doc/rp2350/third-party-notices.md](doc/rp2350/third-party-notices.md).

## Contributing

- Contributions are welcome!
- Please submit a pull request or open an issue for any bugs or feature requests.
- Please try to follow the existing code style and conventions when contributing.
- An MCUxpresso code style formatter can be found in the [software-arm-incubation](https://github.com/selfbus/software-arm-incubation/blob/main/mcuxpresso-code-style-formatter.xml)
  repository. It can be imported into the MCUxpresso IDE via:  
  `Window -> Preferences -> C/C++ -> Code Style -> Formatter -> Import...`.