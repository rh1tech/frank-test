# What else can be tested

Every capability bit in `core/frank_caps.h` is a claim about hardware. Fourteen
of the thirty-one are tested today. This goes through the rest: what a test could
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
| `PS2` | Untested |
| `USB_HOST`, `USB_DEVICE`, `USB_HUB`, `USB_MUX`, `PIO_USB` | Untested |
| `GAMEPAD_DB9` | Untested |
| `ESP01` | Tested. AT, then AT+VER or AT+GMR |
| `ESP32_SPI` | Untested |
| `LED_PLAIN`, `LED_WS2812` | Untested |
| `DIPSWITCH` | Tested, as far as the hardware allows - see below |
| `I2C` | Tested. Full 0x08-0x77 scan, and both lines checked idle-high |
| `AUDIO_AMP`, `AUDIO_CODEC_I2C` | Untested |
| `SD_4BIT` | Untested |

---

## Tier 1: clear wins, low risk

These test real hardware, fail for one identifiable reason, and ask nothing of
the operator's judgement.

### 1. PS/2 keyboard and mouse (`CAP_PS2`)

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

Those PS/2 connectors are a common cold-solder site, which is why this is
probably the most valuable thing missing.

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

### 3. LEDs (`CAP_LED_PLAIN`, `CAP_LED_WS2812`)

A plain LED is one GPIO. A WS2812 is one PIO state machine and a colour cycle.

That proves the driver pin, and for the WS2812 the timing-critical bit protocol.
It cannot prove the LED is fitted or lit, so this belongs in a dialog that asks
whether you see red, then green, then blue, rather than in a pass/fail row.

Mostly worth doing because a WS2812 that stays dark is usually a data-line fault,
and that is worth catching.

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

### 6. USB HID host reporting (`CAP_USB_HOST`)

The input layer already enumerates keyboards and mice, since the interface
depends on it, but nothing reports what it found. A dialog showing VID/PID,
product string, endpoint and live key, button and wheel events would make an
invisible subsystem visible.

That proves the connector, the host stack and the device. It is nearly free: the
data is already flowing, it just is not displayed anywhere.

### 7. USB hub and mux (`CAP_USB_HUB`, `CAP_USB_MUX`)

Core 2U has a hub. Enumerate through it, report the device count and which ports
are occupied. That proves the hub powers up and enumerates.

The mux is switch-selected, so like the audio mux it is half a path. The firmware
can say which side it sees, and has to say it cannot pick.

### 8. PIO-USB second port (`CAP_PIO_USB`)

`pio_usb_dp` is in the descriptor. A second USB host on PIO is well-trodden, but
it wants a spare PIO and all three are committed now: link, I²S, gamepads. It
really needs the gamepad reader to release pio2 when its dialog closes, which it
does not do.

Flagged as much for the resource conflict as for the test. Worth resolving before
another PIO consumer turns up.

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

### 11. SD write, and a results report

Two related ideas. Writing to a card is genuinely destructive so it needs
explicit consent, but a write, read and restore of one sector in unallocated
space is a real test of the bidirectional path.

The more valuable half is writing the results *to* the card. A production rig
wants a record: DS2401 serial, board type, every measurement, a timestamp from
the RTC. One text file per unit turns this from a bench tool into something
traceable.

### 12. SD 4-bit mode (`CAP_SD_4BIT`)

One board wires `sd_dat1` and `sd_dat2`. Testing 4-bit transfer would prove those
two lines, which 1-bit SPI mode never touches. A solder fault on DAT1 is
invisible today.

---

## Tier 3: infrastructure rather than tests

### 13. Burn-in loop

Run the whole suite over and over, counting failures per test. That catches the
class of fault a single pass never will: PSRAM that fails once warm, a link that
degrades, an intermittent joint.

Needs per-row failure counts and a running duration rather than just the latest
state. Pairs naturally with the report export above.

### 14. Long-run link error rate

The link test measures a burst. A soak measuring bit error rate over minutes
would characterise it properly, which is the difference between "it works" and
"it works reliably".

### 15. Slave-side self-test

On a Core 2 the slave is a whole RP2350 with its own flash and RAM, and all we
prove today is that it answers. It could run its own memory tests and report over
the link, roughly doubling the coverage of a dual-chip board.

### 16. Clock cross-check

MegaFRANK has a 3.58 MHz crystal for the AYs and a 32.768 kHz one for the RTC.
Measuring the system clock against the RTC gives a real accuracy figure for both.
A crystal that is present but off-frequency is otherwise undetectable.

---

## Not worth building

**Audio verification.** No loopback and no ADC anywhere in the fleet. Putting a
PASS on the audio dialogs would be inventing a result. If some future board
routes audio back to an ADC pin, this changes.

**Display presence.** No board wires hot-plug detect. *Video output* counting
emitted frames is as far as honesty goes.

**AY readback.** The 74HC595 chain is write-only. Nothing firmware can do.

**Flash write and erase.** The firmware is running from it. A scratch-sector test
is possible, but the risk against the value is poor, and the settings sector
exercises that path incidentally anyway.

---

## Suggested order

1. PS/2 keyboard and mouse. Biggest gap, on the most boards
2. LEDs. Needs the dialog pattern, same as the gamepad one
3. USB HID host reporting. The data is already there
6. Report export to SD. Makes the rig traceable
7. LEDs and DB9 gamepads. Quick once the dialog pattern gets reused
8. ESP-01S and ESP32. Needs modules to test against
9. Burn-in loop and link soak. Once the rest is stable
10. PIO-USB. After sorting out who owns which PIO

Tier 1 is done apart from PS/2 and the LEDs, both of which need a dialog rather
than a pass/fail row. Tested capabilities are now eighteen of thirty-one.
