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
 * The register sequence is not. It is written from the TLV320DAC3100
 * datasheet rather than ported from a firmware that has been heard to
 * work, because no such firmware exists in this fleet yet — the
 * PCM5122 driver beside it had SpeccyP to copy from and this has
 * nothing. Nobody has put a FRANK Next on a bench and listened to it.
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

/* Does a TLV320DAC3100 answer at 0x18? Bit-banged, and it leaves the
 * pins released. */
bool tlv320_detect(unsigned sda, unsigned scl);

/* Reset, then configure for 16-bit I2S at 44.1 kHz with the PLL
 * referenced to BCLK, DAC and headphone drivers powered, unmuted at
 * 0 dB. Returns false as soon as any write goes unacknowledged.
 *
 * Safe to call again: the sequence begins with a software reset.
 *
 * Must be called with the I2S clocks already running. The PLL locks to
 * BCLK, so configuring the codec into silence and starting the clocks
 * afterwards leaves it looking for a reference that was not there when
 * it looked. */
bool tlv320_init(unsigned sda, unsigned scl);

/* Mute, power the drivers down, leave the pins released.
 *
 * Called before the I2S state machine stops, not after. A driver left
 * powered with the bit clock gone holds whatever level the last sample
 * had, which is the click at the end of a pass — the same mistake the
 * PCM5122 path made once and the reason its stop order is spelled out
 * in tests_audio.c. */
bool tlv320_quiet(unsigned sda, unsigned scl);

#endif /* TLV320_H */
