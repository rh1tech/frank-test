# FRANK universal test firmware — plan

One test firmware for the whole FRANK fleet. Three binaries (one per MCU
class), each of which works out which board it is sitting on, works out
which display is plugged in, and then runs every test that board can
support — reporting `n/a` rather than `fail` for the ones it cannot.

Base: `frank_core2u/firmware` merged with `frank_core2/firmware`.
`frank_core2u` is a strict superset of `frank_core2` for every shared
file, so the merge is the core2u tree plus core2's three board headers
(`frank_core2_master.h`, `frank_core2_slave.h`, `frank_core2_board.h`).
That copy is already in place — 89 files, no build artefacts.

---

## 1. The fleet

Extracted from the KiCad schematics via `kicad-cli sch export netlist`,
so every pin below comes from the netlist rather than from a datasheet
or a memory of what was intended. Boards excluded per instruction:
`frank_air` (dead), `xt8086_beta` / `xt8086` (not a FRANK).

### 1.1 Silicon and memory

| Board | MCU(s) | Package | Flash | PSRAM (CS) | Second processor |
|---|---|---|---|---|---|
| `frank` | Pico / Pico 2 **module** | RP2040 *or* RP2350A | on module | — | RP2040-Zero (U12) |
| `frank_pga` | PGA2350 module | RP2350B | on module | — | — |
| `megafrank` | PGA2350 module | RP2350B | on module | ESP-PSRAM64, **soft SPI** GP31/32/33 | — |
| `microfrank` | RP2350 QFN-60 | RP2350A | W25Q128 16 MB | ESP-PSRAM64H (GP8) | — |
| `minifrank` | RP2350 QFN-60 | RP2350A | W25Q128 16 MB | ESP-PSRAM64H (GP8) | ESP-01S |
| `zerofrank` | RP2350 QFN-60 | RP2350A | W25Q128 16 MB | ESP-PSRAM64H (GP8) | — |
| `oldskoolfrank` | PGA2350 module | RP2350B | on module | — | ESP-01S |
| `hecate` | RP2040 QFN-56 | RP2040 | W25Q128 16 MB | — | — |
| `nyx` | RP2350 QFN-80 | RP2350B | W25Q128 16 MB | ESP-PSRAM64 (GP47) | — |
| `frank_core2` | RP2350B + RP2350A | both | 2× W25Q128 | 2× (M:GP47, S:GP0) | the slave |
| `frank_core2u` | RP2350B + RP2350A | both | 2× W25Q128 | 2× (M:GP47, S:GP0) | the slave |
| `frank_next` | RP2350B + ESP32-D0WD-V3 | RP2350B | W25Q128 + W25Q32 | ESP-PSRAM64H (GP47) | ESP32 over SPI |
| `turbosound` | — | — | — | — | AY daughterboard |

`frank`'s CPU is socketed: the same PCB is an RP2040 board or an RP2350A
board depending on which module is fitted. Detection has to survive that.

`turbosound` has no MCU. It is an accessory tested through `megafrank`
(where the same circuit is on-board) or through `minifrank`'s
`Ext_Sound` header.

### 1.2 Peripherals

| Board | Video | Audio | SD | PS/2 | Gamepad | USB | Tape | RTC | 1-Wire ID |
|---|---|---|---|---|---|---|---|---|---|
| `frank` | HDMI + VGA + CVBS (shared GP12–19) | TDA1387 + PAM8403 + 4052 mux | GP4–7 | TXS0104 GP0–3 | 2× GP26/27 | MW7211A hub, stacked A | CD4069 GP22 | — | — |
| `frank_pga` | HDMI + VGA + CVBS | TDA1387 + PAM8403 + 4052 | GP4–7 | TXS0104 GP0–3 | 2× GP26/27 | hub + 4052 mux | CD4069 GP22 | — | — |
| `megafrank` | HDMI + VGA + CVBS | TDA1387 + PAM8403 + 4052 + **TurboSound** (2× AY, GP9/10/11) | GP4–7 | TXS0104 GP0–3 | 2× GP26/27 | hub + 4052 mux | CD4069 | DS3231 GP28/29 | **DS2401 GP30** |
| `microfrank` | HDMI only | TDA1387 + LM358 | GP4–7 | — | — | TS3USB221 mux + hub | — | — | — |
| `minifrank` | HDMI + VGA | TDA1387 + 4052 + Ext_Sound hdr | GP4–7 | TXS0104 GP0–3 | GP26/27 | mux + hub | CD4069 | — | — |
| `zerofrank` | HDMI only | TDA1387 + LM358 | GP4–7 | — | — | native + **PIO-USB GP20/21** | — | — | — |
| `oldskoolfrank` | HDMI + VGA, **solder-jumper selected** | **TDA1545A** + LM358 | GP4–7 | GP0–3 (1K series) | DB9 GP20/21/26/27 | hub + 4052 | CD4069 GP22 | — | — |
| `hecate` | — | — | — | TXS0104 GP11/12/14/15 | — | PIO-USB GP2–5, stacked A | — | — | — |
| `nyx` | — | — | — | — | — | native only | — | — | — |
| `frank_core2` | HDMI | TDA1387 + LM358 | GP4–7 | — | — | 2× native | — | — | — |
| `frank_core2u` | HDMI | TDA1387 + LM358 | GP4–7 | — | — | 2× native + hub + TS3USB221 | CD4069 GP45 | — | — |
| `frank_next` | HDMI | **TLV320DAC3100** I2C+I2S, MCLK GP24 | **4-bit** GP2–7 | — | NES GP20/21 + GP26/27 | CH334F hub + CH343P + TPS2116 | CD4069 GP22 | DS3231 GP28/29 | **DS2401 GP23** |

Full per-GPIO tables for every board are in
`docs/pinouts.md` (generated — see §7).

### 1.3 The pin conventions that survive across the fleet

Worth stating because they are what make one firmware plausible:

- **GP4–7 = microSD** on every board that has a card slot, in the same
  order (DAT0, CD/DAT3, CLK, CMD) — except `frank_next`, which is 4-bit
  on GP2–7.
- **GP9/10/11 = I2S** (DATA, SCLK/BCLK, LRCK/WCLK) on every audio board.
- **GP12–19 = video**, always in the HDMI order CLKN, CLKP, D0N, D0P,
  D1N, D1P, D2N, D2P — and on the boards that also have VGA, the same
  eight pins carry the resistor-ladder RGB and syncs.
- **GP0/1 = UART0** wherever a debug header exists (`frank_next` moves it
  to GP40/41 and muxes GP0/1 to the ESP32).

This matches Murmulator 2.0, which is why the frank-msx drivers already
in `drivers/` drop in unmodified.

### 1.4 The RP2040 question

There are three RP2040s in the fleet, and none of them is a computer:

| Where | Part | Fitted | Video |
|---|---|---|---|
| `hecate` U2 | RP2040 QFN-56, soldered | always | none |
| `frank` U12 | RP2040-Zero module | optional | none |
| `frank` U6 | Pico 1 **or** Pico 2 socket | Pico 2 only | yes |

