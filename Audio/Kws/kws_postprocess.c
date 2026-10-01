/**
 ******************************************************************************
 * @file    kws_postprocess.c
 * @brief   Implementation of KWS decision (segment majority voting).
 ******************************************************************************
 */
#include "kws_postprocess.h"

#include <stdio.h>

void kws_postprocess_init(kws_state_t *s)
{
  s->label = -1;
  s->votes = 0;
  s->peak = 0;
  s->other_run = 0;
  s->len = 0;
  s->rearm = 0;
  s->trigger_cnt = 0;
  s->seg_n = 0;
}

/* Log one frame of the current segment for the finalize dump. */
static void kws_seg_log(kws_state_t *s, int8_t label, int8_t score)
{
  if (s->seg_n <= KWS_MAX_LEN) /* seg_label[] has KWS_MAX_LEN + 1 slots */
  {
    s->seg_label[s->seg_n] = label;
    s->seg_score[s->seg_n] = score;
    s->seg_n++;
  }
}

/* Evidence for 'label' already present in the segment log: its frame count
 * and best score. Used on take-over so that frames of the returning label
 * (e.g. "two 39" ... "zero 48" ... "two 54") keep counting. */
static void kws_seg_prior(const kws_state_t *s, int8_t label,
                          uint8_t *votes, int8_t *peak)
{
  uint8_t v = 0;
  int8_t p = -128;
  for (uint8_t i = 0; i < s->seg_n; i++)
  {
    if (s->seg_label[i] == label)
    {
      v++;
      if (s->seg_score[i] > p)
        p = s->seg_score[i];
    }
  }
  *votes = v;
  *peak = p;
}

/* Score total / average over the candidate label's frames in this segment. */
static void kws_metrics(const kws_state_t *s, int *total, int *avg)
{
  int t = 0;
  for (uint8_t i = 0; i < s->seg_n; i++)
    if (s->seg_label[i] == s->label)
      t += s->seg_score[i];
  *total = t;
  *avg = (s->votes > 0) ? t / (int)s->votes : 0;
}

/* Confirmation gates. Evaluated after EVERY vote (early confirm): the
 * accept set is identical to a close-time check (same thresholds), the
 * result just lands as soon as the word has gathered enough evidence
 * instead of after the KWS_SEG_END close-wait (~0.4 s saved). */
static int kws_confirmed(const kws_state_t *s, int avg)
{
  return (s->label > 1 && s->votes >= KWS_MIN_VOTES &&
          ((s->peak >= KWS_PEAK_MIN && avg >= KWS_AVG_MIN) ||
           s->peak >= KWS_STRONG_SCORE));
}

/* Close the current segment (or confirm it early). Segments with
 * votes >= KWS_MIN_VOTES dump the whole block: one line per slide with its
 * raw result, then the verdict with the actually computed metrics. Junk
 * segments (votes < KWS_MIN_VOTES) can never confirm and only get a
 * one-line ">> drop" summary.
 *
 * Two acceptance paths (score is the hard evidence, votes add robustness):
 *   A) votes >= KWS_MIN_VOTES && peak >= KWS_PEAK_MIN && avg >= KWS_AVG_MIN
 *      (several consistent frames; the average gate rejects noise segments
 *      that luck into one peak of ~35 but cannot sustain a high average)
 *   B) votes >= KWS_MIN_VOTES && peak >= KWS_STRONG_SCORE
 *      (an established segment with a very confident peak skips only the
 *      avg requirement - low-score junk like "seven 30" is still rejected)
 */
