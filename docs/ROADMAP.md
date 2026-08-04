# What else can be tested

An analysis of the gap between what the board descriptors *declare* and what
this firmware currently *exercises*, with a suggested order of work.

Every capability bit in `core/frank_caps.h` is a claim about hardware. Fourteen
of the thirty-one are currently tested. This document takes the rest in turn,
says what a test could honestly prove, and — as important — what it could not.

The principle throughout is the one the firmware already follows: a rig that
claims coverage it does not have is worse than one that admits the gap. Several
items below are marked *not worth building* for exactly that reason.

---

## Where things stand

| Capability | State |
|---|---|
| `PSRAM_QMI`, `PSRAM_SOFTSPI` | Tested — probe and address-seeded sweep |
| `SD` | Tested — CID/CSD and sector 0 |
| `RTC_DS3231` | Partially — an ACK at 0x68 only |
| `ONEWIRE_DS2401` | Tested — ROM read |
| `LINK` | Tested — handshake, throughput, slave reset |
| `VIDEO_HDMI/VGA/COMPOSITE` | Tested — frames counted per backend |
| `AUDIO_I2S`, `AUDIO_MUX`, `TURBOSOUND` | Interactive — driven, not verified |
| `GAMEPAD_NES` | Interactive |
| `TAPE_IN`, `TAPE_DIP_GATED` | Interactive |
| `PS2` | **Untested** |
| `USB_HOST`, `USB_DEVICE`, `USB_HUB`, `USB_MUX`, `PIO_USB` | **Untested** |
| `GAMEPAD_DB9` | **Untested** |
| `ESP01`, `ESP32_SPI` | **Untested** |
| `LED_PLAIN`, `LED_WS2812` | **Untested** |
| `DIPSWITCH` | **Untested** |
| `I2C` | **Untested** as a bus |
| `AUDIO_AMP`, `AUDIO_CODEC_I2C` | **Untested** |
| `SD_4BIT` | **Untested** |

---

## Tier 1 — clear wins, low risk

These test real hardware, fail for one identifiable reason, and need no
judgement call from the operator.

### 1. PS/2 keyboard and mouse (`CAP_PS2`)

Descriptors already carry `ps2_kb_clk/dat` and `ps2_ms_clk/dat`. Present on most
of the fleet and completely unexercised.

A PS/2 device drives the clock line itself, so **presence is measurable without
a device doing anything**: idle both lines high, then watch for a clock burst on
plug-in or key press. A dialog in the shape of the gamepad one — draw a keyboard
and a mouse, show scancodes and movement deltas as they arrive.

- **Proves** the connector, the level shifting and the PIO receiver.
- **Cannot prove** anything without a device attached — so an empty port must
  report *could not run*, never a failure.
- Notable: the PS/2 connectors are a common cold-solder site. This is probably
  the highest-value missing test in the list.

### 2. Config DIP switches (`CAP_DIPSWITCH`)

The descriptor has a `dip` pin, and *Manual Steps* already asks the operator to
set switches the firmware cannot reach. On boards where the DIP is wired to a
GPIO it **can** be read — which turns an instruction into a verification.

- **Proves** the switch positions actually in force.
- **Changes the shape of other tests**: the tape input is DIP-gated on three
  boards, so instead of "close S2 1-4 and try again", the firmware could say
  "S2 1-4 is open — that is why there is no signal".
- Cheap: one GPIO read, plus surfacing it in the *Manual Steps* panel.

### 3. LEDs (`CAP_LED_PLAIN`, `CAP_LED_WS2812`)

A plain LED is one GPIO. A WS2812 is one PIO state machine and a colour cycle.

- **Proves** the driver pin and, for WS2812, the timing-critical bit protocol.
- **Cannot prove** the LED is fitted or lit — this is an operator-confirmed
  check, so it belongs in a dialog ("do you see red, then green, then blue?"),
  not a pass/fail row.
- Worth it mainly because a WS2812 that does not light is usually a data-line
  fault, and that *is* worth catching.

### 4. I²C bus scan (`CAP_I2C`)

The detector already probes 0x68 and the codec address. A full 0x08–0x77 scan
costs milliseconds and lists everything present.

- **Proves** the bus pulls up and clocks, independent of any one device.
- Diagnostic value beyond pass/fail: an unexpected address is informative, and a
  bus with *nothing* on it distinguishes a dead bus from a missing chip.

### 5. RTC oscillator, not just presence

The current test acks at 0x68 and stops. A DS3231 with a dead crystal or a flat
backup cell acks perfectly.

- Read the seconds register, wait, read again. **Proves the oscillator runs.**
- Read the oscillator-stop flag in the status register — that is exactly what it
  is for, and it reports a battery that has been flat since last power-down.
- Small change, materially better test.

---

## Tier 2 — worthwhile, more work

### 6. USB HID host reporting (`CAP_USB_HOST`)

