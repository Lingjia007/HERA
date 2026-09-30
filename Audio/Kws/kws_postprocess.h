/**
 ******************************************************************************
 * @file    kws_postprocess.h
 * @brief   Keyword Spotting (KWS) decision for streaming micro_speech.
 *
 * Turns per-frame argmax results (one every 200 ms) into ONE confirmed
 * keyword event per utterance, using segment-based majority voting instead
 * of an N-consecutive-frame debounce:
 *
 *   1) A keyword frame (argmax not _silence_/_unknown_) scoring at least
 *      KWS_OPEN_MIN opens a candidate segment; further keyword frames
 *      extend it (votes++, peak = max score). Weak frames below
 *      KWS_OPEN_MIN never open a segment (noise murmur suppression).
 *   2) A DIFFERENT label takes over the candidate only if the candidate is
 *      NOT yet established (votes < KWS_MIN_VOTES) and is at least as
 *      strong (handles mislabeled onset/offset frames, e.g. "four 54"
 *      before "one"), or the newcomer is stronger by KWS_SWITCH_MARGIN.
 *      An established candidate cannot be stolen by a 1-point tail blip
 *      (e.g. "seven 36" stealing a locked "eight" at peak 35).
 *   3) The segment ends after KWS_SEG_END consecutive non-candidate frames
 *      (silence, _unknown_, or weaker other labels).
 *   4) At segment end the word is confirmed iff
 *         votes >= KWS_MIN_VOTES
 *         && ((peak >= KWS_PEAK_MIN && avg >= KWS_AVG_MIN)
 *             || peak >= KWS_STRONG_SCORE)
 *      Segments with fewer than KWS_MIN_VOTES votes can never confirm:
 *      they are noise fragments (e.g. single-frame "zero 34" / "seven 30")
 *      and are dropped with one compact ">> drop" line instead of a full
 *      block dump.
 *      A confirmed word is printed exactly once, then KWS_TAIL_COOLDOWN
 *      frame(s) are swallowed to prevent a second trigger from the same
 *      utterance.
 *   5) Every segment that could possibly confirm (votes >= KWS_MIN_VOTES)
 *      is dumped as a "====" delimited block: one line per slide with its
 *      raw argmax + score (including intruder/gap frames), then the verdict
 *      line with the actually computed votes / peak / score total / average
 *      for tuning. Junk segments (votes < KWS_MIN_VOTES) only get the
 *      one-line ">> drop" summary.
 *
 * Empirical basis (on-device 200 ms slide log): a spoken word spans 4-6
 * frames with peak scores 47-78; edge frames score 19-45 and are often
 * mislabeled; noise fragments stay in single-frame segments with peak
 * 27-34 and are discarded by the KWS_MIN_VOTES gate.
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
 *   KWS_MIN_VOTES     : same-label frames required to confirm at all -
 *                       segments below this are junk and get a one-line
 *                       ">> drop" summary instead of a full block dump.
 *                       Real words span 4-6 frames at the 200 ms cadence,
 *                       so 5 still admits normal utterances; lower to 4
 *                       only if short words start being missed.
 *   KWS_PEAK_MIN      : peak int8 score required for the multi-frame path
 *   KWS_AVG_MIN       : average score of the candidate's frames required for
 *                       the multi-frame path - noise segments can luck into a
 *                       single peak of 34-35 but keep avg <= 30, real words
 *                       sustain avg >= 36, so the average is a second,
 *                       largely independent barrier
 *   KWS_OPEN_MIN      : minimum score for a frame to OPEN a new segment.
 *                       Noise murmur scores 20-27 and would otherwise open
 *                       junk segments that lock the state machine for ~2 s;
 *                       a real word's rising edge crosses 28 within 1-2
 *                       frames, so its segment still opens on time
 *   KWS_STRONG_SCORE  : peak that replaces the avg requirement for an
 *                       already-established segment (still needs
 *                       KWS_MIN_VOTES votes)
 *   KWS_SWITCH_MARGIN : score advantage needed to take over an established
 *                       candidate (votes >= KWS_MIN_VOTES)
 *   KWS_SEG_END       : consecutive non-candidate frames that close a segment
 *   KWS_MAX_LEN       : force segment close after this many frames (~2 s)
 *   KWS_TAIL_COOLDOWN : frames swallowed right after a confirmed trigger    */
#define KWS_MIN_VOTES 5
#define KWS_PEAK_MIN 35
#define KWS_AVG_MIN 32
#define KWS_OPEN_MIN 28
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