`hecate` is a PS/2-to-USB adapter. `frank`'s U12 turns out to be the same
function as a plug-in module — it sits on the PS/2 lines (GP0–3) and on
two USB hub downstream ports (DP1/DM1, DP2/DM2), so it is a USB-host-to-
PS/2 converter co-processor. Neither has any video hardware.

**So there is no RP2040 firmware target.** Both adapters are exercised
through the PS/2 keyboard and mouse tests of whatever FRANK board they
are plugged into, which is also the only configuration that matters —
testing the converter in isolation would prove less than testing the
path it exists to serve. `frank`'s socket always takes a Pico 2.

That leaves **two binaries**, RP2350A and RP2350B, and it removes the
constraint that shaped the interface: the framebuffer no longer has to
fit 264 KB. See §5.1.

---

## 2. Board detection

### 2.1 Is it possible?

Yes, for 11 of the 12 boards, from silicon alone plus active probes plus
a passive pin signature. One pair — `frank_core2` and `frank_core2u` —
is effectively indistinguishable and needs a declared identity. The
design below is therefore four tiers, most authoritative first, and it
always prints what each tier concluded so a disagreement is visible
rather than silently resolved.

Precedence: **compiled-in > `FRANKID` flash record > SD `frank.cfg` >
autodetect > interactive prompt**.

### 2.2 Tier 0 — silicon identity (free, no pins touched)

| Source | Distinguishes |
|---|---|
| `sysinfo->chip_id` | RP2040 vs RP2350, plus revision |
| `SYSINFO_PACKAGE_SEL` bit 0 | RP2350A (QFN-60) vs RP2350B (QFN-80) |
| Flash JEDEC ID + capacity | W25Q128 (16 MB) vs W25Q32 (4 MB) |
| `pico_get_unique_board_id()` | per-unit serial, not per-model |

This splits the fleet into the two binaries (§1.4):

- **RP2350A** — `frank`, `microfrank`, `minifrank`, `zerofrank`, and the
  core2/core2u *slave*
- **RP2350B** — `frank_pga`, `megafrank`, `oldskoolfrank`, `nyx`,
  `frank_next`, and the core2/core2u *master*

### 2.3 Tier 1 — active probes (positive identification)

Each of these is a yes/no that only one or two boards can answer yes to.

| Probe | Method | Identifies |
|---|---|---|
| **PSRAM CS sweep** | QMI probe through the *uncached* XIP alias `0x15000000` on each candidate CS {0, 8, 47}; address-aliasing size check | narrows RP2350A to {micro, mini, zero} vs slave; RP2350B to {nyx, next, master} |
| **Soft-SPI PSRAM** | manual 0x9F on GP31/32/33 (not QMI) | `megafrank` only |
| **I2C scan GP28/29** | 100 kHz, addresses 0x08–0x77 | 0x68 DS3231 → {`megafrank`, `frank_next`}; 0x18 TLV320 → `frank_next` only |
| **1-Wire ROM read** | reset + `0x33`, check family byte `0x01` and CRC8 | GP23 → `frank_next`; GP30 → `megafrank`. Also yields a **unique per-unit serial** |
| **Link doorbell** | raise DB_MS (GP41), wait 200 ms for DB_SM (GP42) | `frank_core2` / `frank_core2u` master |
| **ESP32 reset** | pulse CHIP_PU (GP38) and watch GP0/1 for the 115200 ROM banner | `frank_next` **only** |
| **ESP-01S talk** | send `AT\r\n` at 115200 on the board's ESP UART, look for `OK`. CH_PD and RST go to *buttons* on every ESP-01S board — no GPIO reaches them, so the ESP cannot be rebooted to make it announce itself and has to be spoken to instead | `minifrank`, `frank_pga`, `megafrank`, `oldskoolfrank` |

The uncached-alias detail matters and is already understood in the
existing firmware: through the cached window a write followed by a read
of the same address is answered from the 8 KiB XIP cache, so a missing
chip looks present.

### 2.4 Tier 2 — passive pin fingerprint (fills the remaining gaps)

For each GPIO in a per-class candidate list, classify the pad:

1. input, pull-up on, settle 200 µs, read → `u`
2. drive low 10 µs, release to input-no-pull, sample after a fixed delay → `r`
3. classify: `u=0` → **STRONG_LOW** (external pull-down or a load to
   ground); `u=1, r` rises fast → **STRONG_HIGH** (external pull-up);
   `u=1, r` rises slowly → **FLOAT**; intermediate → **LOADED**

The result is a vector compared against a per-board expected table with
a don't-care mask; the report gives the best match, the Hamming margin
to the runner-up, and every pin that disagreed.

> **Design constraint — do not rely on the internal pull-down.**
> RP2350 A2 silicon has a documented input-mode pull-down anomaly
> (RP2350-E9) where a pad configured as input with pull-down can sit at
> an intermediate voltage and read high. The naive "pull-down, read"
> half of a two-phase test is exactly the thing that erratum breaks, so
> the classifier above uses pull-**up** sensing plus a drive-and-release
> RC measurement instead. Confirm the erratum's current status against
> the RP2350 datasheet before implementation and re-check on real A2 and
> A4 parts — this is the single highest-risk assumption in the plan.

What the fingerprint resolves:

| Ambiguity | Discriminator |
|---|---|
| `microfrank` vs `minifrank` vs `zerofrank` | GP2/GP3 STRONG_HIGH (10K via TXS0104) → `minifrank`; GP20 STRONG_HIGH (1.5K USB D+ pull-up) → `zerofrank`; GP0–3 and GP20–29 all FLOAT → `microfrank` |
| `frank_pga` vs `oldskoolfrank` | `frank_pga` has 10K pull-ups on GP0–3; `oldskoolfrank` has 1K series to the PS/2 connector and 1K LED loads on GP23/GP24 |
| `nyx` | RP2350B, PSRAM on GP47, and *every other pin* FLOAT — nothing else is on the board |
| `hecate` | RP2040 with 22R on GP2–5 and TXS0104 on GP11/12/14/15 |
| RP2350A slave vs the standalone A boards | slave has the link buses live on GP1–23 and PSRAM CS on GP0, which no standalone board uses |

### 2.5 The one case autodetect cannot close

`frank_core2` and `frank_core2u` share the same MCU pin map. The only
electrical difference visible from the master is GP45: unconnected on
core2, wired to a CD4069 inverter input on core2u. That is a bare pad
versus roughly 5 pF of CMOS input plus trace — the RC method can see it,
but with a margin thin enough that it is not something to trust a
diagnostic verdict to.

Treat them as an ambiguous pair: autodetect narrows to `{core2, core2u}`
and then the declared identity decides. This is not a workaround so much
as an admission — two revisions of the same board that differ only in
unpopulated-from-the-MCU's-view circuitry are not distinguishable, and
pretending otherwise would produce a confident wrong answer.

### 2.6 Tier 3 — declared identity