static int kws_finalize(kws_state_t *s,
                        const char *const *label_names,
                        uint8_t num_labels)
{
  int hit = 0;
  int total, avg;

  kws_metrics(s, &total, &avg);
  int confirmed = kws_confirmed(s, avg);

  if (!confirmed && s->votes < KWS_MIN_VOTES)
  {
    /* Junk segment: below KWS_MIN_VOTES votes it can never confirm, so
     * skip the per-frame block dump (log spam) and drop one compact line. */
    printf(">> drop: %s | votes=%u peak=%d\r\n",
           (s->label > 1) ? label_names[s->label] : "-", s->votes, s->peak);
  }
  else
  {
    printf("========================================\r\n");
    for (uint8_t i = 0; i < s->seg_n; i++)
    {
      int lb = s->seg_label[i];
      const char *nm = (lb >= 0 && lb < (int)num_labels) ? label_names[lb] : "?";
      /* '*' marks the frames counted for the final candidate. */
      const char *mark = (lb == s->label) ? " *" : "";
      printf("  %2u: %-9s %4d%s\r\n", i, nm, s->seg_score[i], mark);
    }

    if (confirmed)
    {
      printf(">> RESULT: %s | votes=%u peak=%d total=%d avg=%d | OK\r\n",
             label_names[s->label], s->votes, s->peak, total, avg);
      s->trigger_cnt++;
      /* Silence-run re-arm: the utterance tail keeps winning for several
       * frames as the word slides out of the 1 s window; requiring
       * KWS_REARM_SILENCE quiet frames before the next segment prevents a
       * double trigger of the same word. */
      s->rearm = KWS_REARM_SILENCE;
      hit = 1;
    }
    else
    {
      printf(">> RESULT: %s | votes=%u peak=%d total=%d avg=%d | REJECT\r\n",
             (s->label > 1) ? label_names[s->label] : "-",
             s->votes, s->peak, total, avg);
    }
    printf("========================================\r\n");
  }

  s->label = -1;
  s->votes = 0;
  s->peak = 0;
  s->other_run = 0;
  s->len = 0;
  s->seg_n = 0;
  return hit;
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

  /* 2) Silence-run re-arm after a confirmed trigger: a keyword frame
   *    (still the utterance tail sliding out of the window) restarts the
   *    wait; KWS_REARM_SILENCE consecutive non-keyword frames re-arm the
   *    state machine. Prevents a double trigger of the same word. */
  if (s->rearm > 0)
  {
    if (max_idx > 1 && max_val >= KWS_OPEN_MIN)
      s->rearm = KWS_REARM_SILENCE;
    else
      s->rearm--;
    return 0;
  }

  if (s->label >= 0 && max_idx == s->label)
  {
    /* 3a) Matches the candidate: accumulate vote, refresh peak, then check
     *     the gates immediately (early confirm: result lands as soon as
     *     enough evidence exists, not after the SEG_END close-wait). */
    kws_seg_log(s, (int8_t)max_idx, max_val);
    s->votes++;
    if (max_val > s->peak)
      s->peak = max_val;
    s->other_run = 0;
    s->len++;

    int total, avg;
    kws_metrics(s, &total, &avg);
    if (kws_confirmed(s, avg))
      return kws_finalize(s, label_names, num_labels);
  }
  else if (max_idx > 1 && s->label >= 0 &&
           ((s->votes < KWS_MIN_VOTES && max_val >= s->peak) ||
            max_val >= s->peak + KWS_SWITCH_MARGIN))
  {
    /* 2b) Take-over: a candidate that is not yet established (votes <
     *     KWS_MIN_VOTES) is replaced by a different label at least as
     *     strong (mislabeled onset/offset frames); an ESTABLISHED one only
     *     by a newcomer stronger by KWS_SWITCH_MARGIN - a 1-point tail
     *     blip must not steal the accumulated votes. Votes/peak carry over
     *     the frames the new label already had earlier in the segment. */
    uint8_t pv;
    int8_t pp;
    kws_seg_prior(s, (int8_t)max_idx, &pv, &pp);
    kws_seg_log(s, (int8_t)max_idx, max_val);
    s->label = (int8_t)max_idx;
    s->votes = pv + 1;
    s->peak = (max_val > pp) ? max_val : pp;
    s->other_run = 0;
    s->len++;

    int total, avg;
    kws_metrics(s, &total, &avg);
    if (kws_confirmed(s, avg))
      return kws_finalize(s, label_names, num_labels);
  }
  else if (s->label >= 0)
  {
    /* 2c) Non-candidate frame (silence/_unknown_ or a weaker other label):
     *     KWS_SEG_END consecutive ones close the segment. */
    kws_seg_log(s, (int8_t)max_idx, max_val);
    if (++s->other_run >= KWS_SEG_END)
      return kws_finalize(s, label_names, num_labels);
    s->len++;
  }
  else if (max_idx > 1 && max_val >= KWS_OPEN_MIN)
  {
    /* 2d) Idle and a keyword frame arrives: open a new candidate segment.
     *     Frames below KWS_OPEN_MIN never open one: noise murmur scores
     *     20-27 and would otherwise create junk segments that lock the
     *     state machine for KWS_MAX_LEN frames; a real word's rising edge
     *     crosses the gate within 1-2 frames, so it still opens on time. */
    kws_seg_log(s, (int8_t)max_idx, max_val);
    s->label = (int8_t)max_idx;
    s->votes = 1;
    s->peak = max_val;
    s->other_run = 0;
    s->len = 1;
  }
  /* else: idle + silence/_unknown_/weak frame -> ignore. */

  /* 3) Segment length cap: force a decision during continuous speech. */
  if (s->label >= 0 && s->len >= KWS_MAX_LEN)
    return kws_finalize(s, label_names, num_labels);

  return 0;
}