The input layer already enumerates keyboards and mice — the interface depends on
it — but nothing reports what was found. A dialog showing VID/PID, product
string, endpoint and live key/button/wheel events would make an invisible
subsystem visible.

- **Proves** the USB connector, the host stack and the device.
- Effectively free: the data is already flowing, it just is not displayed.

### 7. USB hub and mux (`CAP_USB_HUB`, `CAP_USB_MUX`)

Core 2U carries a hub. Enumerate through it and report the device count and per
port occupancy.

- **Proves** the hub powers up and enumerates.
- The mux is switch-selected, so like the audio mux this is half a path: the
  firmware can say which side it can see and must say that it cannot select.

### 8. PIO-USB second port (`CAP_PIO_USB`)

`pio_usb_dp` is in the descriptor. A second USB host on PIO is a well-trodden
path but it needs a spare PIO — and all three are now committed (link, I²S,
gamepads). Realistically this needs the gamepad reader to release pio2 when its
dialog closes, which it currently does not.

- Flagged as much for the **resource conflict** as for the test itself. Worth
  resolving before more PIO consumers appear.

### 9. DB9 gamepads (`CAP_GAMEPAD_DB9`)

OldSkoolFRANK wires Atari-style directly to GPIOs — no shift register, so the
NES reader does not apply. Simple pin reads, same dialog treatment.

### 10. ESP-01S / ESP32 (`CAP_ESP01`, `CAP_ESP32_SPI`)

Descriptors carry a UART pair, chip-enable, GPIO0, a full SPI set, handshake and
ready lines, and a mux select. Two distinct tests:

- **ESP-01S**: toggle CH_PU, send `AT`, expect `OK`. Proves the module is
  powered, reset-controllable and talking.
- **ESP32 SPI**: assert CS, exercise the handshake/ready pair.

Both need the module fitted, so absence must report *could not run*.

### 11. SD write, and a results report

Two related ideas.

Writing to a card is genuinely destructive, so it needs explicit consent — but a
write/read/restore of one sector in unallocated space is a real test of the
bidirectional path.

More valuable: **write the results to the card**. A production rig wants a
record — DS2401 serial, board type, every measurement, timestamp from the RTC.
One text file per unit turns this from a bench tool into a traceable process.

### 12. SD 4-bit mode (`CAP_SD_4BIT`)

One board wires `sd_dat1`/`sd_dat2`. Testing 4-bit transfer would prove those two
lines, which 1-bit SPI mode never touches — a solder fault on DAT1 is invisible
today.

---

## Tier 3 — infrastructure, not tests

### 13. Burn-in loop

Run the whole suite repeatedly, counting failures per test over time. Catches
the class of fault a single pass cannot: a PSRAM that fails warm, a link that
degrades, an intermittent joint.

- Needs failure counts per row and a running duration, not just the latest state.
- Pairs naturally with report export.

### 14. Long-run link error rate

The link test measures a burst. A soak measuring bit error rate over minutes
would characterise it properly — the difference between "it works" and "it works
reliably".

### 15. Slave-side self-test

On Core 2 the slave is a whole RP2350 with its own flash and RAM, currently only
proven to answer. It could run its own memory tests and report results over the
link, doubling the coverage of a dual-chip board.

### 16. Clock cross-check

MegaFRANK has a 3.58 MHz crystal for the AYs and a 32.768 kHz one for the RTC.
Measuring the system clock against the RTC gives a genuine accuracy figure for
both — a crystal that is present but off-frequency is otherwise undetectable.

---

## Explicitly not worth building

- **Audio verification.** No loopback and no ADC anywhere in the fleet. Adding a
  "PASS" to the audio dialogs would be inventing a result. If a future board
  routes audio back to an ADC pin, this changes.
- **Display presence.** No board wires hot-plug detect. *Video output* counting
  emitted frames is the honest ceiling.
- **AY readback.** The 74HC595 chain is write-only. Nothing to be done in
  firmware.
- **Flash write/erase.** The firmware is running from it. A scratch-sector test
  is possible but the risk-to-value ratio is poor, and the settings sector
  already exercises the path incidentally.

---

## Suggested order

1. **PS/2 keyboard and mouse** — biggest coverage gap on the most boards
2. **DIP switch readback** — small, and improves the tape and audio guidance
3. **RTC oscillator check** — small, closes a real hole in an existing test
4. **I²C scan** — small, good diagnostic value
5. **USB HID host reporting** — the data already exists
6. **Report export to SD** — turns the rig into a traceable process
7. **LEDs, DB9 gamepads** — quick wins once the dialog pattern is reused
8. **ESP-01S / ESP32** — needs modules to test against
9. **Burn-in loop and link soak** — once the above is stable
10. **PIO-USB** — after resolving PIO ownership

Items 1–4 are each an afternoon and would take tested capabilities from fourteen
to eighteen of thirty-one.