- **Compiled** — `-DFRANK_BOARD=<id>`. When set it wins, but autodetect
  still runs and its verdict is printed beside it. Disagreement is a red
  heartbeat and a banner, because a mismatch means either a wrong flash
  or a real hardware fault — both worth stopping for.
- **`FRANKID` flash record** — magic, struct version, board id, board
  revision, unit serial, CRC32, written into a dedicated sector well
  away from the application. Survives reflashing the firmware. Written
  by the console `board set <id>` command or by `flash_all.sh --board`.
- **SD `frank.cfg`** — `board=core2u`, for the case where the board has
  a card slot and no probe is attached.
- **Interactive** — when the fingerprint is ambiguous and nothing has
  declared, present the candidate list on screen and on the console, and
  persist whatever the operator picks into `FRANKID` so it is asked once
  per board, not once per boot.

### 2.7 Hardware recommendation for the next revisions

Detection is doing real work here that three cheap resistors would make
unnecessary. For any new board revision:

- **A 3-bit ID strap** — three GPIOs each strapped to 3V3 or GND
  (8 codes, ~£0.01). Unambiguous, instant, no erratum exposure.
- **Or a DS2401** — one part, one pin, gives model *and* a unique unit
  serial, as `megafrank` and `frank_next` already do.

Adding either to `frank_core2`'s successor would close §2.5 permanently.

---

## 3. Video output detection

### 3.1 The constraint

**HPD (pin 19), DDC SCL/SDA (15/16) and CEC (13) are unconnected on
every FRANK board.** Verified across all 10 boards with an HDMI
connector — pin 17 (DDC/CEC ground) goes to GND and the rest go
nowhere. So there is no EDID to read and no hot-plug line to sense
through the standard channel. Whatever detection exists has to come out
of the TMDS driver pins themselves.

### 3.2 Two proven detectors already exist — use both

`~/Documents/GitHub/frank-msx` ships two independent, silicon-tested
detectors. They answer *different questions* and both cases occur in the
field, so the plan takes both rather than choosing.

**(a) `DispHstxAutoDispSel()` — `drivers/disphstx/disphstx_vmode.c:1225`**

Enable pull-ups on the six VGA RGB pins (GPIO12–17), wait 500 µs, read.
A VGA monitor's 75 Ω to ground drags all six to 0; an HDMI sink does
not, so they read `0x3F`. Answers *"is there a VGA monitor on the DB15?"*

This is the pull-up half of the termination argument, already validated
on RP2350 — and because it never enables a pull-down it sidesteps the
RP2350-E9 concern raised in §2.4 entirely. Prefer it over anything
hand-rolled.

**(b) `testPins()` — `drivers/test_pins.c`**

Probes whether GPIO12 and GPIO13 are electrically *shorted*, which is
what a passive HDMI-to-VGA ribbon/DAC does to the clock pair.
`main.c:439` sets `SELECT_VGA = (link == 0) || (link == 0x1F)`. Answers
*"is there a VGA adapter plugged into the HDMI socket?"* — a different
physical situation from (a), and invisible to it.

**(c) Composite has no detector, and cannot have one.**

`board_m2.h` sets `TV_BASE_PIN 12` — the software composite driver
drives the *same* GPIO12–19 DAC pins. A 75 Ω CVBS load to ground is
electrically indistinguishable from (a)'s VGA verdict on those pins.
This is not a gap in the implementation; it is why the manual override
in §3.4 is a requirement rather than a convenience.

### 3.3 Caveats

- `oldskoolfrank` selects between HDMI and VGA with **solder jumpers**
  JP3/JP4/JP5. The electrical test reports which output is *fitted*, and
  a sink on the unfitted connector is invisible. Say so in the report
  rather than claiming "no display".
- Some DVI-D sinks and most HDMI-to-VGA dongles do not present full
  termination until powered up. Re-probe on a timer while idle, not only
  at boot. The existing firmware's 5-second link-reprobe loop is the
  right shape to hang this on.
- The probe drives the video pins as GPIO, so it must run **before**
  `graphics_init()` claims them, and re-running it means tearing the
  video mode down and back up (§3.5).
- Detection is a hint, not a proof. After picking an output, drive it and
  ask *"press any key if you can read this"*; the keypress is the only
  evidence the whole chain — driver, connector, cable, sink — works. If
  no key arrives, fall through to the next candidate. Always mirror the
  full report to UART/CDC so a serial capture is complete even with no
  display at all.

### 3.4 Forced selection at boot — hold H / V / C

Autodetect cannot separate VGA from composite (§3.2c) and cannot see
past `oldskoolfrank`'s solder jumpers, so the operator needs a way to
say what they actually plugged in. During a bounded **boot window**
(~2 s, banner shown on whatever output autodetect picked, and printed to
the console):

| Key | Effect |
|---|---|
| `H` | force HDMI |
| `V` | force VGA |
| `C` | force composite |
| `A` | return to autodetect |

**The chicken-and-egg problem is the whole design difficulty here.** To
read a key before video comes up you need an input device already alive,
and the sources differ wildly in how fast they are ready:

| Source | Ready in | Available on |
|---|---|---|
| DIP switch / config jumper | instant, no stack | `minifrank` (GP22), `megafrank` (S1/GP22), `frank`, `frank_pga` (JP1) |
| UART console | instant, always present | all |
| PS/2 keyboard | ~500 ms (BAT `0xAA`), self-clocking, no enumeration | frank, pga, mega, mini, oldskool, hecate |
| USB HID keyboard | ~0.5–1.5 s (host enumeration + TinyUSB) | boards with a hub |

So: poll **every** source the detected board has, for the whole window,
and take the first answer. Do not wait for USB — let it join late and
still count if it answers before the window closes.

**There is no USB CDC console.** The USB controller runs as a HID *host*
so keyboards and mice can be tested, and it cannot also enumerate as a
serial device. That removes the slowest input source — CDC routinely
took longer to come up than any window worth waiting through — and moves
the console to UART, which is ready before the first line of `main()`.
It also retires the "repeat the banner for two seconds while CDC
enumerates" workaround the Core 2U firmware needs today.

Two consequences worth being explicit about:

- **The window costs boot time on every board.** Mitigate by making the
  choice sticky (below), so the window only matters the first time.
- **You may be pressing blind.** If autodetect guessed wrong you cannot
  read the banner. That is fine and intended — press `V` anyway and the
  correct output lights up. The keys must therefore work with no visual
  feedback whatsoever, which means the console must echo the accepted
  key so a serial-attached operator gets confirmation.

**Sticky.** A forced choice is written to the settings record alongside
`FRANKID` (§2.6) and reused on every subsequent boot without holding
anything. `A` clears it back to auto. This turns "hold a key every time"
into a one-off per board, which is what makes the window's cost
acceptable.

Precedence for the final video decision:

**boot key held > sticky setting > compiled `-DFRANK_VIDEO=` > autodetect (a)+(b) > HDMI**

### 3.5 Runtime selection

While the firmware is running, the same four choices are available from
the console (`video hdmi|vga|composite|auto`), from a hotkey on any
attached keyboard, and from an on-screen menu entry.

Implementation shape — this is the part with real engineering cost:

