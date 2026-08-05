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
    AUDIO_SRC_COUNT
} audio_src_t;

#define AUDIO_CHANNELS 3

const char *audio_src_name(audio_src_t s);
const char *audio_channel_name(int ch);

/* The capability a board must declare for this source to be reachable. */
uint32_t audio_src_cap(audio_src_t s);

/* Is this source usable on the detected board? False when the board
 * lacks the hardware, or when no board has been identified — the pins
 * come from the descriptor, so driving them without one is guesswork. */
bool audio_src_available(const detect_result_t *d, audio_src_t s);

/* The switch positions this source needs, or NULL on a board whose audio
 * path has no switches. Naming them is not politeness: on megafrank the
 * mux is a 4:1 selector with no firmware access at all, so the wrong two
 * switches make a working DAC completely silent. */
const char *audio_src_switch_hint(const detect_result_t *d, audio_src_t s);

/* Play the melody once through `ch` (0 left, 1 right, 2 centre).
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

#endif /* FRANK_AUDIO_H */
