# What else can be tested

Every capability bit in `core/frank_caps.h` is a claim about hardware. Twenty-one
of the thirty-one are tested or interactively covered today. This goes through the rest: what a test could
honestly prove, what it could not, and roughly what order to do them in.

A few items are marked as not worth building. That is the same principle the
firmware already runs on. A rig claiming coverage it does not have is worse than
one that owns up.

---

## Where things stand

| Capability | State |
|---|---|
| `PSRAM_QMI`, `PSRAM_SOFTSPI` | Tested. Probe and address-seeded sweep |
| `SD` | Tested. CID/CSD and sector 0 |
| `RTC_DS3231` | Tested. Oscillator-stop flag and a live seconds tick |
| `ONEWIRE_DS2401` | Tested. ROM read |
| `LINK` | Tested. Handshake, throughput, slave reset |
| `VIDEO_HDMI/VGA/COMPOSITE` | Tested. Frames counted per backend |
| `AUDIO_I2S`, `AUDIO_MUX`, `TURBOSOUND` | Interactive. Driven, not verified |
| `GAMEPAD_NES` | Interactive |
| `TAPE_IN`, `TAPE_DIP_GATED` | Interactive |
| `PS2` | Interactive. Raw byte counts, scancodes, mouse deltas |
| `USB_HOST`, `USB_HUB` | Tested. Enumerated devices reported; the hub proves itself |
| `USB_DEVICE`, `USB_MUX`, `PIO_USB` | Untested |
| `GAMEPAD_DB9` | Untested |
| `ESP01` | Tested. AT, then AT+VER or AT+GMR |
| `ESP32_SPI` | Untested |
| `LED_PLAIN`, `LED_WS2812` | Interactive. Driven; the operator judges the light |
| `DIPSWITCH` | Tested, as far as the hardware allows - see below |
| `I2C` | Tested. Full 0x08-0x77 scan, and both lines checked idle-high |
| `AUDIO_AMP`, `AUDIO_CODEC_I2C` | Untested |
| `SD_4BIT` | Untested |

---

## Tier 1: clear wins, low risk

These test real hardware, fail for one identifiable reason, and ask nothing of
the operator's judgement.

### 1. PS/2 keyboard and mouse (`CAP_PS2`) — done

The descriptors already carry `ps2_kb_clk/dat` and `ps2_ms_clk/dat`. Most of the
fleet has the connectors. Nothing touches them.

A PS/2 device drives the clock line itself, which is the useful part: presence is
measurable without the device doing anything. Idle both lines high, then watch
for a clock burst when something is plugged in or a key goes down. The dialog
would look like the gamepad one, drawing a keyboard and a mouse and showing
scancodes and movement deltas as they arrive.

It proves the connector, the level shifting and the PIO receiver. It proves
nothing at all with no device attached, so an empty port has to report *could not
run* rather than failing.

Those PS/2 connectors are a common cold-solder site, which is why this was the
most valuable thing missing.

Built as `Tests` ▸ `PS/2 Ports`. The byte count is deliberately the raw one,
taken before decoding: a port carrying garbage is wired but wrong — usually clock
and data crossed — and looks identical to a dead one if you only watch decoded
keys. The last raw code is shown beside it, because a port that reports 0xAA and
nothing else has a keyboard that finished its self-test and a host that is not
hearing keystrokes.

The mouse panel names *why* it is off when it is: no pins on this board, or the
console holding GP0/GP1. Those send an operator to completely different
places.

### 2. Config DIP switches (`CAP_DIPSWITCH`) — done, with a correction

This was written expecting the switch positions to be readable. They are not,
and the descriptor said so all along: `dip` and `tape_in` are the same pin, GP22,
on all four boards that have it. The switch does not present a position. It
connects the tape line to the pin or leaves it floating, and there is no bit
anywhere for firmware to read.

The consequence is readable, and it is the useful half. Closed, GP22 sits on the
tape network — 10K to ground and a microfarad — which divides the chip's own
~60K pull-up to a firm low. Open, the pin floats and follows the pull. One
pull-up and one read separate them.

So *Tape switch* now reports whether S1 3-4 is closed, which is what "close the
switch and try again" was really asking about. An open switch reports *could not
run*: it is a setting, not a fault.

Nothing here reads the other switch banks. The audio mux, the USB mux and the
PSRAM SO link are not wired to GPIOs at all, and *Manual Steps* remains the only
honest thing to say about them.

### 3. LEDs (`CAP_LED_PLAIN`, `CAP_LED_WS2812`) — done

A plain LED is one GPIO. A WS2812 is one PIO state machine and a colour cycle.

That proves the driver pin, and for the WS2812 the timing-critical bit protocol.
It cannot prove the LED is fitted or lit, so this belongs in a dialog that asks
whether you see red, then green, then blue, rather than in a pass/fail row.

Mostly worth doing because a WS2812 that stays dark is usually a data-line fault,
and that is worth catching.

Built as `Tests` ▸ `LEDs`, as one sequence whatever the board carries: the plain
LED blinks three times, then the WS2812 fades up and down through red, green and
blue. A board with only one of the two runs only that part.