- Put the backends behind a **vtable** (`init`, `shutdown`, `set_mode`,
  `blit`) and link every backend the board's capability mask allows.
  frank-msx selects its backend at *compile* time (`HDMI_HSTX`,
  `VGA_HSTX`, `VIDEO_COMPOSITE`, or the default PIO path, mutually
  exclusive in `CMakeLists.txt`), so this is new work, not a port.
- **Switching within a backend family is live.** The PIO HDMI/VGA path
  already has both pipelines in one binary; adding a `graphics_shutdown()`
  and re-running `graphics_init()` with the other `SELECT_VGA` is cheap.
- **Switching across families is a persist-and-reboot.** HSTX HDMI claims
  DMA 0/1, `DMA_IRQ_0` (exclusive handler) and core 1; DispHSTX VGA
  claims core 1 too; the composite driver has its own scanline path.
  Writing correct teardown for four drivers to save 200 ms is poor value.
  Instead: write the choice to the settings record, `watchdog_reboot()`,
  and come up in the new mode. From the operator's point of view it is
  instantaneous, and it reuses the boot path that is already tested
  rather than a teardown path that would not be.

The report renderer must be backend-agnostic — one `report.c` drawing
through the vtable — or the four backends will drift into four slightly
different-looking reports.

### 3.6 Next-revision recommendation

Wire **HPD through a 10K/1K divider to a GPIO** and **DDC SCL/SDA to two
GPIOs**. That converts §3.2 from inference into reading the sink's EDID,
which additionally gives its native resolution and supported modes — so
the firmware could pick a mode the monitor actually likes instead of
always 640×480. It would not, however, remove the need for §3.4:
composite still has no channel to announce itself on.

---

## 4. Test matrix

Every test declares the capabilities it needs. The runner skips tests
whose capabilities the detected board lacks and prints **`n/a`** — which
must render differently from `fail`, because conflating "this board has
no PS/2" with "PS/2 is broken" is how a test suite stops being believed.

