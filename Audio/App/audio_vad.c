/**
  ******************************************************************************
  * @file    audio_vad.c
  * @brief   Energy-based VAD implementation (see audio_vad.h for the rules).
  *
  * State is a module singleton shared IRQ (producer) / main loop (consumer).
  * Word-size reads/writes are atomic on Cortex-M4; benign ordering is
  * acceptable (the main loop only polls level + prints tuning values).
  ******************************************************************************
  */
#include "audio_vad.h"

static volatile uint32_t s_floor = VAD_FLOOR_INIT;
static volatile uint32_t s_energy = 0;
static volatile uint8_t s_active = 0;
static volatile uint8_t s_loud = 0; /* last block above the hold threshold */
static uint32_t s_hangover = 0;
static uint32_t s_active_blocks = 0; /* continuous active duration */

void audio_vad_init(void)
{
  s_floor = VAD_FLOOR_INIT;
  s_energy = 0;
  s_active = 0;
  s_loud = 0;
  s_hangover = 0;
  s_active_blocks = 0;
}

void audio_vad_feed_block(const int16_t *samples, uint32_t n)
{
  uint32_t sum = 0;
  for (uint32_t i = 0; i < n; i++)
  {
    int32_t v = samples[i];
    sum += (uint32_t)((v < 0) ? -v : v);
  }
  uint32_t e = (n > 0) ? (sum / n) : 0; /* mean abs, 0..32768 */

  s_energy = e;
  uint32_t fl = s_floor;

  /* True block loudness (above the hold threshold): the caller counts
   * these to measure actual speech inside a capture, so hangover-extended
   * quiet blocks are NOT counted as speech. */
  s_loud = (e > fl * VAD_CLOSE_MARGIN) ? 1 : 0;

  if (!s_active)
  {
    /* Track floor only while inactive: speech must not inflate it. */
    fl = (uint32_t)((int32_t)fl +
                    (((int32_t)e - (int32_t)fl) >> VAD_FLOOR_SHIFT));
    if (fl < VAD_FLOOR_MIN)
      fl = VAD_FLOOR_MIN;
    s_floor = fl;

    if (e > fl * VAD_OPEN_MARGIN)
    {
      s_active = 1;
      s_hangover = VAD_HANGOVER_BLOCKS;
      s_active_blocks = 0;
    }
  }
  else
  {
    if (e > fl * VAD_CLOSE_MARGIN)
      s_hangover = VAD_HANGOVER_BLOCKS;
    else if (s_hangover > 0)
      s_hangover--;

    if (s_hangover == 0)
    {
      s_active = 0;
    }
    else if (++s_active_blocks >= VAD_MAX_ACTIVE_BLOCKS)
    {
      /* Anti-stuck: an ambient swell riding above the frozen floor's hold
       * threshold keeps the hangover refreshed forever, so no OFF -> ON
       * edge can ever occur again and every following word is swallowed.
       * Force close and re-seed the floor from the current level. */
      s_floor = (e > VAD_FLOOR_MIN) ? e : VAD_FLOOR_MIN;
      s_active = 0;
      s_hangover = 0;
    }
  }
}

uint8_t audio_vad_active(void) { return s_active; }
uint8_t audio_vad_loud(void) { return s_loud; }
uint32_t audio_vad_energy(void) { return s_energy; }
uint32_t audio_vad_floor(void) { return s_floor; }