Fading rather than switching, because a fade is much harder to fake. A marginal
data line usually still manages full brightness and falls apart in the middle of
the range, where a bit error turns a dim red into a bright green.

The WS2812 is bit-banged rather than given a PIO state machine, because there is not one going spare — pio0 has the link, pio1 the I2S
and PS/2, pio2 the gamepad reader and the composite encoder — and taking one here
would mean taking it from something that is also a test. Interrupts are masked
for the 30 microseconds a frame takes, which is safe only because the video
scanout runs on core 1 and its interrupt is core 1's.

### 4. I²C bus scan (`CAP_I2C`) — done

Full 0x08-0x77 scan, listing the first few addresses found. The reserved ranges
either end are skipped: addressing them means something other than "is anyone
home".

Both lines are also checked idling high before anything is driven, because a line
stuck low is a fault the scan itself cannot report — every address would simply
fail to ack, which reads as an empty bus. An empty bus with healthy pull-ups
reports *could not run* rather than failing, since every part on it is optional.

### 5. RTC oscillator, not just presence — done

The seconds register is read, then read again just over a second later. A crystal
that is present but not oscillating leaves it frozen, and that is the only direct
evidence the part is keeping time rather than merely being on the bus.

The oscillator-stop flag is reported too, and deliberately not cleared: clearing
it is how you acknowledge the stored time is not to be trusted, which is the
operator's call. A part that is ticking now but has the flag set passes with the
warning in its detail, because the fault it records is in the past.

---

## Tier 2: worthwhile, more work

### 6. USB HID host reporting (`CAP_USB_HOST`) — done

The input layer already enumerates keyboards and mice, since the interface
depends on it, but nothing reports what it found. A dialog showing VID/PID,
product string, endpoint and live key, button and wheel events would make an
invisible subsystem visible.

That proves the connector, the host stack and the device. It is nearly free: the
data is already flowing, it just is not displayed anywhere.

### 7. USB hub and mux (`CAP_USB_HUB`, `CAP_USB_MUX`) — done, in part

It turned out to need nothing of its own. Every device on Core 2U is behind the
hub, so there is no such thing as one that enumerated without passing through it,
and anything in the inventory is proof the hub powered up and passed a device
along. Port occupancy is not reported: it would need the hub interrogated
directly, and the count already answers the question that matters.

The mux is switch-selected and stays untestable, like the audio one.

### 8. PIO-USB second port (`CAP_PIO_USB`) — not built

Unchanged in substance and worth restating with what is now known. One board
declares it: zeroFRANK, on GP20. It needs the Pico-PIO-USB library, which is not
vendored here, *and* a free PIO — all three are committed, and the gamepad reader
still does not release pio2 when its dialog closes.

That makes it the one Tier 2 item that is a dependency and a refactor rather than
a test. The refactor is worth doing on its own merits: whoever adds the next PIO
consumer will hit the same wall.

### 9. DB9 gamepads (`CAP_GAMEPAD_DB9`)

OldSkoolFRANK wires Atari-style straight to GPIOs. No shift register, so the NES
reader does not apply. Plain pin reads, same dialog treatment.

### 10. ESP-01S and ESP32 (`CAP_ESP01`, `CAP_ESP32_SPI`)

The descriptors carry a UART pair, chip-enable, GPIO0, a full SPI set, handshake
and ready lines, and a mux select. That is two separate tests.

For the ESP-01S: toggle CH_PU, send `AT`, expect `OK`. Proves the module is
powered, reset-controllable and talking. For the ESP32: assert CS and exercise
the handshake and ready pair.

Both need the module fitted, so absence has to report *could not run*.

The ESP-01S half is done. The ESP32 half is not: it exists only on FRANK Next,
which has real reset lines where the ESP-01S boards have buttons, so it is a
genuinely different test rather than the same one with different pins. Written
blind it would be untestable here, and this firmware has already spent an evening
on hardware code that could not be checked against the board it was for.

### 11. SD write, and a results report — write done, report not

*SD write* takes the last sector, writes a counting pattern, reads it back,
compares, restores the original and reads once more to confirm the restore took.
Everything else on the card only reads, and the direction that is never exercised
is the one that turns out to be broken.

No consent dialog in the end. It is not destructive if the original goes back and
the restore is verified, and a prompt nobody can answer usefully — "may I write
to a sector you cannot see?" — is a worse design than doing the safe thing and
saying what was done. A failed restore is reported louder than the original
fault, because the card then holds something nobody chose.

The pattern counts rather than repeating: a stuck bus reads back the same byte
everywhere and would match a constant fill, so a counter catches a wrong address
line as well as a wrong data line. The last sector rather than sector zero,
because a boot sector is the one place a failed restore is unrecoverable.

The report half is not built. It needs a filesystem, and the argument in
tests_sd.c against pulling FatFs in still holds — whether a card carries a
filesystem this firmware recognises says nothing about whether the board works.
Writing a report would make it a dependency of the rig rather than of one test,
which is a decision worth taking deliberately rather than as a side effect.

### 12. SD 4-bit mode (`CAP_SD_4BIT`) — not built, and the entry was wrong

