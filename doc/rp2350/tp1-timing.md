# TP1 timing measurements on the RP2350 test board

Evidence through 2026-11-01; out8-bcu1 on Pico 2, physical address 1.1.101.
These are measurements of particular images and captured intervals, not a KNX certification result.
Image references below are prefixes of the full **ELF SHA-256** values in the identity table.

## Setup and criteria

Kingst LA1010 with sigrok-cli (driver name kingst-la2016), 10 MHz sampling (0.1 µs sample spacing).
CH0 observes TX (GPIO27, pin 32, active HIGH); CH1 observes RX/bus echo (GPIO28, pin 34, active LOW).
Optional CH2/CH3 observe the bistable output pulses. The passive traces are decoded with the project's
knx_timing.py; sync cases are checked against TX and RX edges. Flash pauses come from UART diagnostic counters,
not from an inferred gap in bus traffic.

The firmware uses 9600 bit/s with an integer 104 µs timing raster
([bus_const.h](../../sblib/inc/sblib/eib/bus_const.h)).
The analyzer's local regression criteria are: 35 ± 2 µs normal zero-bit pulse width,
absolute phase error ≤ 2 µs against that raster, 1353 ± 3 µs between character starts,
and 1563 ± 5 µs from the received final character's stop-bit end to our ACK start.
The stop-bit end is defined as character start + 11 × 104 µs.
These analyzer tolerances are project criteria, not a complete statement of TP1 compliance limits.
Raster error is a phase measurement; a separately calibrated bit-rate measurement was not recorded.

## Clean captures and historical correction

Values are **minimum / mean / maximum**, in µs. The two older columns contain 34 own ACKs each; the 2026-11-01 column combines three captures (see below).
The light load is DeviceInfo plus group 0/0/100 on/off during a 20 s recording.

| Quantity | Local target | 2026-10-23, GPIO, a60f243c | 2026-10-26, PIO, e70895f7 | 2026-11-01, PIO, dcfd6eb9 |
|---|---|---|---|---|
| Normal TX zero-bit pulse | 35 ± 2 | 34.3 / 35.03 / 37.0 (2344 pulses) | 35.0 / 35.01 / 35.1 (2404 pulses) | 35.0 / 35.01 / 35.1 (17,668 pulses) |
| Absolute phase error on 104 µs raster | ≤ 2 | 0.0 / 0.12 / 0.9 | 0.0 / 0.11 / 0.2 | 0.0 / 0.11 / 0.2 |
| Character start spacing | 1353 ± 3 | 1352.4 / 1353.28 / 1353.3 | 1353.2 / 1353.29 / 1353.3 | 1353.2 / 1353.29 / 1353.3 |
| Own ACK after received stop bit | 1563 ± 5 | 1561.1 / 1561.97 / 1563.0 | 1561.1 / 1561.52 / 1562.1 | 1561.1 / 1561.6 (median) / 1563.1 (198 ACKs) |

The 2026-11-01 column combines three 20 s captures (coverage 0.996 each, no decode errors, no arbitration loss)
with DeviceInfo and group 0/0/100 on/off. The image is the current release; PIO was verified on the ELF symbols.
The idle time before our own frames was at least 5212 µs after the previous stop bit (234 frames; 50 bit times =
5200 µs). One gateway NACK caused one repetition 5329 µs later, which was acknowledged.

The earlier GPIO image 8bb21010 (2026-10-23, same light-load setup) had pulse width
33.2 / 36.68 / 41.2 µs, maximum raster error 7.9 µs, character spacing
1350.6 / 1353.23 / 1355.3 µs and ACK spacing 1563.7 / 1565.54 / 1572.7 µs.
It failed the local timing check. The subsequent a60f243c capture passed; the mean pulse changed
from 36.68 to 35.03 µs. Historical failures are retained here to distinguish the corrected behavior.

## Load, synchronization, arbitration and flash writing

