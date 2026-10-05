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
  *   - block energy (mean absolute) > floor * VAD_OPEN_MARGIN  -> open
  *   - block energy > floor * VAD_CLOSE_MARGIN                 -> hold
  *   - otherwise hangover counts down; at 0 active closes
  *   - noise floor is tracked one-pole ONLY while inactive (speech must
  *     not inflate it), clamped to VAD_FLOOR_MIN
  ******************************************************************************
  */
#ifndef AUDIO_VAD_H
#define AUDIO_VAD_H

#include <stdint.h>

/* Open/hold hysteresis margins (multipliers on the noise floor).
 * OPEN=8: measured room ambient peaks ~20-72 while real keywords peak
 * 500+, so x8 (threshold ~120-290) filters the noise swell false opens
 * (peak 80-127) that each wasted a 1 s capture + inference, while real
 * words still cross within ~20-50 ms of onset. */
#define VAD_OPEN_MARGIN        8
/* CLOSE=4: hangover refresh and speech counting need clearly-loud blocks
 * (floor x4), so faint sustained noise is not counted as speech. */
#define VAD_CLOSE_MARGIN       4

/* Active is held this many quiet blocks after the last loud block. */
#define VAD_HANGOVER_BLOCKS    30   /* 30 x 10 ms = 300 ms */

/* Anti-stuck: if the VAD stays CONTINUOUSLY active this long, force it
 * inactive and re-seed the floor from the current block energy. Keywords
 * are < ~1.5 s; endless activity means an ambient swell riding above the
 * frozen floor's hold threshold - without this it never deactivates again
 * and every following word is swallowed (no OFF -> ON edge ever). */
#define VAD_MAX_ACTIVE_BLOCKS  200  /* 200 x 10 ms = 2 s */

/* One-pole floor tracking: floor += (energy - floor) >> VAD_FLOOR_SHIFT */
#define VAD_FLOOR_SHIFT        6    /* tau ~ 64 blocks = 640 ms */

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

/* Last block mean-absolute energy and current noise floor (tuning prints). */
uint32_t audio_vad_energy(void);
uint32_t audio_vad_floor(void);

#endif /* AUDIO_VAD_H */
