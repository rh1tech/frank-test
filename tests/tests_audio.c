/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_audio.c — I2S, PWM and TurboSound.
 *
 *
 * WHY THESE ARE NOT TESTS
 *
 * They were, and it was dishonest. There is no loopback and no ADC
 * anywhere in the fleet, so past the pins nothing is measurable:
 *
 *   I2S         the PIO FIFO draining proves SCLK and LRCK are clocking.
 *               It says nothing about the TDA1387 converting, the analogue
 *               path, or whether the DAC is fitted at all.
 *   PWM         the slice counter advancing proves the peripheral runs.
 *               Same caveat past the pin.
 *   TurboSound  the 74HC595 chain is write-only — there is no path back
 *               from the AY — so this can drive it and nothing more.
 *
 * On megafrank all three also share GP9-GP11 behind a 74HC4052 the
 * firmware cannot select, so which one is audible depends on two switches
 * on the board. A PASS in a results list, arrived at without hearing
 * anything, is a claim the firmware is in no position to make — and it
 * ran once and stopped, which is precisely wrong for something whose
 * whole purpose is to be listened to with the switches in three
 * positions.
 *
 * So the rows are gone and what remains is a driver: play one melody
 * through one channel, tell me if the hardware stalled, and let the
 * interface loop it for as long as the operator wants. See frank_audio.h.
 *
 *
 * THE MELODY
 *
 * Metallica, "Enter Sandman" — the opening figure, transposed up so it
 * survives a square wave and a small speaker.
 *
 * It is built on E and the flat fifth above it, which is what makes it
 * useful here as well as recognisable: a tritone is unmistakable, so a
 * channel running at the wrong rate or dropping notes gives itself away
 * by ear without anyone needing to know the tune. A single tone would
 * not: a stuck LRCK, a dead channel and a working one all sound alike.
 */

#include "attest.h"
#include "frank_audio.h"
#include "registry.h"

#include "audio_i2s.pio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define SAMPLE_RATE 44100u

typedef struct { uint16_t hz; uint16_t ms; } note_t;

#define NOTE_E4  330
#define NOTE_G4  392
#define NOTE_Bb4 466
#define NOTE_B4  494
#define REST     0

static const note_t melody[] = {
    { NOTE_E4, 150 }, { NOTE_E4, 150 }, { NOTE_G4, 150 }, { NOTE_E4, 150 },
    { NOTE_Bb4,150 }, { REST,     60 },
    { NOTE_E4, 150 }, { NOTE_E4, 150 }, { NOTE_G4, 150 }, { NOTE_E4, 150 },
    { NOTE_B4, 300 }, { REST,    120 },
};
#define MELODY_LEN (sizeof(melody) / sizeof(melody[0]))

/* Which speaker each pass drives. Named rather than numbered because the
 * name is what goes on screen while it plays: three tones after the fact
 * leaves the operator working out which one they just heard. */
static const struct { const char *label; bool left, right; } channels[] = {
    { "Left",   true,  false },
    { "Right",  false, true  },
    { "Centre", true,  true  },
};

const char *audio_channel_name(int ch) {
    if (ch < 0 || ch >= AUDIO_CHANNELS) return "?";
    return channels[ch].label;
}

const char *audio_src_name(audio_src_t s) {
    switch (s) {
        case AUDIO_SRC_PWM: return "PWM";
        case AUDIO_SRC_I2S: return "TDA (I2S)";
        case AUDIO_SRC_TS:  return "TurboSound";
        default:            return "?";
    }
}

uint32_t audio_src_cap(audio_src_t s) {
    switch (s) {
        case AUDIO_SRC_PWM: return CAP_AUDIO_MUX;
        case AUDIO_SRC_I2S: return CAP_AUDIO_I2S;
        case AUDIO_SRC_TS:  return CAP_TURBOSOUND;
        default:            return 0;
    }
}

bool audio_src_available(const detect_result_t *d, audio_src_t s) {
    if (!d || !d->board || d->board->id == FRANK_BOARD_UNKNOWN) return false;
    return (d->board->caps & audio_src_cap(s)) != 0;
}

