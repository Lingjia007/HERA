/**
  ******************************************************************************
  * @file    kws_postprocess.c
  * @brief   Implementation of KWS post-processing (debounce + cooldown).
  ******************************************************************************
  */
#include "kws_postprocess.h"

#include <math.h>
#include <stdio.h>

void kws_postprocess_init(kws_state_t *s)
{
  for (int i = 0; i < KWS_HISTORY_M; i++)
    s->hist[i] = -1;
  s->hist_idx = 0;
  s->cooldown_remaining = 0;
  s->trigger_cnt = 0;
}

int kws_postprocess_feed(kws_state_t *s,
                         const int8_t *logits,
                         uint8_t num_labels,
                         const char *const *label_names)
{
  /* 1) argmax over all labels. */
  int max_idx = 0;
  int8_t max_val = logits[0];
  for (int i = 1; i < num_labels; i++)
  {
    if (logits[i] > max_val)
    {
      max_val = logits[i];
      max_idx = i;
    }
  }

  /* 2) Drop non-keyword labels: 0=_silence_, 1=_unknown_. */
  if (max_idx <= 1)
    return 0;

  /* 3) Record this inference's label into the circular history buffer. */
  s->hist[s->hist_idx] = (int8_t)max_idx;
  s->hist_idx = (s->hist_idx + 1) % KWS_HISTORY_M;

  /* 4) Global cooldown: suppress all triggers right after any keyword. */
  if (s->cooldown_remaining > 0)
  {
    s->cooldown_remaining--;
    return 0;
  }

  /* 5) Reject low-confidence outputs (noise / truncated syllables).
   *    Spurious "up" from noise typically scores 15-18; real words >= 25. */
  if (max_val < KWS_MIN_SCORE)
    return 0;

  /* 6) Count hits of the current label within the last M inferences. */
  uint8_t hits = 0;
  for (int i = 0; i < KWS_HISTORY_M; i++)
    if (s->hist[i] == (int8_t)max_idx)
      hits++;

  if (hits < KWS_REQUIRED_N)
    return 0;

  /* 7) Confirmed keyword: compute softmax probability for display. */
  float max_f = (float)max_val;
  float sum = 0.0f;
  /* Softmax scratch on stack; num_labels is small (36 for micro_speech). */
  float exp_vals[64];
  if (num_labels > 64)
    num_labels = 64; /* guard against overflow */
  for (int i = 0; i < num_labels; i++)
  {
    exp_vals[i] = expf((float)logits[i] - max_f);
    sum += exp_vals[i];
  }
  float prob = exp_vals[max_idx] / sum;

  printf("[KWS %lu] %s (%.1f%%, score=%d, hits=%d/%d)\r\n",
         (unsigned long)s->trigger_cnt, label_names[max_idx],
         prob * 100.0f, max_val, hits, KWS_HISTORY_M);

  s->trigger_cnt++;
  s->cooldown_remaining = KWS_COOLDOWN;

  /* Clear history so the same word doesn't immediately re-qualify. */
  for (int i = 0; i < KWS_HISTORY_M; i++)
    s->hist[i] = -1;

  return 1;
}
