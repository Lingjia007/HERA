/**
  ******************************************************************************
  * @file    audio_vad.h
  * @brief   Energy-based voice activity detection on the raw capture path
  *          (pre-gain, post-DC-removal domain, fed per 10 ms DMA block).
  *
  * The SAI IRQ feeds every 10 ms block; the main loop polls the active flag
  * and starts utterance capture on a rising edge (OFF -> ON).
  *
  * Decision rules (all tunable below):
  *   - block energy (mean absolute) above min(floor * VAD_OPEN_MARGIN, VAD_THRESHOLD_ABS_MAX)  -> open
  *   - block energy above open bar / 2 refreshes the hangover and counts as speech                 -> hold
  *   - otherwise hangover counts down; at 0 active closes
  *   - noise floor is tracked one-pole ONLY while inactive (speech must
  *     not inflate it), clamped to VAD_FLOOR_MIN
  ******************************************************************************
  */
#ifndef AUDIO_VAD_H
#define AUDIO_VAD_H

#include <stdint.h>

/* Open margin (multiplier on the noise floor) + ABSOLUTE ceiling on the
 * resulting bar. Distance costs a keyword ~10-14 dB of level, and the
 * adaptive floor follows ANY ambient noise upward, so a plain floor x8
 * bar in a non-silent room regularly sat above a 2-3 m voice - distant
 * keywords only triggered on a shout. The bar is now:
 *   open_thr = min(floor * VAD_OPEN_MARGIN, VAD_THRESHOLD_ABS_MAX)
 * - margin 8 -> 5: quiet rooms keep a low bar, distant words cross it.
 * - the ceiling bounds noisy rooms AND the anti-stuck re-seed (which
 *   sets floor = current energy and used to lift the bar far above any
 *   distant voice for seconds afterwards).
 * Noise blips that now slip under the lower bar are still filtered by
 * VAD_MIN_SPEECH_SAMPLES and the KWS score/margin gate. */
#define VAD_OPEN_MARGIN        5
#define VAD_THRESHOLD_ABS_MAX  480U

/* Hold (loud) threshold = open bar / 2, tied to the REAL bar instead of
 * a second floor multiplier: faint distant tails stay counted as speech
 * so a capture survives the whole word instead of dying mid-word. */

/* Active is held this many quiet blocks after the last loud block. */
#define VAD_HANGOVER_BLOCKS    30   /* 30 x 10 ms = 300 ms */

/* Anti-stuck: if the VAD stays CONTINUOUSLY active this long, force it
 * inactive and re-seed the floor from the current block energy. Keywords
 * are < ~1.5 s; endless activity means an ambient swell riding above the
 * frozen floor's hold threshold - without this it never deactivates again
 * and every following word is swallowed (no OFF -> ON edge ever). */
#define VAD_MAX_ACTIVE_BLOCKS  200  /* 200 x 10 ms = 2 s */

/* Asymmetric one-pole floor tracking:
 *   floor += (energy - floor) >> shift
 *   rising : shift = VAD_FLOOR_RISE_SHIFT (slow - a brief swell must not
 *             lift the bar for seconds)
 *   falling: shift = VAD_FLOOR_FALL_SHIFT (fast - distance sensitivity
 *             comes back quickly once the noise ends) */
#define VAD_FLOOR_RISE_SHIFT   7    /* tau ~ 128 blocks = 1.28 s */
#define VAD_FLOOR_FALL_SHIFT   5    /* tau ~  32 blocks = 0.32 s */

/* Absolute floor clamp (mean-abs scale): keeps a digital-silence floor of 0
 * from opening on the tiniest click. */
#define VAD_FLOOR_MIN          8

/* Boot-time floor until adapted; higher than expected ambient so no
 * spurious open occurs during the first adaptation second. */
#define VAD_FLOOR_INIT         400

/* Reset state (call once before SAI DMA starts). */
void audio_vad_init(void);

/* Producer: feed one audio block (SAI IRQ context). n = samples in block. */
void audio_vad_feed_block(const int16_t *samples, uint32_t n);

/* Consumer (main loop): 1 = speech active (including hangover), 0 = idle. */
uint8_t audio_vad_active(void);

/* Consumer (main loop): 1 = the LAST FED block energy was above the hold
 * threshold (real speech energy, NOT hangover). Used to measure the actual
 * speech content of a capture; blocks consumed by the main loop correspond
 * to the VAD's last-fed block as long as the ring backlog stays ~0. */
uint8_t audio_vad_loud(void);

/* Last block mean-absolute energy, current noise floor and the current
 * open bar floor*margin (capped) - tuning prints. */
uint32_t audio_vad_energy(void);
uint32_t audio_vad_floor(void);
uint32_t audio_vad_open_threshold(void);

#endif /* AUDIO_VAD_H */
