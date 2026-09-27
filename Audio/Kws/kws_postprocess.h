/**
  ******************************************************************************
  * @file    kws_postprocess.h
  * @brief   Keyword Spotting (KWS) post-processing for streaming micro_speech.
  *
  * Filters raw per-frame int8 logits from the micro_speech classifier to
  * reject spurious triggers and emit stable keyword detections:
  *   1) Drop _silence_/_unknown_ (labels 0/1) — not useful to report.
  *   2) N-in-M debounce: need >= N matches of same label in last M inferences
  *      to reject truncated-syllable spurious labels at window edges.
  *   3) MIN_SCORE: reject noise (spurious "up" scores 15-18).
  *   4) Global cooldown: after any trigger, suppress all triggers for
  *      COOLDOWN inferences to avoid one word firing repeatedly.
  *
  * With a 200 ms slide window, M=5 covers ~1 s and COOLDOWN=5 ~= 1 s.
  ******************************************************************************
  */
#ifndef KWS_POSTPROCESS_H
#define KWS_POSTPROCESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default KWS post-processing parameters.
 *   HISTORY_M   : look back over last M inferences
 *   REQUIRED_N  : need >= N matches of same label in window
 *   COOLDOWN    : global suppress after any trigger (in inferences)
 *   MIN_SCORE   : reject noise (spurious "up" scores 15-18)            */
#define KWS_HISTORY_M   5
#define KWS_REQUIRED_N  3
#define KWS_COOLDOWN    5
#define KWS_MIN_SCORE   20

typedef struct
{
  int8_t   hist[KWS_HISTORY_M];   /* circular label history buffer       */
  uint8_t  hist_idx;              /* next write slot in hist[]           */
  uint8_t  cooldown_remaining;    /* >0 suppresses all triggers          */
  uint32_t trigger_cnt;           /* number of confirmed keywords so far */
} kws_state_t;

/* Zero-init the state. Call once before the first kws_postprocess_feed(). */
void kws_postprocess_init(kws_state_t *s);

/* Feed one inference's int8 logits.
 *
 *   s           : post-processing state (caller-owned)
 *   logits      : int8 classifier output, length num_labels
 *   num_labels  : number of labels in logits / label_names
 *   label_names : array of label name strings (size num_labels)
 *
 * Returns:
 *   1 if a keyword was confirmed and printed,
 *   0 otherwise (silence/unknown, below threshold, debounce not met,
 *   or within cooldown).
 *
 * Labels 0 and 1 are assumed to be _silence_ and _unknown_ and are
 * silently dropped (never recorded in history, never trigger). */
int kws_postprocess_feed(kws_state_t *s,
                         const int8_t *logits,
                         uint8_t num_labels,
                         const char *const *label_names);

#ifdef __cplusplus
}
#endif
#endif /* KWS_POSTPROCESS_H */
