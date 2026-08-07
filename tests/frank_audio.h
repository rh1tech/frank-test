/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * frank_audio.h — the three sound paths, as something to drive rather
 * than something to run.
 *
 * These used to be rows in the test list, which was the wrong shape for
 * them. Every other test measures something and returns a verdict; none
 * of these can. There is no loopback and no ADC anywhere in the fleet, so
 * past the pin the firmware is deaf, and a row that says PASS after
 * driving a dead amplifier is worse than no row at all.
 *
 * What the operator actually needs is to hear it — repeatedly, with the
 * switches in different positions, for as long as it takes. So audio is
 * now a dialog: pick a source, it loops left/right/centre and names the
 * channel while it plays, and it stops when you say so. The verdict is
 * the operator's, which is the only place it could ever have lived.
 */
#ifndef FRANK_AUDIO_H
#define FRANK_AUDIO_H

#include "detect.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AUDIO_SRC_PWM = 0,
    AUDIO_SRC_I2S,
    AUDIO_SRC_TS,
    AUDIO_SRC_PCM5122,
    /* frank_next's codec has two outputs with separate power bits, and
     * they are separate sources here for the same reason they are
     * brought up one at a time in the driver: the operator is being
     * asked what they heard, and one question about two outputs is how
     * a working headphone jack gets recorded as a working speaker. */
    AUDIO_SRC_TLV320_HP,
    AUDIO_SRC_TLV320_SPK,
    AUDIO_SRC_COUNT
} audio_src_t;

/* Left, right, centre — the most any source has. */
#define AUDIO_CHANNELS 3

const char *audio_src_name(audio_src_t s);

/* How many passes this source makes, 1 to AUDIO_CHANNELS.
 *
 * Three for anything stereo. One for the codec's Class-D amp, which is
 * mono and fed from the left output mixer alone: a right-channel pass on
 * it is silence by construction, and asking someone to listen to a
 * channel the hardware was never going to reproduce is how "Silent" gets
 * pressed about a speaker that works. */
int audio_src_passes(audio_src_t s);

/* What to call pass `ch` of this source, on screen and in the log. Takes
 * the source because "Left" is the wrong name for the only pass a mono
 * output makes. */
const char *audio_channel_name(audio_src_t s, int ch);

/* The capability a board must declare for this source to be reachable. */
uint32_t audio_src_cap(audio_src_t s);

/* Is this source usable on the detected board? False when the board
 * lacks the hardware, or when no board has been identified — the pins
 * come from the descriptor, so driving them without one is guesswork. */
bool audio_src_available(const detect_result_t *d, audio_src_t s);

/* The one thing the operator has to know about this source before they
 * listen to it, or NULL when there is nothing to say.
 *
 * Two things qualify so far. On megafrank the audio mux is a 4:1
 * selector with no firmware access at all, so the wrong two switches
 * make a working DAC completely silent and the switch positions have to
 * be named. On the codec's speaker it is that the amp is mono, because
 * an operator who was expecting stereo and got one channel needs to know
 * which of those two facts they are looking at.
 *
 * Boards and sources with neither get NULL: inventing switches they do
 * not have is worse than saying nothing at all. */
const char *audio_src_hint(const detect_result_t *d, audio_src_t s);

/* Play the melody once through pass `ch` — 0 left, 1 right, 2 centre on
 * a stereo source, and 0 the only pass a mono one makes. Must be less
 * than audio_src_passes(s).
 *
 * `abort_fn`, if given, is called roughly every 10 ms *during* each note,
 * not merely between them; returning true stops the pass early. Between
 * notes was the obvious place and it was not enough — at 150 ms per note
 * the interface updated seven times a second, which is what a slow mouse
 * looks like.
 *
 * It must be cheap. On the I2S path the TX FIFO holds 180 us of audio,
 * so a tick that outlasts that puts a gap in the sound; moving the
 * cursor overlay is microseconds, recomposing the screen is not.
 *
 * Returns false if the pass was aborted or the hardware stalled. */
bool audio_play(const detect_result_t *d, audio_src_t s, int ch,
                bool (*abort_fn)(void));

/* Leave the source quiet and its pins in a state the next source can
 * take over. Always call this when done: the three paths share GP9-11 on
 * the boards that have all three, and a pad left owned by PWM makes I2S
 * silent with no error anywhere. */
void audio_stop(const detect_result_t *d, audio_src_t s);

/* Reset the TurboSound AYs and leave them silent. No-op on every board
 * without one.
 *
 * Called during start-up, which is the whole point of it. An AY comes
 * out of power-on with its registers in an undefined state, and nothing
 * on these boards resets it except the R bit in the 74HC595 word this
 * firmware drives — so a board that was making a noise when it was reset
 * carries on making it, through a reboot, until something writes to the
 * chips. That was reported as noise after reset and it is exactly what
 * it sounds like.
 *
 * On megafrank the shift-register pins are the I2S pins wearing another
 * hat, so this claims GP9-11 for the millisecond it takes and hands them
 * back. Doing it before the audio path is brought up keeps that from
 * mattering. */
void audio_ts_silence(const detect_result_t *d);

#endif /* FRANK_AUDIO_H */