Checked before writing anything. One board wires the lines — FRANK Next, on GP5
and GP6, with its own SD pin set — and **no board declares `CAP_SD_4BIT` at
all**, so a test gated on it would have been permanently n/a and looked like
coverage.

The lines cannot be proven without driving them, and driving them means SDIO
rather than SPI. That is a PIO program, and all three PIOs are committed. Nothing
cheaper works: DAT1 and DAT2 are unused in SPI mode, so reading them through a
pull-up cannot tell a card holding the line from a floating trace doing the same.

Needs the capability declared on FRANK Next and an SDIO implementation with a PIO
to put it in. Both are real work; neither is hard.

---

## Tier 3: infrastructure rather than tests

### 13. Burn-in loop — done

Run the whole suite over and over, counting failures per test. That catches the
class of fault a single pass never will: PSRAM that fails once warm, a link that
degrades, an intermittent joint.

Built as `Tests` ▸ `Burn-in`. It counts failures per row and shows only the rows
that have gone wrong, so a clean burn-in is a blank panel and anything on it is
something to look at. Tests reporting *could not run* are counted separately from
failures — an empty SD socket is not an intermittent fault, and lumping the two
together would bury a real one under forty of them.

Stopping happens between tests, never during one: a test owns its pins and its
timing while it runs. There is no cycle limit, because the operator is the only
party who knows how much evidence the board needs.

### 14. Long-run link error rate — done

The link test measures a burst, which answers whether the link works and says
nothing about whether it keeps working. *Link soak* repeats the sweep for thirty
seconds and counts errors against passes.

Thirty seconds is a compromise and the code says so: long enough to catch a fault
that appears every few seconds, far too short for one that appears every few
minutes. For that, run the burn-in, which has no limit and includes this row in
every cycle. A single error fails the row — not because one error is a
catastrophe, but because a soak that tolerated errors would report exactly what
the burst already reported.

### 15. Slave-side self-test

On a Core 2 the slave is a whole RP2350 with its own flash and RAM, and all we
prove today is that it answers. It could run its own memory tests and report over
the link, roughly doubling the coverage of a dual-chip board.

### 16. Clock cross-check — done

Two oscillators that know nothing about each other, each checking the other. The
system crystal has no reference to be wrong against — every timer and baud rate
is derived from it — while the DS3231 is temperature compensated and specified
two orders better than the part it is checking.

*Clock accuracy* counts four RTC seconds and compares them with the system timer.
It reports the disagreement in ppm and names both crystals rather than picking
one: the RTC is the better reference in practice, but a rig that guessed would be
wrong about half the time. The tolerance is loose on purpose — 500 ppm still
keeps a UART happy, while a part on the wrong overtone is out by percent.

---

## Not worth building

**Audio verification.** No loopback and no ADC anywhere in the fleet. Putting a
PASS on the audio dialogs would be inventing a result. If some future board
routes audio back to an ADC pin, this changes.

**Display presence.** No board wires hot-plug detect. *Video output* counting
emitted frames is as far as honesty goes.

**AY readback.** Verified against MegaFRANK's netlist rather than assumed, and
it is worse than "write-only" suggests. `DA0-DA7` connects three things: both
AYs and the 595's parallel outputs, with no GPIO anywhere on it. The chain runs
GP11 to `SER`, GP10 to `SRCLK`, GP9 to `RCLK`, then U8 `QH'` into U9 `SER` - and
**U9's `QH'` goes nowhere**. Both `OE` pins are tied to ground, so the registers
cannot be tri-stated to let an AY drive the bus even if something could read it.
The clock is a crystal through a 74HC74 with no GPIO on it, and the AY I/O ports
are unconnected apart from the second one's IOB, which drives a resistor DAC into
the analogue mix.

The audio route was checked too, since the mixer output reaches GP22 through the
tape switch: `AMP_IN_L` goes through 10K into 10K to ground, halving an AY output
of about a volt peak-to-peak to well under the input threshold. It never crosses.

So the row reports what the operator heard, through core/attest.h, and says on
its face that a person gave the verdict. "Not checked" is the state until someone
listens, and is not a failure.

One track would change most of this. Routing U9's `QH'` to a spare GPIO would let
firmware shift a pattern through both registers and read it back, proving all
three control lines, both 595s and the traces as far as the AY pins - leaving
only the AYs themselves as a matter of listening.

**Flash write and erase.** The firmware is running from it. A scratch-sector test
is possible, but the risk against the value is poor, and the settings sector
exercises that path incidentally anyway.

---

## Suggested order

1. USB HID host reporting. The data is already there
2. Report export to SD. Makes the rig traceable
6. Report export to SD. Makes the rig traceable
7. LEDs and DB9 gamepads. Quick once the dialog pattern gets reused
8. ESP-01S and ESP32. Needs modules to test against
9. Burn-in loop and link soak. Once the rest is stable
10. PIO-USB. After sorting out who owns which PIO

Tier 1 is done. Tested or interactively covered capabilities are now twenty-one
of thirty-one, and what remains is Tier 2 and the infrastructure below it.