| Measurement, date and image | Target or expected behavior | Observed result and coverage |
|---|---|---|
| TX pulse under load, 2026-10-25, GPIO dd97fef6 | Normal pulse 35 ± 2 µs | Six 30 s captures during 10 min load, 587 KNX calls/0 errors: 54,858 pulses, 34.9 / 35.01 / 35.1 µs; no pulse > 36 µs. Maximum raster error 0.3 µs in decodable frames. |
| TX edge diagnostics, 2026-10-25, GPIO 83a8e142 | No late/fallback TX edge | After 10 min diagnostic load: late=0, fallback=0. This is a separate instrumented image, not the release ELF. |
| TX pulse ceiling, 2026-10-25, PIO e617a141 | No elongated normal pulse | 60 min, 10,684 calls/0 errors; twelve 30 s LA samples, approximately 436,000 pulses, none > 36.0 µs. No min/mean is asserted for this aggregate. |
| Arbitration and retries, 2026-10-25, PIO e617a141 | Lose arbitration, receive winner, retry successfully | 57 observed arbitration losses against the gateway; each following retried frame was ACKed. These are bus-contention observations, not firmware error counts. |
| TX under load, 2026-10-26, PIO e70895f7 | No elongated normal pulse | 10 min, 1731 calls/0 errors; two LA samples, 64,665 pulses, max 35.1 µs, none > 36.0 µs. Mixed raw minima 24.9/9.6 µs include shortened/aborted pulses; they are not a clean normal-pulse distribution. |
| Sync pulse end after foreign falling edge, 2026-10-26, PIO e70895f7 | About 35 µs after the foreign edge; our pulse may be shorter | Three cases: foreign lead 6.4/8.5/9.3 µs; our width 29.0/26.1/26.0 µs; end relative to foreign edge 35.4/34.6/35.3 µs (min/max 34.6/35.4). Not measured as a GPIO counterpart. |
| TX under replay/long load, 2026-10-26, GPIO 85fe73ae | Normal pulse 35 ± 2 µs | 20/20 replay passes, 23,278 pulses in a 75 s replay capture, max 35.1 µs. Separate 60 min load: 9983 calls/0 errors, twelve scheduled LA samples; max 35.1 µs, none > 36 µs in captured data. |
| Bus pause spanning sector erase, 2026-10-26, PIO diagnostic 649bf01e | No TP1 nominal duration; measure interruption, not a compliance margin | 8 erases: 37.878 / 38.504 / 39.394 ms; program-only pauses, 118 cases: 0.738 / 0.745 / 0.754 ms. Seven UART counter intervals, not the planned full endurance series. |

The later release images 31dc9046 and 85fe73ae use **GPIO**, despite the source option's PIO default:
a reused local CMake cache retained the OFF setting. The e70895f7 image and 649bf01e diagnostic use **PIO**.
This classification was checked on the actual ELF symbols: pioRxIrq identifies PIO, rxEdgeHandler identifies GPIO.
Source defaults, branch names and CI builds do not establish the bus variant of an already measured image.

## Limits and remaining measurements

LA buffer gaps occur around 0.42/0.84/1.25 s after recording start. Cut frames, simultaneous spurious pulses
on all channels and truncated capture ends must be investigated as recording artifacts before attributing them
to firmware. A GPIO before/after comparison reproduced gap-related errors with both images.
Real arbitration losses are a separate phenomenon and also truncate transmitted frames.

Consequently, an aggregate timing FAIL is not evidence of elongated normal pulses, and selected passing
quantities are not evidence that every frame passed. The PIO load pair had coverage 0.804/0.993 and verdicts
INCOMPLETE/FAIL. The later GPIO hour had nine approximately complete captures (coverage 0.993) and three
partial captures (about 0.04/0.04/0.76). The flash diagnostic's second LA capture had coverage 0.999 and a
cut frame at its beginning. ACK pairing across gaps/aborted frames can produce meaningless aggregate extrema;
only the clean-capture ACK statistics above are used as timing evidence.

Still required: a freshly configured PIO release with ELF-symbol verification followed by the same device,
replay and long-load checks; matched GPIO/PIO sync and collision captures; a controlled LPC reference using
the same traffic and probe setup; full coverage of the interrupted flash/replay series; deliberately timed
traffic during a flash pause and unobserved slow-erase tails. These records do not establish worst-case flash
latency or general loss-free reception while writing flash.

## Image identity

Raw captures and offline source mappings are retained in the local measurement archive, outside the firmware
repository. The date and full ELF hash identify the image; the companion delivery report maps each row to its
capture, raw file and source line. No new hardware measurement was made for this document.

| ELF prefix | Bus | Full ELF SHA-256 |
|---|---|---|
| 8bb21010 | GPIO | 8bb2101004221a5c4821bde114e5d2da22118c766c6ecf2553a41b7c2d25b917 |
| a60f243c | GPIO | a60f243ce25195b06f5d3966bf96ba89c4f784bcd12696e43373b5661d202709 |
| dd97fef6 | GPIO | dd97fef635480e24e5a8857851eece6c7e1af0e315ade2a115ec39f6fc8efe8b |
| 83a8e142 | GPIO diagnostics | 83a8e142f106f1c0dbdde5ddfa54867511188c4def8ee9c35fe4abebc6a5e9e2 |
| e617a141 | PIO | e617a141b4a69aff265087c51a18009e9f93d38c1d936ea88c1f8a5250a38b3d |
| e70895f7 | PIO | e70895f72a6ead5f9abc4ca88a42654a794b1498a50e1e84c7317da1a92b451c |
| 31dc9046 | GPIO | 31dc904614dec2a8c63f52a6ab3789c48bc01a57132915e144b11773881f6e6e |
| 85fe73ae | GPIO | 85fe73ae38c528f19a36988474ca421d667a7a3a4f4e2e94acea6f13e58faf59 |
| 649bf01e | PIO diagnostics | 649bf01e55f2b9158095fe3753940f162856ce9e04fc4331615994bc3ca35fa0 |
| dcfd6eb9 | PIO | dcfd6eb941bdca3b23cabbf6f27350346c84a11d4fd78dc3953e4edc6b6245ef |
