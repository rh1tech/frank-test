/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * pcm5122.h — the audio hat's DAC, over I2C.
 *
 * A PCM5122 on an add-on board for the Waveshare RP2350-PiZero. It is
 * not a FRANK part and it is not soldered to anything: it arrives on a
 * hat that may or may not be fitted, which is why nothing about it is
 * declared as present. The board declares only that it can take one.
 *
 * The register sequence is ported from SpeccyP, which drives this exact
 * hat on this exact board. It matters that it came from working code
 * rather than from the datasheet, because the interesting part is not
 * documented as a default: the hat wires no system clock, so the DAC
 * has to be told to derive its PLL from the bit clock and to stop
 * treating the absent SCK as a fault. Miss either and the chip
 * initialises cleanly, acknowledges every write, and stays silent.
 */
#ifndef PCM5122_H
#define PCM5122_H

#include <stdbool.h>

/* Does a PCM5122 answer on this bus? Bit-banged, so it costs nothing
 * but the two pins and leaves them released afterwards. */
bool pcm5122_detect(unsigned sda, unsigned scl);

/* Bring the DAC up for 16-bit I2S with no master clock: out of reset
 * and standby, PLL referenced to BCK, SCK error detection off, unmuted
 * at 0 dB. Returns false if it does not answer.
 *
 * Safe to call again; the sequence starts with a reset. */
bool pcm5122_init(unsigned sda, unsigned scl);

/* Mute and return to standby, leaving the pins released. The melody
 * ends with the I2S state machine stopped, and a DAC left unmuted with
 * no bit clock is where the audible click at the end came from. */
bool pcm5122_quiet(unsigned sda, unsigned scl);

#endif /* PCM5122_H */
