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
  s->cooldown = 0;
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

/* Close the current segment. Dump the whole block: one line per slide with
 * its raw result, then the verdict with the actually computed metrics.
 *
 * Two acceptance paths (score is the hard evidence, votes add robustness):
 *   A) votes >= KWS_MIN_VOTES && peak >= KWS_PEAK_MIN   (several consistent
 *      frames, even if each is only moderately confident)
 *   B) peak >= KWS_STRONG_SCORE                          (a single very
 *      confident frame is valid on its own - e.g. "zero 47"; low-score
 *      isolated frames like "seven 34" are still rejected)
 */
static int kws_finalize(kws_state_t *s,
                        const char *const *label_names,
                        uint8_t num_labels)
{
  int hit = 0;
  int total = 0;
  int avg = 0;
  int confirmed;

  /* Weighted total: sum of the winning label's scores in this segment. */
  for (uint8_t i = 0; i < s->seg_n; i++)
    if (s->seg_label[i] == s->label)
      total += s->seg_score[i];
  if (s->votes > 0)
    avg = total / s->votes;

  confirmed = (s->label > 1 &&
               ((s->votes >= KWS_MIN_VOTES && s->peak >= KWS_PEAK_MIN) ||
                s->peak >= KWS_STRONG_SCORE));

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
    s->cooldown = KWS_TAIL_COOLDOWN;
    hit = 1;
  }
  else
  {
    printf(">> RESULT: %s | votes=%u peak=%d total=%d avg=%d | REJECT\r\n",
           (s->label > 1) ? label_names[s->label] : "-",
           s->votes, s->peak, total, avg);
  }
  printf("========================================\r\n");

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
  /* 0) Swallow tail frame(s) right after a confirmed trigger. */
  if (s->cooldown > 0)
  {
    s->cooldown--;
    return 0;
  }

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

  if (s->label >= 0 && max_idx == s->label)
  {
    /* 2a) Matches the candidate: accumulate vote, refresh peak. */
    kws_seg_log(s, (int8_t)max_idx, max_val);
    s->votes++;
    if (max_val > s->peak)
      s->peak = max_val;
    s->other_run = 0;
    s->len++;
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
  else if (max_idx > 1)
  {
    /* 2d) Idle and a keyword frame arrives: open a new candidate segment. */
    kws_seg_log(s, (int8_t)max_idx, max_val);
    s->label = (int8_t)max_idx;
    s->votes = 1;
    s->peak = max_val;
    s->other_run = 0;
    s->len = 1;
  }
  /* else: idle + silence/_unknown_ -> ignore. */

  /* 3) Segment length cap: force a decision during continuous speech. */
  if (s->label >= 0 && s->len >= KWS_MAX_LEN)
    return kws_finalize(s, label_names, num_labels);

  return 0;
}
