/**
 ******************************************************************************
 * @file    kws_postprocess.h
 * @brief   Keyword Spotting (KWS) decision for streaming micro_speech.
 *
 * Turns per-frame argmax results (one every 200 ms) into ONE confirmed
 * keyword event per utterance, using segment-based majority voting instead
 * of an N-consecutive-frame debounce:
 *
 *   1) A keyword frame (argmax not _silence_/_unknown_) opens or extends a
 *      candidate segment for its label (votes++, peak = max score).
 *   2) A DIFFERENT label takes over the candidate only if the candidate is
 *      NOT yet established (votes < KWS_MIN_VOTES) and is at least as
 *      strong (handles mislabeled onset/offset frames, e.g. "four 54"
 *      before "one"), or the newcomer is stronger by KWS_SWITCH_MARGIN.
 *      An established candidate cannot be stolen by a 1-point tail blip
 *      (e.g. "seven 36" stealing a locked "eight" at peak 35).
 *   3) The segment ends after KWS_SEG_END consecutive non-candidate frames
 *      (silence, _unknown_, or weaker other labels).
 *   4) At segment end the word is confirmed iff
 *         (votes >= KWS_MIN_VOTES && peak >= KWS_PEAK_MIN)
 *           || peak >= KWS_STRONG_SCORE
 *      and printed exactly once, then KWS_TAIL_COOLDOWN frame(s) are
 *      swallowed to prevent a second trigger from the same utterance.
 *      Isolated frames are judged by score, not discarded: a lone frame
 *      scoring >= KWS_STRONG_SCORE is a valid detection (e.g. "zero 47"),
 *      while a low-score one (e.g. "seven 34") is rejected.
 *   5) EVERY segment (confirmed or not) is dumped as a "====" delimited
 *      block: one line per slide with its raw argmax + score (including
 *      intruder/gap frames), then the verdict line with the actually
 *      computed votes / peak / score total / average for tuning.
 *
 * Empirical basis (on-device 200 ms slide log): a spoken word spans 4-6
 * frames with peak scores 47-78; edge frames score 19-45 and are often
 * mislabeled; isolated frames are kept or dropped by score ("zero 47"
 * confirms, "seven 34" does not).
 ******************************************************************************
 */
#ifndef KWS_POSTPROCESS_H
#define KWS_POSTPROCESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Decision parameters (tuned on on-device logs, see brief above).
 *   KWS_MIN_VOTES     : same-label frames required for the multi-frame path
 *   KWS_PEAK_MIN      : peak int8 score required for the multi-frame path
 *   KWS_STRONG_SCORE  : peak that confirms a segment on its own (single
 *                       high-confidence frame is valid, low score is not)
 *   KWS_SWITCH_MARGIN : score advantage needed to take over an established
 *                       candidate (votes >= KWS_MIN_VOTES)
 *   KWS_SEG_END       : consecutive non-candidate frames that close a segment
 *   KWS_MAX_LEN       : force segment close after this many frames (~2 s)
 *   KWS_TAIL_COOLDOWN : frames swallowed right after a confirmed trigger    */
#define KWS_MIN_VOTES 3
#define KWS_PEAK_MIN 35
#define KWS_STRONG_SCORE 45
#define KWS_SWITCH_MARGIN 10
#define KWS_SEG_END 2
#define KWS_MAX_LEN 10
#define KWS_TAIL_COOLDOWN 1

  typedef struct
  {
    int8_t label;                      /* current candidate label, -1 = none            */
    uint8_t votes;                     /* frames matching the candidate label           */
    int8_t peak;                       /* highest score seen for the candidate label    */
    uint8_t other_run;                 /* consecutive non-candidate frames (incl. gap)  */
    uint8_t len;                       /* total frames in the current segment           */
    uint8_t cooldown;                  /* frames to swallow right after a trigger       */
    uint32_t trigger_cnt;              /* number of confirmed keywords so far           */
    uint8_t seg_n;                     /* frames logged for the finalize dump           */
    int8_t seg_label[KWS_MAX_LEN + 1]; /* per-frame argmax label           */
    int8_t seg_score[KWS_MAX_LEN + 1]; /* per-frame int8 score             */
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
   *   1 if a keyword was confirmed and printed (once per utterance),
   *   0 otherwise.
   *
   * Labels 0 and 1 are assumed to be _silence_ and _unknown_: they never
   * trigger and only count as segment-ending gap frames. */
  int kws_postprocess_feed(kws_state_t *s,
                           const int8_t *logits,
                           uint8_t num_labels,
                           const char *const *label_names);

#ifdef __cplusplus
}
#endif
#endif /* KWS_POSTPROCESS_H */