| # | Test | Covers | Boards |
|---|---|---|---|
| 1 | **Silicon** | chip id, package, revision, sys clock, core 1 launch, ROM version | all |
| 2 | **Flash** | JEDEC ID + capacity, XIP read throughput, CRC-32 over 64 KiB computed **twice** (a mismatch = marginal QSPI timing, which makes every later number suspect) | all |
| 3 | **PSRAM** | presence + size by address aliasing through the uncached alias, full write/verify sweep, throughput both directions | micro, mini, zero, nyx, next, core2×2, mega (soft-SPI variant) |
| 4 | **microSD** | card detect, CID/CSD decode, capacity, FAT mount, file write→read→compare, throughput | all with a slot; `frank_next` also 4-bit mode |
| 5 | **Video detect** | §3.2 — DispHSTX RGB pull-up probe + `testPins()` clock-pair short | all with video |
| 6 | **Video select** | §3.4 boot window (H/V/C/A from DIP, PS/2, UART, USB), sticky setting, §3.5 runtime switch | all with video |
| 7 | **Video output** | mode set, test card, colour bars, pixel-clock check, operator confirmation | all with video |
| 8 | **Audio — I2S** | 440 Hz L / 880 Hz R / 660 Hz both, ~0.2 s each. Separate channels at distinct pitches deliberately: one mono tone cannot tell a working stereo path from a stuck LRCK | all with a DAC |
| 9 | **Audio — codec** | TLV320DAC3100 I2C init + register readback + MCLK | `frank_next` |
| 10 | **Audio — amp/mux** | PAM8403 enable, 74HC4052 path selection, LM358 path | frank, pga, mega, mini, oldskool |
| 11 | **Audio — TurboSound** | AY register write/read, 74HC595 latch, 74HC74 clock divider, tone per channel | `megafrank`, `turbosound` via `minifrank` |
| 12 | **USB HID keyboard** | host enumeration, HID descriptor parse, make/break decode, modifiers, LED report (Caps/Num echo back to the keyboard) | boards with USB host |
| 13 | **USB HID mouse** | enumeration, X/Y deltas both signs, all buttons, wheel, and boot-protocol vs report-protocol parsing. IntelliMouse (3-button + wheel) is auto-detected | boards with USB host |
| 14 | **USB hub** | downstream port enumeration with both a keyboard and a mouse attached at once | boards with a hub |
| 15 | **PIO-USB** | soft host on GP20/21 (`zerofrank`) or GP2–5 (`hecate`) | zero, hecate |
| 16 | **USB mux** | verify the path the switch currently selects, and name the switch. Not firmware-selectable — see §4.1 | micro, mini, core2u, next, pga, mega, oldskool |
| 17 | **PS/2 keyboard** | TXS0104 level-shifter direction, idle-high check, reset (`0xFF`→`0xFA`,`0xAA`), scan-code set, typematic, LED command | frank, pga, mega, mini, oldskool, hecate |
| 18 | **PS/2 mouse** | reset and ID (`0x00` plain, `0x03` wheel, `0x04` 5-button), sample-rate knock sequence for IntelliMouse, stream-mode packets, X/Y sign bits, overflow flags, all buttons | frank, pga, mega, mini, oldskool, hecate |
| 17 | **Gamepad** | NES/SNES latch+clock shift, DB9 Atari, idle-state sanity | frank, pga, mega, mini, oldskool, next |
| 18 | **Tape in** | CD4069 comparator toggling; audio-out→tape-in loopback where the connectors allow | frank, pga, mega, mini, oldskool, core2u, next |
| 19 | **RTC** | DS3231 read/write, oscillator running (two reads 1 s apart), temperature register, battery retention across a power cycle | mega, next |
| 20 | **1-Wire ID** | DS2401 ROM read + CRC-8, report the unit serial | mega, next |
| 21 | **ESP** | reset, ROM banner, firmware version; RP↔ESP32 SPI link throughput | mini, pga, mega, oldskool, next |
| 22 | **Inter-MCU link** | the existing 4-rate duplex sweep, integrity pass, round-trip latency | core2, core2u |
| 23 | **LED / heartbeat** | WS2812 colour states or plain LED, driven from a timer IRQ so it keeps beating while the foreground is parked | all |
| 24 | **Buttons / DIP** | reset, BOOTSEL, config switches | boards that have them |
| 25 | **Power** | VREG setting, 3V3 rail via ADC where a pin is free | all |
| 26 | **GPIO short scan** | probe every adjacent GPIO pair for an unwanted connection — a solder-bridge and assembly-defect detector. Borrowed from [murmulator-tester](https://github.com/DnCraptor/murmulator-tester), which blinks the offending pin number; here it names the pair on screen | all |
| 27 | **Pointer** | the cursor tracks a USB or PS/2 mouse across the whole screen, buttons select and menus open. Proves the input path end to end rather than by report decode alone | boards with a mouse |

### 4.1 What firmware cannot drive

The netlist extraction turned up something the board diagrams do not
make obvious, and it changes what several of these tests can claim:

**Every audio mux, USB mux and amplifier-shutdown line in the fleet is
driven by a physical switch or jumper, not by a GPIO.** The sole
exception is `frank_next`'s ESP UART mux on GP30.

| Board | Control | Sets |
|---|---|---|
| `frank` | JP1 | pin1 audio mux, pin4 PAM8403 shutdown, pin6-7 tape onto GP22 |
| `frank_pga` | JP1, S1 | audio mux + amp shutdown; S1 USB mux |
| `megafrank` | S1, S9, S10 | S1 1/2 audio mux (TDA vs TurboSound), 6 amp, 9-3 tape onto GP22; S9 USB mux; S10 PSRAM enable |
| `minifrank` | S2, S1 | S2 pin2 audio mux, pins 1-4 tape onto GP22; S1 USB mux |
| `microfrank`, `oldskoolfrank`, `core2u` | S1 / S2 / S5 | USB mux |

So tests 10 and 16 cannot switch a path and compare. They can only
verify whichever path the switch currently selects, and then **name the
switch the operator has to move** to test the other one. The report
carries that text per board (`manual_note` in the descriptor table).

Stating this plainly matters more than it looks: a test that silently
verifies one of two paths and reports "audio mux OK" is claiming
coverage it does not have.

Likewise **ESP-01S CH_PD and RST go to buttons on every board that
carries one.** Only `frank_next` can reset its companion processor from
firmware.

Throughput and integrity stay measured **separately**, as they already
are in the core2u link test: throughput is one uninterrupted DMA with no
verification (the number is the wire and nothing else), integrity is
block-at-a-time with every byte compared. Mixing them either understates
the speed or overstates the confidence.

Structural failures report as **`no-run`**, not as a zero or a small
error count. Conflating "the exchange never completed" with "two bytes
were wrong" sends you hunting for signal-integrity problems that are not
there — a lesson already paid for in the core2u bring-up.

---

## 5. Interface

Not a console. A test rig that prints a wall of log lines makes the
reader do the work of finding the one thing that failed; a list of
subsystems, each with an icon and a state, does not. The state of a
board is exactly the kind of thing a screen full of small repeated
elements shows better than prose.

The visual language is the classic Macintosh one — menu bar, pull-downs
with keyboard equivalents, framed windows with striped drag regions and
hard drop shadows, selection by inversion, greys made of ordered dither.
That is not nostalgia for its own sake: it is the design language that
was worked out for exactly this problem, a small monochrome screen that
has to stay legible at a glance.

### 5.1 The framebuffer: 640x480 at 4 bpp

153,600 bytes — 30% of an RP2350's 520 KB, leaving 366 KB for everything
else.

This was 2 bpp, sized to fit an RP2040's 264 KB. §1.4 established that
constraint was self-imposed: nothing in the fleet that needs a
framebuffer is an RP2040. Four bits buys a proper grey ramp, which is
what makes bevels possible — a raised edge needs a highlight above the
face colour and two steps below it, and at four colours that has to be
faked with dither. Sixteen entries covers that, an accent, and genuine
pass / fail / warn colours, without the 307,200 bytes 8 bpp would cost.

The palette lives in `ui/ui_palette.h` and is included by both the
display backends and the host preview, so the two cannot drift apart.

| Index | Role | Used for |
|---|---|---|
| 0 | white | bevel highlights, field interiors |
| 1 | paper | window and menu backgrounds |
| 2–6 | grey ramp | control faces, bevel shadows, secondary text |
| 7 | ink | frames, text |
| 8–9 | accent | selection, progress |
| 10–11 | pass | the tick |
| 12–13 | fail | the cross, and failed measurements |
| 14 | warn | could-not-run, and manual steps |
| 15 | desktop | the backdrop |

### 5.2 What is on screen

- **Menu bar** — a mark, then File, Board, Video, Tests, Window. Every
  command lives in a menu and every item has a keyboard equivalent.
  That is a requirement, not a nicety: `nyx` has no USB host and no PS/2
  connector, so on some boards the only input is the UART, and a menu
  reachable only by clicking would be unusable exactly where the
  hardware is thinnest.
- **Test Results** — the main window. One row per test: icon, name, the
  measured value, and a state glyph. Scrollable, selectable, and the
  running test shows a thermometer in place of its value, because "37%"
  and "31.8 MiB/s" mean different things and should not look alike.
- **Board** — what was detected, the unit serial, and the pass / fail /
  n/a tallies.
- **Manual Steps** — the switches this firmware cannot reach (§4.1),
  named per board. Present on screen rather than buried in a log,
  because a rig that silently tests one half of a switched path is
  claiming coverage it does not have.
- **Dialogs** — modal, with a default button ringed the classic way. The
  board-ambiguity question (§2.5) is one of these rather than a console
  prompt.

State glyphs are deliberately distinct: a green tick for pass, a red
cross for fail, a **grey dash** for n/a and an **amber query** for
could-not-run. "This board has no PS/2" must not look like "the PS/2 is
broken", so n/a never uses the fail colour — and could-not-run gets its
own colour again, because it usually means the operator has a switch to
flip (§4.1).

### 5.3 The scanline expander

The framebuffer is 4 bpp; HSTX wants RGB565 pairs packed two to a word.
That is a happy alignment — one source byte is exactly one output word —
so `ui/ui_expand4bpp.h` is a single 256-entry table lookup per byte, no
shifts, no masking, no branches. A row is 320 lookups inside a ~25 us
active line. The table costs 1 KB of RAM.

The destination packing was read off `video_output.c:89`
(`static uint16_t line_buffer[MODE_H_ACTIVE_PIXELS]`, so the low
half-word is the left pixel) rather than assumed, because this is the one
piece of the video path whose bugs are silent: a transposed pixel pair
does not crash, it produces an image that looks almost right — fine
vertical detail combed, large shapes correct.

So it is header-only and SDK-free, and `ui/hostpreview/expand_test.c`
runs *that* code over a real composed frame, decodes the words back to
palette indices and compares against the framebuffer they came from. All
307,200 pixels round-trip. Compiling the same test with
`-DUI_HSTX_SWAP_PAIR=1` fails on row 4 with 81,152 pixels wrong, which is
the check that the check works.

It is also why `ui_video_hstx.c` is a new file rather than an edit to
`drivers/HDMI_hstx.c`: that driver is copied verbatim from frank-msx so
the two projects can re-sync, and a second pixel format inside it would
end that.

### 5.3a What hardware found

Both of these reported success and produced nothing. Recorded because
the failure mode is the same in each case — a log line saying the video
backend is up, and a sink showing its no-input pattern.

**1. `clk_hstx` must be exactly 126 MHz.** TMDS is 10 bits per pixel and
HSTX emits 2 bits per lane per clock, so 640x480@60 needs five HSTX
clocks per pixel at a 25.2 MHz pixel rate. `video_output_init()` derives
it as `clk_sys / MODE_HSTX_CLK_DIV`, which only lands on 126 MHz for
particular pairings — 126/1 or 252/2. The self-test ran at the SDK's
default 150 MHz with DIV=1 and got 150 MHz, which no sink can lock to.
`ui_video_hstx.c` now sets `clk_hstx` explicitly after init, so the video
path no longer depends on what clock the rest of the firmware chose.

**2. `video_output_init()` does not start the scanout.** It configures
HSTX and claims the DMA channels; nothing leaves the connector until
`video_output_core1_run()` is launched on core 1. `drivers/HDMI_hstx.c`
does this at the end of its `graphics_init()`, and the new backend had
skipped it along with `hstx_di_queue_init()`.

**3. A measured constant for 3.2.** With the HDMI capture device
attached, `video_test_pins(12, 13)` returns **0x1E** — both pins read
high under both internal pulls, i.e. externally pulled up. That is the
50R-to-AVCC TMDS termination the section predicted, observed. It is the
first real calibration point for those thresholds; the VGA and
composite cases still need measuring.

### 5.3b Bugs the board found that the host could not

Four, all of the same shape — code that looked right, built clean, and
was wrong only in the presence of real hardware.

**Probing a pin steals it.** `gpio_init()` routes a pad to SIO and
`gpio_deinit()` leaves it unassigned. GP0/GP1 are the console UART on
almost every board, and they are also what separates `frank_pga` from
`oldskoolfrank`, so the fingerprint probed them and silently disconnected
the console. The log stopped mid-word and the firmware looked hung while
it was in fact finishing normally. Both `pinsig.c` and the borrowed
`video_test_pins()` now save the pad function and put it back; the
adjacent-pin scan additionally skips the console pins outright, since a
33 ms low pulse mangles whatever is being transmitted even when the pad
is restored afterwards.

**Absolute match counts are not comparable across signatures.** `nyx`
matching 6 of its 7 pins scored the same as `core2` matching 6 of 6, so
a board with an obvious 10K pull-up on GP43 tied with one that expects
that pin to float. Scoring is now a ratio, which makes a single mismatch
decisive — correct here, because these are pull-ups that either exist or
do not, not noisy measurements.

**The candidate list was the wrong set.** It held everything Tier 1
failed to rule out, not the boards that actually tied, so the dialog
offered `nyx` as option 1 on a Core 2. Offering a board the evidence has
already ruled out is a good way to have it picked — which is exactly what
happened.

**A tie-break is not a disagreement.** `core2` and `core2u` share a
signature exactly, so `auto_guess` is whichever the table lists first.
Warning that the declared board "does not look like" an arbitrary
tie-break trains the operator to ignore the one warning that catches a
genuinely wrong flash. Suppressed when the fingerprint was ambiguous and
the declared board is among the boards that tied.

### 5.3c Video scanout costs three quarters of the memory bandwidth

Measured on a Core 2 master, same 1 MB XIP read, same build:

| | flash read | PSRAM write | PSRAM read |
|---|---|---|---|
| core 1 parked | **33.7 MiB/s** | **13.4 MiB/s** | **32.9 MiB/s** |
| HSTX scanout running | 7.8 MiB/s | 3.4 MiB/s | 7.8 MiB/s |

The scanout loop runs out of flash, and its instruction fetches contend
with core 0's XIP reads through the single QMI. Roughly 4x, consistently.

This matters more than it first appears: 7.8 MiB/s printed next to a
part number reads as a failing flash. The memory tests therefore run
during boot, while core 1 is still parked, and the test rows report those
stored figures rather than re-measuring — `detect_benchmark()`. The
numbers now agree with the ones frank_core2u's README documents for the
same hardware, which is the check that they are the real ones.

It was found by controlled experiment rather than reasoning: the first
suspect was the USB HID host, and building the same firmware with
`-DUI_INPUT_USB_HID=OFF` produced identical slow figures, which ruled it
out in one step.

### 5.3d Partial repaint

There is no back buffer. The scanline renderer reads the framebuffer in
place, and a second 153,600-byte buffer is not free, so a full
recomposition rewrites pixels the beam is currently displaying —
arrowing through a menu made the whole screen shimmer.

The fix is to repaint only the union of where the menu was and where it
now is. That needed one thing from the graphics layer: `ui_clip_reset()`
now means "back to the repaint region" when one is in force, rather than
"back to the whole surface". Window and menu drawing legitimately reset
the clip partway through — a window frame is drawn outside the content
clip it then sets — so a partial repaint has to survive that without
every call site knowing about it.

Verified rather than assumed: eleven consecutive captured frames during
rapid menu navigation are **pixel-identical below y=200**, zero differing
pixels. Nothing outside the menu rectangle is touched.

### 4.2 What the audio tests can prove

No loopback and no ADC exist anywhere in the fleet, so past the pins
nothing is measurable. Each test states how far its evidence reaches,
because "audio ok" on a board with an unfitted DAC is the kind of result
that gets a whole rig distrusted.

| Test | Proves | Does not prove |
|---|---|---|
| I2S | the PIO FIFO drains, so SCLK and LRCK are clocking | that the TDA1387 converts, the analogue path works, or that the DAC is fitted |
| PWM | the slice counter advances, so the peripheral runs | anything past the pin |
| TurboSound | the firmware drove the 595 chain in the right order | anything at all — the chain is write-only, with no return path from the AY |

All three play **distinct pitches per channel** rather than one mono
tone. A single tone cannot tell a working stereo path from a stuck LRCK
or a dead channel: all three sound the same, which is to say they all
sound like success.

The 74HC595 word format comes from SpeccyP's `aySoft.h`, which drives the
same arrangement — `* R * B 1 0 W A` then a data byte, reset and both
chip selects active low. Writing one AY register is two words: latch the
address with A0 high, then the value with A0 low. Bit-banged rather than
driven from PIO, because a register write here has no deadline and both
PIO blocks are already spoken for by the link and I2S.

PWM audio turned out to be the **same GP9/10/11 pins** as I2S, through RC
filters, with the 74HC4052 choosing which analogue path reaches the
amplifier. That mux is a switch, not a GPIO, so the test cannot select
itself — if nothing is heard, the audio switch is the first thing to
check.

### 4.3 First MegaFRANK run

The second board the firmware met, and the first with hardware nothing
had exercised. 13 passed, 3 failed.

Detection named it outright with no ambiguity, as predicted: it is the
only board with a DS2401 *and* a DS3231, so Tier 1's positive
identifications settle it before the fingerprint is consulted.

New hardware that worked first time: **DS3231 at 0x68**, **DS2401 serial
000004F89B8E** — the first genuine per-unit identity read anywhere in the
fleet — **PWM audio** on GP9/10, and **TurboSound**, 260 registers across
both AYs without error.

Two of the three failures were the rig's, not the board's:

- **`GPIO short scan: GP9-GP10`.** True and useless. On megafrank those
  pins carry I2S, the TurboSound shift register and the PWM audio path,
  and the analogue network between them ties them together by design.
  The scan now skips audio pins for the same reason it already skipped
  the video and link pins — a deliberate connection is not a defect.
- **`SPI PSRAM sweep: 2040 byte errors in 128 blocks`.** Noise: the probe
  had already reported nothing responding, and sweeping a part that never
  identified itself buries the one line that matters under a wall of
  errors. The sweep now refuses to run unless the probe found something.

The third is genuine and unresolved: **nothing answered the SPI PSRAM ID
command**. That is either S10 being off or the SIO turnaround in
`tests_psram_spi.c` being wrong, and the firmware cannot tell which. The
failure names the switch, because sending someone to debug firmware when
a switch is off costs more than the opposite mistake.

### 4.4 MegaFRANK: three things the netlist settled

All three of the first run's failures came back to reading the schematic
more carefully, not to the board.

**The audio mux is a 4:1 selector, not two enables.** U21's two select
lines both come from switch S1, and the combination picks the source:

| S1-1 (SW_TDA) | S1-2 (SW_TS) | Reaches the amplifier |
|---|---|---|
| 0 | 0 | PWM |
| 1 | 0 | TDA / I2S |
| 0 | 1 | TurboSound |
| 1 | 1 | **ground — silence** |

So "turn TurboSound on" while I2S is also on selects *ground*. That is
the likely explanation for hearing I2S and silence from TurboSound on a
board whose TurboSound switch is on, and no amount of firmware fixes it.
The descriptor's `manual_note` now spells the table out rather than
saying "1/2 select the audio mux", which was true and unusable.

**The SPI PSRAM is four-wire.** Confirmed: with the corrected pin map it
identifies as ESP-PSRAM64, manufacturer 0x0D, and sweeps 8 MB clean.
S10 had been closed the whole time — the half-duplex assumption was the
entire fault.

 `SO/SIO` does not come back on the same
pin as `SI` — it has its own, and reaches the MCU only through switch
S10 to GP34. The first implementation read the answer on the MOSI pin, on
a half-duplex assumption taken from a partial reading of the netlist, and
could not have worked. SpeccyP's `drivers/psram` confirms the model:
separate `PSRAM_PIN_CS / SCK / MOSI / MISO`.

S10 therefore gates the *read* path specifically: with it open the
commands still go out and nothing comes back, which is indistinguishable
from an absent part. The failure text now names the pin and the switch.

**GP9-GP10 is not a short.** Those pins carry I2S, the TurboSound shift
register and the PWM path, and the analogue network between them ties
them together by design.

### 5.4 The host preview

`ui/hostpreview/` compiles the entire interface for the host and renders
it to an image:

```bash
cc -o preview preview.c ../ui_*.c ../../master/src/ui_font.c
./preview desktop.ppm
```

The UI layer has no SDK dependency, which is what makes this possible
and is worth preserving. Designing a pixel interface by flashing a board
and squinting at a monitor is slow enough that you stop iterating, and
it shows in the result. This builds in under a second.

The preview's palette must match the one the display driver installs, or
it is a nice picture of a different program.

### 5.5 Input

With USB CDC gone (§3.4), the pointer and the keyboard are real
hardware, which is also why they need real tests:

Every menu command has a keyboard equivalent, and the menu bar itself is
reachable by Alt+letter — `F`ile, `B`oard, `V`ideo, `T`ests, `W`indow,
and Alt+A for the mark. Arrows navigate, Enter activates, Esc closes.
That is a requirement rather than a nicety: on `nyx` and the core2 halves
there is no pointing device at all, so a menu bar that can only be opened
by clicking would be decoration.

Over a terminal, Alt+X arrives as ESC followed by X and an arrow as
ESC `[` A..D — both begin with ESC, and a bare ESC means "close the
menu". `ui_input.c` tells them apart by what follows and how soon.

| Device | Boards | Notes |
|---|---|---|
| USB HID keyboard + mouse | anything with USB host | TinyUSB host; the mouse drives the arrow cursor |
| PS/2 keyboard + mouse | frank, pga, mega, mini, oldskool, hecate | IntelliMouse auto-detected via the sample-rate knock |
| UART | all | full keyboard equivalents, no cursor |
| Gamepad | boards with a pad | menu navigation only |

Boards with no pointing device at all (`nyx`, `microfrank`,
`zerofrank`, the core2 halves) are driven entirely from menu keyboard
equivalents over UART. The interface has to be complete without a mouse.

### 5.6 Still to do

- A Chicago-like 8x13 face. The current 6x8 is crisp and period-correct
  but small; the layout is built so a taller font drops in.
- Save-under for the cursor and for pull-downs, so a redraw is not a
  whole-screen recomposition.
- Backends beyond HSTX HDMI. PIO HDMI, VGA and composite still need
  porting into the vtable; until they exist, `ui_video_open()` on an
  RP2040 correctly finds nothing rather than pretending.


## 6. Architecture

```
test_firmware/
├── boards/                 SDK board headers, one per board+role
├── core/
│   ├── board_table.c/.h    the 13 descriptors: pin map, capability
│   │                       bitmask, expected fingerprint, don't-care mask
│   ├── detect.c/.h         tiers 0–3, verdict + margin + disagreements
│   ├── frankid.c/.h        the flash identity record
│   ├── video_detect.c/.h   §3.2 — both detectors
│   ├── video_backend.c/.h  §3.5 vtable: hstx_hdmi / pio_hdmi / pio_vga /
│   │                       disphstx_vga / composite
│   ├── video_select.c/.h   §3.4 boot window, key sources, sticky choice
│   ├── settings.c/.h       persisted record: board id, video choice, CRC32
│   ├── registry.c/.h       test registration, capability gating, n/a vs fail
│   ├── report.c/.h         backend-agnostic renderer: screen + console
│   └── console.c/.h        commands incl. `board set`, `video <mode>`
├── ui/
│   ├── ui_gfx.c/.h         2 bpp primitives, patterns, text — no SDK deps
│   ├── ui_icons.c/.h       16x16 icons, authored as ASCII art
│   ├── ui_window.c/.h      frames, buttons, checkboxes, scrollbars
│   ├── ui_menu.c/.h        menu bar and pull-downs
│   ├── ui_desktop.c/.h     the composed screen
│   └── hostpreview/        renders the whole interface to a PPM on the host
├── tests/                  one file per row of §4
├── common/                 link bus, protocol, session, mem_test  (from core2u)
├── drivers/                HDMI, SD, FatFs, audio, PSRAM, USB HID  (from frank-msx)
├── targets/
│   ├── rp2350a/            frank, micro, mini, zero, core2 slave
│   └── rp2350b/            pga, mega, oldskool, nyx, next, core2 master
└── probe/                  the ~20-line bring-up bisect target
```

Two binaries, one per MCU class (§1.4). Each carries the descriptors
for every board in its class and picks one at boot.

Consequences to design around, stated plainly because they are the cost
of the universal-binary choice:

- **A wrong guess drives real pins.** Detection must be conservative:
  probe passively before driving anything, and never drive a pin whose
  function is still ambiguous. Where the fingerprint is uncertain, stop
  and ask rather than proceed.
- **Binary size.** All three classes carry HDMI + SD + FatFs + USB host
  + audio. Budget for it; if flash gets tight, the RP2040 target is the
  one to trim first — `hecate` needs almost none of it.
- **`frank`'s socket** is a Pico 2 by policy, not by the connector. If a
  Pico 1 is ever fitted, that board has no firmware — which is a louder
  and more debuggable failure than a subtly wrong one.

`common/board_config.h` stays a shim feeding the FRANK pin map to the
frank-msx drivers under the names they expect, so `drivers/` can keep
being re-synced from frank-msx without patching.

---

## 7. Phases

| Phase | Work | Done when |
|---|---|---|
| **0** | Merge copy into `test_firmware/` | ✅ done — 89 files, no build artefacts |
| **1** | De-core2u-ify: rename symbols, extract the board descriptor struct, add the capability bitmask, stand up the three CMake targets, get all three linking with a single hard-coded board | `./build_all.sh` emits 3 `.uf2`; core2u behaviour unchanged on real hardware |
| **2** | Descriptors for all 13 boards; `docs/pinouts.md` generated from the netlists so it cannot drift | every board has a descriptor; a generator script regenerates the doc |
| **3** | Detection: tiers 0–3, `FRANKID` record, `board` console command | every board identifies correctly on the bench; `{core2, core2u}` correctly reports as ambiguous rather than guessing |
| **4** | Test registry + capability gating + `n/a`/`fail`/`no-run` reporting | the existing core2u tests run through the registry |
| **5a** | Port `DispHstxAutoDispSel()` and `testPins()` from frank-msx; video backend vtable; all backends the board supports linked in | correct verdict for none / HDMI / VGA on a shared-pin board |
| **5b** | Boot window + key sources (DIP, PS/2, UART, USB) + sticky setting (§3.4) | H/V/C/A works blind on a board whose autodetect guessed wrong |
| **5c** | Runtime switching: live within a family, persist-and-`watchdog_reboot()` across families (§3.5) | `video composite` from the Video menu lands in composite |
| **5d** | ✅ UI layer (§5): 2 bpp primitives, icons, windows, menus, desktop composition, host preview | done — `ui/hostpreview` renders the screen; wiring it to a real backend is 5e |
| **5e** | ✅ 2 bpp expander + HSTX HDMI backend behind the vtable | done — expander verified on the host, all three MCU classes link |
| **5f** | ✅ HSTX HDMI on real hardware | done — desktop captured off the HDMI output of a Core 2 master, 640x480 |
| **5g** | VGA / composite backends into the vtable; cursor plumbed to USB and PS/2 | the cursor moves, and VGA comes up on a board that has the connector |
| **6a** | ✅ Test registry, capability gating, and the first ten tests | done — running on a Core 2 master: 10 passed, 0 failed, 2 n/a |
| **6b** | ✅ Full keyboard control and pointer plumbing | done — Alt+F/B/V/T/W open menus, arrows navigate, Enter activates, Esc closes; USB HID host drives the cursor |
| **6c** | ✅ Link (full sweep + escalating recovery + FS reset), audio (I2S/PWM/TurboSound) | done — 12 passed, 0 failed on a Core 2 master; link at 96.1 MiB/s duplex |
| **6d** | ✅ MegaFRANK bring-up | done — detection named it outright; RTC, DS2401 serial, PWM and TurboSound all ran for the first time |
| **6e** | SD, PS/2 and USB HID *tests* | the rest of §4. MegaFRANK now runs 16 passed, 0 failed |
| **6** | Per-peripheral tests in dependency order: flash, PSRAM → video → SD → audio → USB → HID/gamepad/PS2 → RTC/1-Wire → ESP → link | §4 implemented |
| **7** | Bring-up board by board, recording measured baselines into `baselines/<board>.txt` | a golden run captured per board; regressions become visible |
| **8** | Docs: one README per board section, the hardware recommendations from §2.7 and §3.5 raised as schematic changes | — |

Bring-up order for phase 7, easiest first so the harness is trustworthy
before it meets the hard boards: `nyx` (nothing to get wrong) →
`microfrank` → `zerofrank` → `minifrank` → `frank_core2u` (already known
good) → `frank_core2` → `hecate` → `frank` → `frank_pga` →
`oldskoolfrank` → `megafrank` → `frank_next`.

---

## 8. Provenance

Everything in §1 came from the KiCad schematics, not from documentation:

```bash
kicad-cli sch export netlist --format kicadsexpr -o <board>.net <board>.kicad_sch
```

then a parser that walks `(nets → net → node)` and joins `pinfunction`
to net name, so each GPIO row is the actual net plus every other
component pin sitting on it. The scripts that did this
(`parse.py`, `gpio2.py`) should move into `test_firmware/tools/` in
phase 2 and become the generator for `docs/pinouts.md` — a pinout table
that regenerates from the schematic cannot go stale, and a hand-written
one will.

---

## 9. Open risks

1. **RP2350-E9.** The pull-down erratum threatens the passive
   fingerprint. Mitigated in §2.4 by pull-up + RC sensing, but it needs
   confirming against the current datasheet and re-testing on both A2
   and A4 silicon. This is the highest-risk assumption here.
2. **Composite is undetectable** (§3.2c) — it drives the same GPIO12–19
   DAC pins and presents the same 75 Ω-to-ground signature as VGA. The
   manual override is load-bearing, not a nicety. Reduced risk on the
   VGA-vs-HDMI half: both detectors are lifted from frank-msx where they
   are already proven, and the RGB pull-up probe uses no pull-down, so it
   is not exposed to risk 1.
3. **Runtime backend switching is new work.** frank-msx picks its video
   backend at compile time and the four drivers fight over core 1,
   `DMA_IRQ_0` and the same eight pins. The persist-and-reboot fallback
   in §3.5 exists so this cannot become a schedule risk — but linking
   every backend into one image will cost flash, most painfully on the
   RP2040 target.
4. **The boot window costs boot time on every board** (§3.4), and the
   fastest input source varies per board — DIP switches are instant, USB
   HID may not enumerate before the window closes. The sticky setting is
   what makes this acceptable; if it does not work reliably, the window
   becomes a permanent tax.
5. **One test can break another.** The adjacent-pin short scan drives
   pins for tens of milliseconds, and it was scanning across the link's
   control lines — FS and both doorbells. FS held high is how the master
   asks the slave to reboot, so the scan reset the slave and left the
   handshake out of step; the link test ran straight afterwards and
   failed every HELLO. It read as an intermittent link for several runs.

   Found by making the handshake report *which* step failed rather than
   "no response": send succeeded, reply never came, which pointed at one
   direction of the bus and from there at what had just been driving it.
   Any test that drives pins now has to declare what it must not touch.

6. **`frank_core2` vs `frank_core2u`** cannot be resolved
   electrically (§2.5). Declared identity only, until a board ID strap
   exists.
6. **`frank`'s socketed CPU** means the board spans two binaries. If a
   Pico 1 and a Pico 2 behave differently enough elsewhere, `frank` may
   need to be two descriptors rather than one.
8. **`oldskoolfrank`'s solder-jumper video select** limits detection to
   what is fitted. Report the limitation rather than papering over it.
8. **Binary size** on the RP2040 target — `hecate` needs almost none of
   the payload the shared core carries, and §3.5 adds to it.
