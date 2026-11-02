# RP2350 port: host unit tests

Unit tests of the RP2350 sources of the sblib that run on the development host (Windows/MinGW
or Linux GCC), without a Pico. The RP2350 code is compiled unchanged against a small simulation
of the Pico SDK functions it uses (`sim/pico_sim.*`: microsecond time, the alarm registers of
TIMER0, interrupt handlers in the vector table, NVIC priorities (a higher priority interrupts a
running handler) and pending state, GPIO edge interrupts, global interrupt lock, alarm interrupt
latency, busy waiting also in polling loops of the main context and in interrupt handlers, and
a KNX TP1 bus line with own echo and other senders).
Models of other bus devices react to our TX pin (`sim::onPinChange`) with hardware events
(`sim::atHardware`) that also run while the code under test busy waits.

```
cmake --workflow --preset host-test
```

Every test program is built twice: `<name>_pio` with `SBLIB_RP2350_PIO_BUS` (KNX bus by PIO, the
default of the sblib) and `<name>` without it (`-DSBLIB_RP2350_PIO_BUS=OFF`: GPIO
interrupt and alarm).

## Tests

`src/test_iap.cpp` (`rp2350_iap_tests`): the unchanged RP IAP source with a 16 KiB XIP array,
whose final 8 KiB are storage. `IAP_EMULATION` redirects the platform flash base to that array;
linker symbols retain the production storage-boundary checks. The flash SDK model uses 4 KiB
sectors, 256 byte pages, erased bytes 0xFF and bitwise AND programming (only 1 to 0).
It rejects unaligned/out-of-range SDK operations and flash operations with IRQs enabled.
Tests cover storage boundaries, alignment/count/source rejection, multi-sector page RMW,
injected erase/program verify failures, IRQ preservation and UserEeprom's erase/1024-byte
program sequence. Identity stubs are deterministic. The model omits timing, power failure,
multicore access and real flash/XIP controller behaviour.

`src/test_pin.cpp` (`rp2350_pin_tests`): the unchanged RP digital pin source and public inline
IO functions. GPIO configuration records SDK function, direction, pulls, input buffer and
call counts. Tests cover peripheral functions, analog/bus keeper/unsupported modes, invalid
pins, both 32-bit port masks and interrupt configuration/registration/dispatch. IRQ mode
mapping is checked as SDK event masks; the simulation's existing edge model is not extended
to a full pad/electrical or level-interrupt model. The program is also built with `SBLIB_RP2350_PIO_BUS` (`rp2350_pin_tests_pio`), because
the function of the bus TX pin depends on the bus variant.


`src/test_timer.cpp` (`rp2350_host_tests`): emulation of the LPC11xx timers
(`sblib/src/rp2350/timer.cpp`) as the KNX bus (`src/eib/bus.cpp`) uses it: counter, match with
INTERRUPT/RESET/STOP, bus PWM (MAT0 pulse start, MAT2 pulse end) also with interrupt latency and
with long interrupt handlers (TX edge interrupt above the alarm and capture interrupts),
capture CAP0 (every edge, no spike filter), interrupt enable and pending flags, begin()/end(),
priorities. Built with `SBLIB_RP2350_TIMER_STATS`, so the diagnostics counters (edges closer
than 8 us to the previous one, delay and width of the TX edges, duration of the RX edge
interrupt) are tested too; `rp2350_bus_tests` is built without it.
The expected values follow the LPC11xx (UM10398, CT16B), deliberate deviations of the emulation
are marked "deviation" in the test name.

`src/test_bus.cpp` (`rp2350_bus_tests`): Darth's unchanged `src/eib/bus.cpp` on top of the timer
emulation and the simulated bus line. Sending (bytes, parity, checksum, 104 us bit raster,
35 us pulses, 1353 us per character as bus.cpp times it), acknowledged sending with own echo
after 0/2/5 us, 3 repetitions without acknowledge, receiving with acknowledge 15 bit times after
the stop bit, address filter (own address, group address table, others), wrong checksum,
alarm interrupt latency, a slow timer interrupt handler (bus.cpp from flash, up to 40 us per call),
a reflection after the pulse end during a long echo interrupt. The hidden test case `[.explore]`
prints the observed timing.

