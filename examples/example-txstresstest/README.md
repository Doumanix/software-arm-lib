KNX-Tx driver stress test Example
=================================

This is a simple application which stress tests the KNX-Tx driver part of an Selfbus controller\
The mask version of this example is 0x0701.
RP2350 (Pico 2 test board)
--------------------------
CMake preset `rp2350-pico2` (`CMakeLists.txt`). For a real bus the build uses the group telegram
mode of `src/app_main.cpp`: own address 1.1.101, A_GroupValue_Write to 0/0/100 ... 0/0/108 in turn,
a pause of `TXSTRESS_DELAY_MS` (default 60 ms, at least 50) after each telegram, duration
`TXSTRESS_DURATION_MS` (default 30 s, at most 5 min) after a start delay of 5 s. Report lines
`STRESS t=<ms> sent=<count> done=<0|1>` every 5 s on UART1 (Tx GPIO8, 115200 baud).
The build uses a flash size of 2 MiB, so the storage of the example does not overwrite the storage
of an application at the end of the 4 MiB flash.