/* The mux is the whole reason this exists.
 *
 * megafrank's 74HC4052 is a 4:1 selector driven by two switches, not two
 * enables — turning both on selects ground, which is silence, and looks
 * exactly like a dead amplifier. Any board that has the mux gets told
 * which way to set it; boards whose audio is hard-wired get nothing,
 * because inventing switches they do not have is worse than saying
 * nothing at all. */
const char *audio_src_switch_hint(const detect_result_t *d, audio_src_t s) {
    if (!d || !d->board || !(d->board->caps & CAP_AUDIO_MUX)) return NULL;
    switch (s) {
        case AUDIO_SRC_PWM: return "Audio mux: S1-1 off, S1-2 off";
        case AUDIO_SRC_I2S: return "Audio mux: S1-1 on,  S1-2 off";
        case AUDIO_SRC_TS:  return "Audio mux: S1-1 off, S1-2 on";
        default:            return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* I2S                                                                 */
/* ------------------------------------------------------------------ */

/* pio0 belongs to the inter-processor link and pio2 to the NES pads, so
 * I2S lives on pio1.
 *
 * Driven straight from the FIFO with no DMA and no interrupt, which is
 * not laziness: audio.c's i2s_init() installs an *exclusive* DMA_IRQ_0
 * handler, and the HSTX video driver has already taken DMA_IRQ_0. The
 * second irq_set_exclusive_handler() hard-asserts, panic() executes a
 * breakpoint with no debugger attached, and the core escalates into
 * lockup — report half-drawn, no message, no way back into BOOTSEL. A
 * tone needs neither DMA nor an interrupt, so it takes neither. */
static PIO  s_pio;
static uint s_sm;
static bool s_i2s_up;

/* Re-assert the pads and restart the state machine.
 *
 * Called at the top of every pass, not just the first. The TurboSound
 * path takes GP9/10/11 outright on the boards that have one, and the
 * GPIO short scan drives them as ordinary pins. Both hand the pads back,
 * but the state machine has been feeding a dead pin in the meantime and
 * its FIFO is full, so the next run pushed samples into a queue that
 * never drained and played nothing.
 *
 * That was the "sound only plays once" bug: the first run set everything
 * up and worked, every run after it was silent. */
static void i2s_reassert(const frank_pins_t *p) {
    const uint pins[] = { (uint)p->i2s_data,
                          (uint)p->i2s_clk_base,
                          (uint)p->i2s_clk_base + 1 };

    pio_sm_set_enabled(s_pio, s_sm, false);
    pio_sm_clear_fifos(s_pio, s_sm);

    for (unsigned i = 0; i < count_of(pins); i++) {
        pio_gpio_init(s_pio, pins[i]);
        gpio_set_drive_strength(pins[i], GPIO_DRIVE_STRENGTH_12MA);
    }

    pio_sm_restart(s_pio, s_sm);
    pio_sm_set_enabled(s_pio, s_sm, true);
}

static bool i2s_bring_up(const frank_pins_t *p) {
    if (s_i2s_up) { i2s_reassert(p); return true; }
    if (p->i2s_data == PIN_NC || p->i2s_clk_base == PIN_NC) return false;

    static bool s_claimed;
    static uint s_off;
    if (!s_claimed) {
        s_pio = pio1;
        int sm = pio_claim_unused_sm(s_pio, false);
        if (sm < 0) return false;
        s_sm  = (uint)sm;
        s_off = pio_add_program(s_pio, &audio_i2s_program);
        s_claimed = true;
    }

    /* Route the pads to the PIO.
     *
     * audio_i2s_program_init() configures the state machine but does NOT
     * call pio_gpio_init() — the frank-msx driver did that separately
     * inside i2s_init(). Dropping these calls is why, once, the FIFO
     * drained happily, the probe reported ok, and absolutely nothing
     * reached the DAC: the pads never left SIO mode. */
    const uint pins[] = { (uint)p->i2s_data,
                          (uint)p->i2s_clk_base,
                          (uint)p->i2s_clk_base + 1 };
    for (unsigned i = 0; i < count_of(pins); i++) {
        pio_gpio_init(s_pio, pins[i]);
        gpio_set_drive_strength(pins[i], GPIO_DRIVE_STRENGTH_12MA);
    }

    /* The program only ever gets added once — re-adding it on every
     * source switch would exhaust the 32-instruction store after a
     * handful of trips through the Audio menu. */
    audio_i2s_program_init(s_pio, s_sm, s_off,
                           (uint)p->i2s_data, (uint)p->i2s_clk_base);

    /* 32 bits per stereo frame x 2 PIO cycles per bit, in 8.8 fixed
     * point — the same arithmetic the driver does. */
    uint32_t div = clock_get_hz(clk_sys) * 4u / SAMPLE_RATE;
    pio_sm_set_clkdiv_int_frac(s_pio, s_sm, div >> 8u, div & 0xFFu);
    pio_sm_set_enabled(s_pio, s_sm, true);

    s_i2s_up = true;
    return true;
}

/* How often the interface gets a look-in while a note is playing.
 *
 * 512 frames is 11.6 ms at 44.1 kHz, so the pointer updates about eighty
 * times a second instead of once per note. The tick has to be cheap:
 * the TX FIFO holds eight samples, 180 us, and anything slower than that
 * leaves the DAC holding its last level. Moving the cursor overlay is
 * microseconds — see ui_cursor.c — which is what makes this safe at all.
 * It was not, back when a pointer move meant recomposing the screen. */
#define I2S_TICK_FRAMES 512u

/* Returns false if the FIFO stops draining — which is the one thing this
 * path can actually detect: no bit clock — or if the tick asked to stop. */
static bool i2s_tone(uint32_t hz, bool left, bool right, uint32_t ms,
                     bool (*tick)(void)) {
    const uint32_t frames = (SAMPLE_RATE * ms) / 1000u;
    const uint32_t half   = SAMPLE_RATE / (2u * hz);
    const int16_t  amp    = 9000;            /* ~27% of full scale */

    absolute_time_t deadline = make_timeout_time_ms(ms * 4u + 200u);

    uint32_t to_tick = I2S_TICK_FRAMES;

    for (uint32_t i = 0; i < frames; i++) {
        if (tick && --to_tick == 0) {
            to_tick = I2S_TICK_FRAMES;
            if (tick()) return false;
        }

        int16_t v = ((i / half) & 1u) ? amp : (int16_t)-amp;
        uint32_t w = ((uint32_t)(uint16_t)(left  ? v : 0) << 16)
                   |  (uint32_t)(uint16_t)(right ? v : 0);

        while (pio_sm_is_tx_fifo_full(s_pio, s_sm)) {
            if (absolute_time_diff_us(get_absolute_time(), deadline) < 0)
                return false;
        }
        pio_sm_put(s_pio, s_sm, w);
    }
    return true;
}

static bool i2s_pass(const frank_pins_t *pins, int ch, bool (*abort_fn)(void)) {
    if (!i2s_bring_up(pins)) return false;

    for (unsigned n = 0; n < MELODY_LEN; n++) {
        if (abort_fn && abort_fn()) return false;

        if (melody[n].hz == REST) {
            /* A rest still has to be clocked out as samples. Letting the
             * FIFO run dry leaves the DAC holding its last level, which
             * clicks — and on a genuinely stuck channel would look like
             * silence for the wrong reason. */
            if (!i2s_tone(1, false, false, melody[n].ms, abort_fn)) return false;
        } else if (!i2s_tone(melody[n].hz, channels[ch].left,
                             channels[ch].right, melody[n].ms, abort_fn)) {
            return false;
        }
    }
    return true;
}

/* Sleep, but let the interface breathe.
 *
 * Used by the paths that genuinely idle — a rest, and every note of the
 * TurboSound melody, where the AYs generate the tone themselves and the
 * CPU has nothing to do. A single sleep_ms() of 300 there froze the
 * pointer for the whole note for no reason at all.
 *
 * Returns false if the tick asked to stop. */
static bool quiet_wait(uint32_t ms, bool (*tick)(void)) {
    while (ms) {
        const uint32_t slice = ms > 8u ? 8u : ms;
        sleep_ms(slice);
        ms -= slice;
        if (tick && tick()) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* PWM                                                                 */
/* ------------------------------------------------------------------ */

/* The same two pins as I2S, through RC filters, with the 74HC4052
 * choosing which analogue path reaches the amplifier. */
static bool pwm_pass(const frank_pins_t *pins, int ch, bool (*abort_fn)(void)) {
    if (pins->i2s_data == PIN_NC) return false;

    const uint pin_l = (uint)pins->i2s_data;
    const uint pin_r = (uint)pins->i2s_clk_base;

    gpio_set_function(pin_l, GPIO_FUNC_PWM);
    gpio_set_function(pin_r, GPIO_FUNC_PWM);

    const uint slice_l = pwm_gpio_to_slice_num(pin_l);
    const uint slice_r = pwm_gpio_to_slice_num(pin_r);

    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv(&c, 1.0f);
    pwm_config_set_wrap(&c, 1023);
    pwm_init(slice_l, &c, true);
    if (slice_r != slice_l) pwm_init(slice_r, &c, true);

    /* The counter advancing is the only measurable part: it proves the
     * slice is clocked. Everything past the pin is inaudible to the
     * firmware. */
    const uint16_t c0 = pwm_get_counter(slice_l);
    busy_wait_us_32(50);
    if (pwm_get_counter(slice_l) == c0) return false;

    /* The pads have changed owner; I2S must set itself up again. */
    s_i2s_up = false;

    /* Square wave by flipping the duty at the note frequency; the RC
     * filter on the way to the mux does the smoothing. */
    for (unsigned n = 0; n < MELODY_LEN; n++) {
        if (abort_fn && abort_fn()) return false;

        if (melody[n].hz == REST) {
            pwm_set_gpio_level(pin_l, 0);
            pwm_set_gpio_level(pin_r, 0);
            if (!quiet_wait(melody[n].ms, abort_fn)) return false;
            continue;
        }

        const uint32_t half_us = 500000u / melody[n].hz;
        const uint32_t cycles  = (melody[n].ms * 1000u) / (half_us * 2u);

        /* Roughly every 10 ms, whatever the pitch. This loop busy-waits
         * the whole note, so without a tick inside it the pointer would
         * only move once per note. */
        uint32_t per_tick = 10000u / (half_us * 2u);
        if (!per_tick) per_tick = 1u;

        for (uint32_t i = 0; i < cycles; i++) {
            if (abort_fn && (i % per_tick) == 0 && abort_fn()) return false;

            if (channels[ch].left)  pwm_set_gpio_level(pin_l, 900);
            if (channels[ch].right) pwm_set_gpio_level(pin_r, 900);
            busy_wait_us_32(half_us);
            if (channels[ch].left)  pwm_set_gpio_level(pin_l, 124);
            if (channels[ch].right) pwm_set_gpio_level(pin_r, 124);
            busy_wait_us_32(half_us);
        }
    }
    return true;
}

static void pwm_quiet(const frank_pins_t *pins) {
    if (pins->i2s_data == PIN_NC) return;
    const uint pin_l = (uint)pins->i2s_data;
    const uint pin_r = (uint)pins->i2s_clk_base;
    pwm_set_gpio_level(pin_l, 0);
    pwm_set_gpio_level(pin_r, 0);
    pwm_set_enabled(pwm_gpio_to_slice_num(pin_l), false);
    pwm_set_enabled(pwm_gpio_to_slice_num(pin_r), false);
}

/* ------------------------------------------------------------------ */
/* TurboSound                                                          */
/* ------------------------------------------------------------------ */

/* Two AY-3-8910 clones behind a pair of chained 74HC595s.
 *
 * The 16-bit word format is taken from aySoft.h in SpeccyP, by Constantin
 * (billgilbert7000), https://github.com/billgilbert7000/SpeccyP — which
 * drives the same arrangement:
 *
 *     * R * B 1 0 W A  d d d d d d d d
 *       ^   ^ ^^^ ^ ^  \--- data ---/
 *       |   | |   | +- A0    1 = latch a register number
 *       |   | |   +--- WR
 *       |   | +------- chip select, active low, one bit each
 *       |   +--------- BEEP
 *       +------------- RESET, active low
 *
 * Writing one AY register is two strobes: latch the address with A0 high,
 * then the value with A0 low, each released while holding the same chip
 * select and the same data byte.
 *
 * Bit-banged rather than driven from PIO. SpeccyP uses a state machine
 * because it is servicing an emulated Z80 in real time; a register write
 * here has no deadline, and all three PIOs are spoken for.
 *
 * The pin assignment is confirmed against SpeccyP's Murmulator 2 board
 * file, which MegaFRANK follows: CLK_LATCH_595_BASE_PIN=9 with the PIO
 * side-set driving base+1 (GP10) once per bit and base+0 (GP9) once per
 * word — so GP9 is RCLK and GP10 is SRCLK — and DATA_595_PIN=11.
 *
 * One M2 init step does *not* apply here: SpeccyP generates the AY master
 * clock from PWM on GP29. MegaFRANK has its own X1 3.58 MHz oscillator
 * divided by a 74HC74 to 1.75 MHz, and its GP29 is the RTC's I2C clock.
 *
 * Nothing comes back. The 595 chain is write-only and there is no return
 * path from the AY, so this proves the firmware drove the pins in the
 * right order and nothing more — which is exactly why it is a dialog the
 * operator listens to rather than a row that claims a pass. */

/* The BEEP bit idles HIGH. SpeccyP ORs its `beep595` into every word and
 * out_beep595(false) sets that bit — so a zero here does not mean "no
 * beeper", it means the beeper is held *on*, which puts a DC level into
 * the same summing network the AYs feed. */
#define AY_BEEP_OFF 0x1000u

#define AY_RES     (0x0300u | AY_BEEP_OFF)  /* reset asserted (R low)   */
#define AY_Z       (0x4C00u | AY_BEEP_OFF)  /* neither chip addressed   */

/* Per chip: strobe words, and an idle word that keeps *that* chip
 * selected with the bus inactive.
 *
 * The idle word matters more than it looks. Returning to AY_Z between
 * latching a register number and writing its value drives both chip
 * selects high, so the address is latched with one selection and the
 * data written with another. SpeccyP never does this — send595_0()
 * returns to AY0_Z and send595_1() to AY1_Z, each preserving its own
 * chip select and the data byte. Using the common idle instead is why
 * 260 register writes went out and nothing was heard. */
#define AY0_LATCH  (0x4B00u | AY_BEEP_OFF)  /* chip 0, BDIR+BC1: latch  */
#define AY0_WRITE  (0x4A00u | AY_BEEP_OFF)  /* chip 0, BDIR only: write */
#define AY0_IDLE   (0x4800u | AY_BEEP_OFF)  /* chip 0 held, bus idle    */
#define AY1_LATCH  (0x4700u | AY_BEEP_OFF)
#define AY1_WRITE  (0x4600u | AY_BEEP_OFF)
#define AY1_IDLE   (0x4400u | AY_BEEP_OFF)

static void ay_send(const frank_pins_t *p, uint16_t word) {
    /* MSB first: the control byte leads, so it ends up in the second
     * register of the chain and the data byte in the first. */
    for (int b = 15; b >= 0; b--) {
        gpio_put((uint)p->ay_ser, (word >> b) & 1u);
        gpio_put((uint)p->ay_srclk, 1);
        busy_wait_us_32(1);
        gpio_put((uint)p->ay_srclk, 0);
        busy_wait_us_32(1);
    }
    gpio_put((uint)p->ay_rclk, 1);      /* latch to the outputs */
    busy_wait_us_32(1);
    gpio_put((uint)p->ay_rclk, 0);
}

static void ay_write(const frank_pins_t *p, int chip, uint8_t reg, uint8_t val) {
    const uint16_t latch = chip ? AY1_LATCH : AY0_LATCH;
    const uint16_t write = chip ? AY1_WRITE : AY0_WRITE;
    const uint16_t idle  = chip ? AY1_IDLE  : AY0_IDLE;

    ay_send(p, latch | reg);
    ay_send(p, idle  | reg);      /* data byte held, not zeroed */
    ay_send(p, write | val);
    ay_send(p, idle  | val);
}

/* The AY divides its input clock by 16 for the tone counters. megafrank's
 * AYs run from the 74HC74 divider at 1.75 MHz — the Spectrum's rate,
 * which is what the chips and the music were designed around. */
#define AY_CLOCK_HZ 1750000u
#define AY_PERIOD(f) ((uint16_t)(AY_CLOCK_HZ / (16u * (f))))

/* Channel A is the left output, C the right, B the centre — which is how
 * a TurboSound board is normally wired and, conveniently, gives the same
 * three passes the other two paths make. */
static const uint8_t ay_chan_for[AUDIO_CHANNELS] = { 0, 2, 1 };

static bool ay_claim(const frank_pins_t *pins) {
    if (pins->ay_rclk == PIN_NC || pins->ay_srclk == PIN_NC ||
        pins->ay_ser == PIN_NC)
        return false;

    /* These are the I2S pins wearing another hat, so take them back. */
    s_i2s_up = false;
    const uint sig[3] = { (uint)pins->ay_rclk, (uint)pins->ay_srclk,
                          (uint)pins->ay_ser };
    for (int i = 0; i < 3; i++) {
        gpio_init(sig[i]);
        gpio_set_dir(sig[i], GPIO_OUT);
        gpio_put(sig[i], 0);
    }
    return true;
}

static bool ts_pass(const frank_pins_t *pins, int c, bool (*abort_fn)(void)) {
    if (!ay_claim(pins)) return false;

    ay_send(pins, AY_RES);
    sleep_ms(5);
    ay_send(pins, AY_Z);

    for (int chip = 0; chip < 2; chip++) {
        ay_write(pins, chip, 7, 0x38);                   /* tone A/B/C, no noise */
        for (int ch = 0; ch < 3; ch++)
            ay_write(pins, chip, (uint8_t)(8 + ch), 0);  /* start silent */
    }

    const uint8_t ch = ay_chan_for[c];

    for (unsigned n = 0; n < MELODY_LEN; n++) {
        if (abort_fn && abort_fn()) return false;

        if (melody[n].hz != REST) {
            const uint16_t per = AY_PERIOD(melody[n].hz);
            /* Both chips together: a TurboSound with one dead AY would
             * otherwise pass unnoticed on whichever half was picked. */
            for (int chip = 0; chip < 2; chip++) {
                ay_write(pins, chip, (uint8_t)(ch * 2),     per & 0xFF);
                ay_write(pins, chip, (uint8_t)(ch * 2 + 1), per >> 8);
                ay_write(pins, chip, (uint8_t)(8 + ch),     12);
            }
        }
        const bool go_on = quiet_wait(melody[n].ms, abort_fn);

        for (int chip = 0; chip < 2; chip++)
            ay_write(pins, chip, (uint8_t)(8 + ch), 0);
        if (!go_on) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* The interface frank_audio.h publishes                               */
/* ------------------------------------------------------------------ */

bool audio_play(const detect_result_t *d, audio_src_t s, int ch,
                bool (*abort_fn)(void)) {
    if (!audio_src_available(d, s)) return false;
    if (ch < 0 || ch >= AUDIO_CHANNELS) return false;

    const frank_pins_t *pins = &d->board->pins;
    switch (s) {
        case AUDIO_SRC_I2S: return i2s_pass(pins, ch, abort_fn);
        case AUDIO_SRC_PWM: return pwm_pass(pins, ch, abort_fn);
        case AUDIO_SRC_TS:  return ts_pass(pins, ch, abort_fn);
        default:            return false;
    }
}

void audio_stop(const detect_result_t *d, audio_src_t s) {
    if (!d || !d->board) return;
    const frank_pins_t *pins = &d->board->pins;

    switch (s) {
        case AUDIO_SRC_I2S:
            if (s_i2s_up) {
                pio_sm_set_enabled(s_pio, s_sm, false);
                pio_sm_clear_fifos(s_pio, s_sm);
            }
            break;
        case AUDIO_SRC_PWM:
            pwm_quiet(pins);
            break;
        case AUDIO_SRC_TS:
            if (ay_claim(pins)) {
                for (int chip = 0; chip < 2; chip++)
                    for (int ch = 0; ch < 3; ch++)
                        ay_write(pins, chip, (uint8_t)(8 + ch), 0);
                ay_send(pins, AY_Z);
            }
            break;
        default:
            break;
    }
}
