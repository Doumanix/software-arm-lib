Serial Bus Monitor Example
==========================

This is a bus monitor that outputs all received telegrams to the serial port.
The serial port is used with 115200 baud, 8 data bits, no parity, 1 stop bit.
Tx-pin is PIO1.6, Rx-pin is PIO1.7

In addition, one can send telegram on the bus via the serial port by sending
a sequence of bytes. The incoming data from the serial line should be in
following byte-format (with-out spaces):
CC aa bb cc dd ee
CC             the number of telegram-bytes to be send
aa,bb,cc,dd,ee the telegram data (without the checksum)

RP2350 (Pico 2 test board)
--------------------------
CMake preset `rp2350-pico2` (`CMakeLists.txt`). Serial port is UART1: Tx GPIO8, Rx GPIO9,
115200 baud (`src/rp2350/serial_rp2350.cpp`). The build uses a flash size of 2 MiB, so the
storage of the example (UserEeprom) does not overwrite the storage of an application at the
end of the 4 MiB flash.