`src/test_bus_t9.cpp` (`rp2350_bus_tests`): a second device on the bus (`Peer`)
decodes our frames, acknowledges them with ACK, NACK, BUSY or nothing, starts its own frame
together with ours, or sends a series of telegrams with repetitions like a download. Cases:

- Collision and arbitration: the peer wins in the control field, in the source address or
  in the last data byte; we stop at the collided bit, receive and acknowledge the peer's
  telegram and repeat ours with the repeat flag unchanged after 50 bit times; a peer more than
  30 us earlier is no collision (we defer); 4 lost arbitrations give the telegram up
  (COLLISION_RETRY_MAX); our ACK against the NACK of another receiver. With the KNX bus by PIO
  (`_pio`): a peer 1..20 us before our start bit, our start bit synced to it ends 35 us after the
  peer's edge as on the LPC11xx.
- Reception with bit jitter (data bits -40..+70 us, pulse width 25/50 us, longer pause between
  characters) and spikes (idle bus 1..10 us, inside a 1 bit, a dropout inside a 0 bit, a spike
  just before a 0 bit); afterwards a clean telegram must be received (no stuck state).
- BUSY and NACK acknowledge: repetition after 150 or 50 bit times with the repeat flag cleared,
  3 repetitions each, separate counters for BUSY and NACK.
- Download-like load: 50 MemoryWrite telegrams at the shortest spacing, a slow application
  (no acknowledge while the receive buffer is taken), lost acknowledges (no duplicate).
- GPIO interrupt of the application during a download: an input pin with its own raw GPIO
  handler (0, 5, 20 us) on IO_IRQ_BANK0, an edge every 997 us; all telegrams are received and
  acknowledged once and every edge reaches the handler.
- `pause()`/`resume()`: telegram while paused, `resume()` in the middle of a telegram,
  `pause()` waiting for the acknowledge, `pause(true)`, `pause()` during repetitions, flash
  writes (pauses) of 2..50 ms during a download, and the limit of about 85 ms
  (3 repetitions of the sender).

`[.explore]` prints the observed frames and times of all these cases.

Known deviations that are not fixed yet are tagged `[!shouldfail]`: the test run stays green,
and a test that suddenly passes is reported, so the tag is removed together with the fix.
There is no such test case at the moment.

Fixed so far:

| Topic | Former deviation |
|---|---|
| RESET cycle length | A RESET cycle had MR microseconds instead of MR + 1 (TX pulse 34 instead of 35 us, bit timing 1 us short per cycle; on the bus up to 9 us off the bit raster within a byte) |
| Match without RESET | A match without RESET fired again every microsecond until the match value changed (LPC11xx: once per counter cycle) |
| Spike filter on timer values | The capture spike filter compared timer values, not times: with a match RESET every bit time an edge 104 us after the last one had the same timer value and was dropped; a peer winning the arbitration in the control field was received as 0xB4 instead of 0xB0 and not acknowledged (fixed: filter on system time) |
| Spike filter after a spike | The capture spike filter (8 us) dropped the real edge of a 0 bit after a 2 us spike 3..7 us before it, the telegram was lost; the LPC11xx captures both edges and bus.cpp rejects the spike itself (fixed: filter removed, edges closer than 8 us are only counted with `SBLIB_RP2350_TIMER_STATS`; on the test board none in 1.17 million edges) |
| TX pulse width with long interrupts | The alarm of a TX edge had the priority of the RX edge interrupt and came through the SDK alarm handler in flash: a long RX edge interrupt (own echo, bus.cpp) or a slow interrupt entry made TX pulses wider (on the test board up to 43 us, first pulse of a telegram 37.2 us; fixed: TX edge interrupt with its own alarm above the alarm and capture interrupts, alarm interrupts from the vector table in RAM) |

Accepted behaviour (deliberately tolerated, the test case checks it as it is):

| Test case | Behaviour |
|---|---|
| bus jitter: bit edges 34..68 us late are accepted (tolerated like Darth's bus.cpp) | The KNX window is n*104 -7/+33 us, bus.cpp only sets RX_TIMING_ERROR_SPIKE for an edge later than 33 us and still takes and acknowledges the telegram (edges up to about +68 us belong to their bit). The LPC11xx build behaves the same. Kept tolerant on purpose. The test reads `rx_error` before bus.cpp clears it when taking the telegram. |
