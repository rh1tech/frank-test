/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tlv320.h — frank_next's codec, over I2C.
 *
 * A TLV320DAC3100: stereo DAC, headphone drivers and a mono Class-D
 * speaker amp in one part, at 0x18 on the same I2C pair as the DS3231.
 * It is the only audio path in the fleet that will not make a sound
 * until it has been configured — every other board's DAC starts working
 * the moment I2S arrives — which is why frank_next declares
 * CAP_AUDIO_CODEC_I2C rather than plain CAP_AUDIO_I2S.
 *
 *
 * TWO OUTPUTS, ONE AT A TIME
 *
 * The headphone drivers and the Class-D amp are separate blocks with
 * separate power bits, and both come out of reset powered down. Bringing
 * up one says nothing about the other: for a while this driver
 * configured only the headphone path, and the speaker was reported dead
 * when it had simply never been switched on.
 *
 * They are brought up one at a time rather than together, because the
 * operator is being asked which one they heard. Two live outputs and one
 * question is how "the speaker works" gets recorded about a pair of
 * headphones.
 *
 * The Class-D amp is mono and its input is the left output mixer alone —
 * there is no path from the right DAC to it — so the speaker is driven
 * with the left channel and gets one pass rather than three. A "Right"
 * pass on it would be silence by construction, which is the worst thing
 * to show someone whose job is to answer "did you hear that?".
 *
 *
 * THE CLOCK IT IS GIVEN
 *
 * The board wires MCLK to GP24, and this driver does not use it.
 *
 * Nothing in this firmware generates a master clock: the I2S state
 * machine in drivers/audio_i2s.pio produces BCLK and LRCK and no third
 * clock, so an MCLK-referenced configuration would be waiting for an
 * edge that never comes. The codec's PLL is therefore referenced to
 * BCLK, which it supports, and the part runs entirely from the two
 * clocks the PIO already sends.
 *
 * Same reasoning as the PCM5122 hat, arrived at from the opposite
 * direction: there the board wires no MCLK at all, here it wires one
 * that nothing drives.
 *
 *
 * WHAT HAS AND HAS NOT BEEN VERIFIED
 *
 * The I2C layer is the same one detection uses and the same one the
 * DS3231 and PCM5122 drivers use, and the part answering at 0x18 is
 * already proven by detect.c on real hardware.
 *
 * The register sequence was written from the TLV320DAC3100 datasheet
 * rather than ported from a firmware that has been heard to work,
 * because no such firmware exists in this fleet — the PCM5122 driver
 * beside it had SpeccyP to copy from and this had nothing.
 *
 * The headphone half of it has since been heard on a FRANK Next, which
 * settles most of the file: the clocking, the BCLK-referenced PLL, the
 * dividers, the serial interface and the left and right mixers are all
 * upstream of that jack, and none of them can be quietly wrong now.
 *
 * The three writes that bring up the Class-D amp are newer and carry the
 * original caveat by themselves. What they have proved so far is that
 * the codec acknowledges them.
 *
 * That is why tlv320_init() reports only whether every write was
 * acknowledged, and why the dialog it feeds asks a person what they
 * heard. An acknowledged write proves the part is on the bus and
 * nothing else: a codec with the wrong PLL settings acknowledges every
 * byte and stays perfectly silent. Treat a pass here as "the bus works
 * and the sequence ran", and the operator's answer as the result.
 */
#ifndef TLV320_H
#define TLV320_H

#include <stdbool.h>

/* Which output block to bring up. Never both — see above. */
typedef enum {
    TLV320_OUT_HP = 0,   /* HPL and HPR, the headphone jack       */
    TLV320_OUT_SPK,      /* the mono Class-D amp, left channel    */
} tlv320_out_t;

/* Does a TLV320DAC3100 answer at 0x18? Bit-banged, and it leaves the
 * pins released. */
bool tlv320_detect(unsigned sda, unsigned scl);

/* Reset, then configure for 16-bit I2S at 44.1 kHz with the PLL
 * referenced to BCLK, the DAC and the chosen output powered, unmuted at
 * 0 dB. The output not chosen is left powered down, which is where the
 * reset left it. Returns false as soon as any write goes unacknowledged.
 *
 * Safe to call again, and safe to call with the other output: the
 * sequence begins with a software reset, so switching outputs is a
 * plain second call rather than an undo.
 *
 * Must be called with the I2S clocks already running. The PLL locks to
 * BCLK, so configuring the codec into silence and starting the clocks
 * afterwards leaves it looking for a reference that was not there when
 * it looked. */
bool tlv320_init(unsigned sda, unsigned scl, tlv320_out_t out);

/* Mute, power both output blocks down, leave the pins released.
 *
 * Both, whichever tlv320_init() was asked for: writing the power bit of
 * a block that is already down costs one byte and removes the need for
 * this to remember, or be told, what the last call did.
 *
 * Called before the I2S state machine stops, not after. A driver left
 * powered with the bit clock gone holds whatever level the last sample
 * had, which is the click at the end of a pass — the same mistake the
 * PCM5122 path made once and the reason its stop order is spelled out
 * in tests_audio.c. */
bool tlv320_quiet(unsigned sda, unsigned scl);

#endif /* TLV320_H */
