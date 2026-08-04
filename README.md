# FRANK Test

A universal hardware test firmware for the FRANK family of RP2350 retro-computer
boards. One image identifies which board it is running on, works out what that
board actually has, and tests only that — reporting what it measured rather than
a verdict it cannot support.

![Board Tests](screenshots/board-tests.png)

---

## Contents

- [What it is](#what-it-is)
- [Supported boards](#supported-boards)
- [Getting started](#getting-started)
- [The interface](#the-interface)
- [Board detection](#board-detection)
- [Video](#video)
- [The tests](#the-tests)
- [Interactive checks](#interactive-checks)
- [What it cannot test](#what-it-cannot-test)
- [Building](#building)
- [Releases](#releases)
- [Repository layout](#repository-layout)
- [Contributing](#contributing)
- [Attribution](#attribution)
- [Licence](#licence)

---

## What it is

FRANK boards share a lineage — the Murmulator pin conventions — but differ in
silicon, memory, connectors and audio. Maintaining a separate test firmware per
board meant a dozen nearly-identical images, each drifting from the others.

This is one image for all of them. It:

- identifies the board from silicon, an I²C/1-Wire inventory and a GPIO
  fingerprint, and says how confident it is;
- gates every test on what that board declares, so a missing peripheral reports
  **n/a** rather than a failure;
- distinguishes *could not run* from *failed*, because they mean different
  things to whoever is holding the board;
- prints what it measured — clock rates, chip IDs, throughput, capacities — not
  just a tick.

The interface is a windowed desktop at 640×480, driven entirely from the
keyboard, with HDMI, VGA and composite output.

---

## Supported boards

| Board | MCU | Notes |
|---|---|---|
| FRANK | RP2350A (Pico 2 socket) | DIP-gated tape input |
| FRANK PGA | PGA2350 module | |
| MegaFRANK | RP2350B | TurboSound, SPI PSRAM, RTC, unit serial |
| miniFRANK | RP2350A | DIP-gated tape input |
| microFRANK | RP2350A | |
| zeroFRANK | RP2350A | |
| OldSkoolFRANK | RP2350A | DB9 gamepads |
| Nyx | RP2350A | No USB host, no PS/2 |
| FRANK Core 2 | RP2350B + RP2350A | Inter-processor link |
| FRANK Core 2U | RP2350B + RP2350A | As Core 2, plus tape and a USB hub |
| FRANK Next | RP2350B | TLV320 codec |

An unidentified board still boots and draws the screen using only the
conventions common to the whole fleet — video on GP12–19, microSD on GP4–7, I²S
on GP9–11 — which is enough to show an interface and deliberately not enough to
test anything.

---

## Getting started

1. Hold **BOOTSEL**, connect USB, and drop
   `frank-test_<version>_master.uf2` onto the mass-storage device that appears.
2. Connect a display. HDMI is the default; see [Video](#video) for VGA and
   composite.
3. Attach a USB or PS/2 keyboard.
4. Press **S** and choose your board.
5. Press **A** to run everything.

On a Core 2 or Core 2U, also flash `frank-test_<version>_slave.uf2` to the second
chip. Without it the link tests correctly report that nothing answered.

---

## The interface

![Menus](screenshots/menus.png)

Everything is reachable from the keyboard, because some boards in this fleet
have no pointing device at all.

| Key | Action |
|---|---|
| `Alt`+`F` `B` `V` `U` `T` | Open the File, Board, Video, Audio or Tests menu |
| arrows | Move within a menu or the test list |
| `Enter` | Activate; on the list, run the selected test |
| `Esc` | Close a menu or dialog |
| `S` | Set Board |
| `A` | Run All |
| `E` | Run Selected |
| `?` | Key help on the console |

A mouse works if one is attached, and the pointer is drawn by patching the
framebuffer directly rather than recomposing the screen, so it stays responsive
even while audio is playing.

**Enter BOOTSEL has no keyboard equivalent, deliberately.** It is the one command
that ends the session, and a stray keypress used to trigger it.

### Reading the results

| Mark | Meaning |
|---|---|
| ✓ | Passed, with the measurement beside it |
| ✗ | Failed — this board has the hardware and it did not work |
| — | **n/a**: the board has no such hardware. Not a defect |
| ? | **Could not run** — the result is unknown, which is not the same as bad |

The *Manual Steps* panel lists switches and jumpers this firmware cannot reach.
Where a board has an audio mux or a tape jumper, the software half of the path
can be exercised and the analogue half cannot; saying so is the difference
between a rig you can trust and one you cannot.

---

## Board detection

Four tiers, each narrowing the field, and the result is reported with its
confidence rather than presented as fact.

1. **Silicon.** `SYSINFO_PACKAGE_SEL` separates RP2350A from RP2350B, and the
   chip revision and unique ID come from the same place. Flash JEDEC ID and
   PSRAM presence and size follow.
2. **Inventory.** A DS3231 at 0x68, a TLV320 codec, a DS2401 silicon serial —
   each present on some boards and not others.
3. **Fingerprint.** Pins are pulled up, released and classified by how they
   settle. The result is scored against each descriptor as a *ratio* of pins
   matched, so a board with seven signature pins does not out-score a board with
   six simply by having more.
4. **The operator.** Some boards are genuinely indistinguishable — Core 2 and
   Core 2U share a pin map exactly. When two descriptors tie, the firmware says
   so and asks, rather than picking one and being quietly wrong.

![Set Board](screenshots/set-board.png)

The choice is **not** written to flash. A stored answer is invisible, survives
the operator changing their mind, and follows the firmware rather than the board
when an image is moved. A reset starts again from what the hardware says.

---

## Video

Three backends, all at 640×480, selected at boot or from the Video menu.

| Mode | How | Notes |
|---|---|---|
| **HDMI** | HSTX, TMDS | Default. `clk_hstx` at 126 MHz |
| **VGA** | HSTX, raw 8-lane | Same GP12–19 pins through the resistor ladder |
| **Composite** | PIO software encoder | PAL/NTSC, 320×240 |

Hold **H**, **V**, **C** or **A** (auto) during the two-second window at boot, or
choose from the Video menu — that persists the choice and reboots, because the
boot path is the only code path that brings a backend up which has ever been
tested.

Composite does not get the desktop. It cannot: a composite line carries roughly
320 usable samples however the source is arranged, and 6-pixel type resampled to
fit stops being type. It renders its own text page instead — 42 columns by 30
rows at native resolution, with the same information and a legible result.

Menu items are enabled only when the board has the connector **and** this
firmware has a backend for it. An enabled control that silently falls back to
something else is worse than a greyed-out one.

---

## The tests

### Silicon and memory

| Test | What it proves | What it does not |
|---|---|---|
| **Silicon** | MCU class, revision, unique ID, system clock | |
| **Flash ID** | JEDEC manufacturer/device ID and capacity | Nothing about contents |
| **Flash read** | Sustained XIP read throughput | |
| **Flash CRC32** | A CRC over the image — a fingerprint of what is loaded | |
| **QSPI PSRAM** | The chip answers on its chip select, and its size | |
| **QSPI PSRAM sweep** | Write-and-verify across the whole part | |
| **SPI PSRAM** | MegaFRANK's second, bit-banged PSRAM: manufacturer ID | Needs S10 closed |
| **SPI PSRAM sweep** | One block per 64 KiB, seeded from the address | |

The memory benchmarks run **before** video starts. HSTX scanout contends with
XIP for the same bus, and measuring afterwards understated flash throughput by a
factor of four — a number that looked like a hardware fault and was not.

The sweeps seed each block from its own address, so a block read back from the
wrong place fails rather than matching by luck. That is what catches a dropped
address line.

### Board peripherals

| Test | What it proves |
|---|---|
| **Video detect** | Which output the detector chose, and why |
| **Video output** | Frames are actually being emitted, by counting them |
| **RTC** | A DS3231 acknowledges at 0x68 |
| **Unit serial** | The DS2401 ROM — the only per-unit identity in the fleet that is not a guess |
| **GPIO short scan** | Adjacent-pin solder bridges |

The short scan skips pins that are *deliberately* tied: the link buses run
adjacent pins to the same places by design, GP9–11 are joined by the audio
network, and driving the link's control lines resets the slave. Reporting those
every run would train the operator to ignore the result.

### Storage

| Test | What it proves |
|---|---|
| **SD card** | CMD0/CMD8/ACMD41 succeed; CID and CSD decode to manufacturer, product, capacity |
| **SD read** | Sector 0 comes back with a boot signature |

Two rows because they fail for different reasons: sixteen bytes of CID can
succeed on a link that falls apart over 512. An unformatted card reports *could
not run*, not a failure — that is not a board fault.

### Inter-processor link (Core 2 / Core 2U)

| Test | What it proves |
|---|---|
| **Processor link** | Handshake, then bidirectional throughput — typically ~96 MiB/s duplex |
| **Slave reset** | The master can reboot the slave over FS and see it come back |

Both need the slave image running. The link stack escalates recovery — an FS
request every second failure, a reset pulse every fourth — because a slave that
boots late or is reflashed has to be noticed rather than forced into step.

---

## Interactive checks

Some things cannot be tested by a machine that has no way to observe the result.
Nothing on any FRANK board can hear its own audio output or read back a
write-only shift register, so a PASS in a results list would be a claim the
firmware is in no position to make. These are dialogs you drive instead.

### Audio — `Alt`+`U`

![Audio](screenshots/audio.png)

**PWM**, **TDA (I²S)** and **TurboSound**, each looping a short melody through
left, right and centre and naming the channel as it plays. A single tone cannot
tell a working stereo path from a stuck LRCK — they all sound like success.

On a board with an audio mux the dialog states the switch positions that source
needs. The mux is a 4:1 selector, not two enables: setting both switches selects
ground, which is silence and looks exactly like a dead amplifier.

### NES gamepads — `Tests` ▸ `NES Gamepad(s)`

Both ports drawn schematically, each button lighting as it is pressed and named
underneath. A row saying "gamepad: PASS" cannot mean anything; a row saying
"0x21" means something to nobody.

### Tape in — `Tests` ▸ `Tape In`

The CD4069 squares the audio into a digital edge stream, and this counts and
times it. The visualisation is a ZX Spectrum loading stripe, and it is not
decoration: a Spectrum paints one border band per tape pulse, so band thickness
*is* pulse length. Thick red/cyan bands are pilot tone, thin blue/yellow are
data, a stalled picture means nothing is arriving — none of which needs anyone to
read a number.

---

## What it cannot test

Stated plainly, because a rig that quietly claims coverage it does not have is
worse than one that admits the gap.

- **Anything past an audio pin.** No loopback, no ADC. The firmware can prove
  SCLK and LRCK are clocking, and nothing about the DAC, the amplifier or the
  speaker.
- **The AY chips themselves.** The 74HC595 chain is write-only.
- **Whether a display is attached.** No FRANK board wires hot-plug detect. An
  absent monitor and a broken output are indistinguishable, so *Video output*
  counts emitted frames and says only that.
- **Switch positions.** Every board has switches the firmware cannot read. They
  are listed in *Manual Steps*.
- **Composite lock.** The encoder can be proven to run; whether a television
  syncs to it is something only a television can say.

---

## Building

Requires the [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.x and the Arm
GNU toolchain. `sdk_env.sh` locates the SDK and rejects a stale `PICO_SDK_PATH`
rather than trusting it.

```sh
make build                    # default board (frank_core2_master)
make build BOARD=frank_core2u_master
make flash                    # hold BOOTSEL first
```

Or directly:

```sh
cmake -S app -B app/build -DPICO_BOARD=frank_core2_master
cmake --build app/build -j8
```

### Console

The build is a USB **HID host** so keyboards and mice can be tested, and the
RP2350's single USB controller cannot also be a CDC device. The console is
therefore UART, at 115200. For bring-up work, `-DUI_INPUT_USB_HID=OFF` swaps the
host for a USB serial console.

---

## Releases

`version.txt` holds `MAJOR MINOR` and is the single source of truth: CMake reads
it to stamp the banner and the About box, and `release.sh` writes it before
building, so a binary can never disagree with its filename.

```sh
./release.sh 1.00
```

Produces, in `release/`:

- `frank-test_1_00_master.uf2` — the test firmware
- `frank-test_1_00_slave.uf2` — the link peer for Core 2 / Core 2U

---

## Repository layout

```
app/          the test firmware executable and its CMake
core/         board table, capabilities, detection, settings, registry
ui/           4 bpp drawing, windows, menus, icons, video backends
tests/        one file per subsystem under test
common/       the inter-processor link stack, shared with the core2 firmware
drivers/      vendored hardware drivers (see Attribution)
boards/       Pico SDK board headers for each FRANK variant
slave/        the link peer firmware
screenshots/  captured over HDMI from real hardware
```

`master/`, `probe/` and `selftest/` are inherited from the frank_core2 firmware
this was branched from. Only `master/src/ui_font.c` is still used.

---

## Contributing

```sh
make hooks    # point git at .githooks
```

**This repository's history names the people responsible for it and nobody
else.** Commit messages crediting an AI — co-author trailers, "generated with"
advertisements, session links — are rejected by the `commit-msg` hook locally and
by the `Attribution` job in CI for everyone. A local hook only protects a clone
that opted in and cannot see a commit made through GitHub's web surface, which is
why the CI gate is the one that actually enforces the policy.

---

## Attribution

This firmware stands on a good deal of other people's work. Vendored code keeps
its original authorship and licence; the files carry a provenance note at the
top.

| Component | Source | Author |
|---|---|---|
| Composite PAL/NTSC encoder (`drivers/tv`) | [frank-msx](https://github.com/rh1tech/frank-msx), from murmnes | Mikhail Matveev, after the Murmulator lineage |
| NES/SNES gamepad PIO reader (`drivers/nespad`) | [pico-infonesPlus](https://github.com/fhoedemakers/pico-infonesPlus) | shuichitakano, fhoedemakers — MIT |
| SD CID/CSD decode and manufacturer table (`tests/tests_sd.c`) | SpeccyP, `drivers/pico_fatfs/tf_card.c` | DnCraptor and contributors |
| FatFs (`drivers/fatfs`) | [elm-chan.org](http://elm-chan.org/fsw/ff/) | ChaN — BSD-style |
| I²S audio PIO (`drivers/audio_i2s.pio`) | Raspberry Pi Pico examples lineage | |
| USB HID host, XInput (`drivers/usbhid`) | TinyUSB and contributors | |
| HSTX VGA register configuration | DispHSTX | Miroslav Nemecek |
| Adjacent-pin short scan | [murmulator-tester](https://github.com/DnCraptor/murmulator-tester) | DnCraptor |
| TurboSound 74HC595 word format | SpeccyP, `aySoft.h` | DnCraptor |

Two local edits were made to the vendored composite driver, both forced by this
firmware's resource layout rather than by preference:

- `PIO_VIDEO` selects **pio2**. pio0 carries the inter-processor link and pio1
  the I²S audio.
- The DMA request line comes from `pio_get_dreq()` instead of a hardcoded
  `DREQ_PIO1_TX0`, which predates the RP2350's third PIO and silently hands pio2
  the pio1 request.

A third change publishes the encoder's emitted-frame count, so *Video output* has
something honest to measure.

---

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

Copyright © 2026 Mikhail Matveev — <https://rh1.tech>
