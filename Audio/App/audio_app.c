/**
 ******************************************************************************
 * @file    audio_app.c
 * @brief   VAD-gated single-shot KWS: SAI capture (x2 input gain applied in
 *          the SAI IRQ) -> ring buffer -> energy VAD -> 1 s utterance
 *          capture -> full mel frontend -> echodscnn inference ->
 *          single-shot print (USART1).
 *
 * Data flow (16 kHz, mono, int16, x2-amplified in the SAI IRQ):
 *   SAI1_B DMA (10 ms half-blocks, IRQ) -> VAD + ring buffer
 *   -> main loop: consume 10 ms blocks, watch the VAD level; on a rising
 *      edge (OFF -> ON) capture audio into a 1 s buffer,
 *      then run ONE inference on the full 1 s window:
 *      mel (49x40 int8) -> echodscnn (int8 1960 in, 14 logits out)
 *      -> argmax + margin print.
 *
 * The model got too big for the 160 ms sliding-window cadence, so inference
 * now happens once per detected utterance instead of continuously; while it
 * runs the IRQ keeps VAD/ring alive (ring overrun guard drops audio, see
 * audio_capture_ring_buff.c). Words shorter than 1 s keep their real
 * silence tail in the window; words longer than 1 s are truncated at 1 s.
 * A capture whose VAD-active part is < VAD_MIN_SPEECH_SAMPLES is a noise
 * blip and is discarded without inference.
 *
 * Label order matches the training label_map (12 Chinese keywords),
 * index printed for cross-checking against the model output.
 *
 * Low-power: audio capture must not be suspended, so Stop/Off modes are kept
 * disabled via UTIL_LPM. The BLE stack re-enables OFF mode in
 * APPE_SysEvtReadyProcessing (app_entry.c), which runs after AudioApp_Init,
 * therefore the disable is re-asserted on every Process call (idempotent).
 ******************************************************************************
 */
#include "audio_app.h"
#include "audio_stream.h"
#include "audio_vad.h"
#include "audio_frontend.h"
#include "audio_params.h"
#include "echodscnn.h"
#include "echodscnn_data_params.h"
#include "main.h"
#include "app_conf.h"
#include "stm32_lpm.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ---- AI model: echodscnn 14-label classifier -------------------------------
 * Label order matches the model output (training label_map): 0=_silence_,
 * 1=_unknown_, then the 12 keywords. Must match AI_ECHODSCNN_OUT_1_SIZE (14). */
#define NUM_LABELS 14

static const char *const kLabelNames[NUM_LABELS] = {
    "_silence_", "_unknown_",
    "上一首", "下一首",
    "停止播放", "关闭降噪",
    "开启降噪", "小艺小艺",
    "微信支付", "挂断电话",
    "接听电话", "播放音乐",
    "调大音量", "调小音量"};

/* Activations buffer (io buffers are allocated inside it). */
AI_ALIGNED(32)
static uint8_t pool0[AI_ECHODSCNN_DATA_ACTIVATION_1_SIZE];

static ai_handle s_net = AI_HANDLE_NULL;
static ai_buffer *s_ai_input;
static ai_buffer *s_ai_output;

/* IO data pointers (inside pool0: io buffers are allocated in activations). */
static ai_i8 *s_data_ins[AI_ECHODSCNN_IN_NUM];
static ai_i8 *s_data_outs[AI_ECHODSCNN_OUT_NUM];

/* Frontend context: one 1 s clip -> 49x40 int8 features.
 * noise_estimate persists across utterances (ambient tracking). */
static AudioFrontend s_fe;
static int8_t s_features[SPECTROGRAM_LENGTH * NUM_CHANNELS];

/* 1-second utterance capture buffer (already x2-amplified by audio_stream). */
static int16_t s_capture[DESIRED_SAMPLES];

/* Scratch for the consume_no_cpy wrap fallback (never taken with the
 * current all-160-sample-aligned ring geometry). */
static int16_t s_blk[AS_DMA_HALF_FRAMES];

/* ---- Pre-roll idle history -------------------------------------------------
 * Keyword onsets are fricatives (上一首 "shàng", 下一首 "xià"): x/sh noise
 * energy sits far below the VAD open threshold, so the VAD only opens at
 * the vowel - the decisive FIRST syllable then lies BEFORE the capture
 * window and the two words differ only in their truncated remnant
 * (observed margins of 2-3). Keep the last 250 ms of idle audio in a
 * rolling buffer and PREPEND it to the capture on the rising edge,
 * recovering the fricative onset. Training places keywords at random
 * window positions, so the shifted layout stays in-distribution. */
#define PREROLL_SAMPLES (SAMPLE_RATE / 4) /* 250 ms */
static int16_t s_preroll[PREROLL_SAMPLES];
static uint32_t s_preroll_w;    /* write index = oldest sample once full */
static uint32_t s_preroll_fill; /* valid history samples, 0..PREROLL_SAMPLES */

/* ---- Utterance state machine ------------------------------------------------
 * PH_IDLE    : drain the ring, watch VAD level; rising edge -> capture.
 * PH_CAPTURE : append 10 ms blocks (x2 gain) until 1 s is collected, then
 *              run ONE inference inline and return to idle. If VAD closed
 *              with almost no active speech -> noise blip -> discard. */
typedef enum
{
  PH_IDLE = 0,
  PH_CAPTURE
} phase_t;
static phase_t s_phase;
static uint8_t s_vad_prev;         /* VAD level at the previous poll (edge detect) */
static uint32_t s_cap_len;         /* samples captured in s_capture               */
static uint32_t s_speech_len;      /* captured samples that were VAD-loud          */
static uint32_t s_peak_e;          /* peak block energy inside the capture (tuning) */
static uint32_t s_drop_base;       /* ring droppedSamples at capture start          */
static uint32_t s_last_block_tick; /* HAL_GetTick() of last consumed block     */
static uint32_t s_vad_on_tick;     /* HAL_GetTick() of the VAD ON edge (trace)  */

/* Minimum VAD-active audio for the inference to run; a shorter active span
 * is a noise blip (e.g. a click) and the capture is discarded. */
#define VAD_MIN_SPEECH_SAMPLES (SAMPLE_RATE / 5) /* 200 ms */

/* Single-shot decision gate: a keyword argmax (>1) is only reported as RESULT
 * when BOTH the score and the top1-top2 margin clear these floors; a keyword
 * below them prints as [KWS] weak (observable, no action). Measured on-device:
 * real words score 13-79 / margin 2-67, while a noise RESULT scored 3/10, so
 * score>=20 kills it; margin>=5 keeps 下一首 33/10 and drops its 17/3 misses
 * to weak. Raise both after collecting more logs. */
#define KWS_MIN_SCORE 20
#define KWS_MIN_MARGIN 5

/* Audio stall watchdog: on a SAI error the HAL aborts the DMA SILENTLY
 * (empty weak HAL_SAI_ErrorCallback) and the ring stops filling forever
 * -> VAD frozen, nothing ever prints again. Blocks must arrive every
 * 10 ms; after 1.5 s of silence restart the whole audio pipeline. */
#define AUDIO_STALL_TIMEOUT_MS 1500

static void ai_log_err(const ai_error err, const char *fct)
{
  if (fct)
    printf("AI - Error (%s) - type=0x%02x code=0x%02x\r\n", fct,
           err.type, err.code);
  else
    printf("AI - Error - type=0x%02x code=0x%02x\r\n", err.type, err.code);
}

/* Create and initialize the network instance. */
static int ai_boostrap(void)
{
  ai_error err;

  err = ai_echodscnn_create_and_init(&s_net, (ai_handle[]){pool0}, NULL);
  if (err.type != AI_ERROR_NONE)
  {
    ai_log_err(err, "ai_echodscnn_create_and_init");
    return -1;
  }

  s_ai_input = ai_echodscnn_inputs_get(s_net, NULL);
  s_ai_output = ai_echodscnn_outputs_get(s_net, NULL);

  for (int idx = 0; idx < AI_ECHODSCNN_IN_NUM; idx++)
    s_data_ins[idx] = (ai_i8 *)s_ai_input[idx].data;
  for (int idx = 0; idx < AI_ECHODSCNN_OUT_NUM; idx++)
    s_data_outs[idx] = (ai_i8 *)s_ai_output[idx].data;

  return 0;
}

static int ai_run(void)
{
  ai_i32 batch;

  batch = ai_echodscnn_run(s_net, s_ai_input, s_ai_output);
  if (batch != 1)
  {
    ai_log_err(ai_echodscnn_get_error(s_net), "ai_echodscnn_run");
    return -1;
  }

  return 0;
}

/* Consume one 10 ms DMA block from the ring (zero-copy when possible).
 * Returns NULL when the ring does not have a full block yet. */
static const int16_t *consume_block(void)
{
  AudioCaptureRingBuff_t *ring = AudioStream_get_ring();
  if ((int32_t)ring->availableSamples < (int32_t)AS_DMA_HALF_FRAMES)
    return NULL;

  const int16_t *blk = (const int16_t *)AudioCaptureRingBuff_consume_no_cpy(
      ring, AS_DMA_HALF_FRAMES);
  if (blk)
    return blk;
  AudioCaptureRingBuff_consume((uint8_t *)s_blk, ring, AS_DMA_HALF_FRAMES);
  return s_blk;
}

/* Append n samples to the capture buffer. Audio is already x2-amplified
 * upstream in audio_stream.c (SAI IRQ), no further gain here. */
static void capture_append(const int16_t *src, uint32_t n)
{
  uint32_t room = (uint32_t)DESIRED_SAMPLES - s_cap_len;
  if (n > room)
    n = room;
  memcpy(s_capture + s_cap_len, src, (size_t)n * sizeof(int16_t));
  s_cap_len += n;
}

/* Rolling idle history: remember the newest audio so the capture window
 * can be extended backwards across the fricative onset on VAD open. */
static void preroll_write(const int16_t *src, uint32_t n)
{
  uint32_t total = n;
  while (n)
  {
    uint32_t room = PREROLL_SAMPLES - s_preroll_w;
    uint32_t k = (n < room) ? n : room;
    memcpy(s_preroll + s_preroll_w, src, (size_t)k * sizeof(int16_t));
    s_preroll_w += k;
    if (s_preroll_w == PREROLL_SAMPLES)
      s_preroll_w = 0;
    src += k;
    n -= k;
  }
  if (s_preroll_fill < PREROLL_SAMPLES)
  {
    s_preroll_fill += total;
    if (s_preroll_fill > PREROLL_SAMPLES)
      s_preroll_fill = PREROLL_SAMPLES;
  }
}

/* Drop the history (stale once a capture consumed the stream): re-anchors
 * fill/write so the next idle samples start from index 0. */
static void preroll_reset(void)
{
  s_preroll_w = 0;
  s_preroll_fill = 0;
}

/* Full 1 s clip -> 49x40 features -> one inference -> single-shot print:
 * argmax over all 14 labels, margin = top1 - top2 (all classes included).
 * One line per detected utterance, for threshold tuning. */
static void run_inference(void)
{
  audio_frontend_process_clip(&s_fe, s_capture, DESIRED_SAMPLES, s_features);
  memcpy(s_data_ins[0], s_features, AI_ECHODSCNN_IN_1_SIZE_BYTES);

  if (ai_run() != 0)
    return;

  const int8_t *out = (const int8_t *)s_data_outs[0];
  int max_i = 0;
  for (int i = 1; i < NUM_LABELS; i++)
    if (out[i] > out[max_i])
      max_i = i;
  int sec_i = (max_i == 0) ? 1 : 0;
  for (int i = 0; i < NUM_LABELS; i++)
    if (i != max_i && out[i] > out[sec_i])
      sec_i = i;
  int margin = out[max_i] - out[sec_i];
  uint32_t speech_ms = s_speech_len * 1000UL / SAMPLE_RATE;
  /* Samples the ring dropped while the main loop was blocked (capture +
   * inference time): 160 samples = 10 ms of lost audio. */
  uint32_t dropped = AudioStream_get_ring()->droppedSamples - s_drop_base;

  if (max_i > 1 && out[max_i] >= KWS_MIN_SCORE && margin >= KWS_MIN_MARGIN)
    printf("[KWS] RESULT #%-2d %-8s score=%d margin=%d speech=%lums peak_e=%lu drop=%lu\r\n",
           max_i, kLabelNames[max_i], out[max_i], margin,
           (unsigned long)speech_ms, (unsigned long)s_peak_e,
           (unsigned long)dropped);
  else if (max_i > 1)
    printf("[KWS] weak   #%-2d %-8s score=%d margin=%d speech=%lums peak_e=%lu drop=%lu\r\n",
           max_i, kLabelNames[max_i], out[max_i], margin,
           (unsigned long)speech_ms, (unsigned long)s_peak_e,
           (unsigned long)dropped);
  else
    printf("[KWS] reject  #%-2d %-9s score=%d margin=%d speech=%lums peak_e=%lu drop=%lu\r\n",
           max_i, kLabelNames[max_i], out[max_i], margin,
           (unsigned long)speech_ms, (unsigned long)s_peak_e,
           (unsigned long)dropped);

  /* Complete model output: one line per class, score-descending. */
  int order[NUM_LABELS];
  for (int i = 0; i < NUM_LABELS; i++)
    order[i] = i;
  for (int i = 1; i < NUM_LABELS; i++) /* insertion sort, descending */
  {
    int key = order[i], j = i - 1;
    while (j >= 0 && out[order[j]] < out[key])
    {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = key;
  }
  for (int i = 0; i < NUM_LABELS; i++)
    printf("   #%-2d %-12s %d\r\n",
           order[i], kLabelNames[order[i]], out[order[i]]);
}

void AudioApp_Init(void)
{
  printf("\r\n=== micro_speech VAD single-shot ===\r\n");

  ai_boostrap();

  /* Frontend for full 1 s clips (noise_estimate persists between clips). */
  audio_frontend_init(&s_fe);

  /* AudioStream_init resets the ring and the SAI-IRQ VAD. */
  AudioStream_init();
  AudioStream_start();

  s_phase = PH_IDLE;
  s_vad_prev = 0;
  s_cap_len = 0;
  s_speech_len = 0;
  s_peak_e = 0;
  s_drop_base = 0;
  s_last_block_tick = HAL_GetTick();
}

void AudioApp_Process(void)
{
  /* Keep Stop/Off modes disabled: SAI audio DMA must keep running.
   * Re-asserted here because APPE_SysEvtReadyProcessing re-enables OFF. */
  UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
  UTIL_LPM_SetOffMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);

  if (!s_net)
    return;

  for (;;)
  {
    /* 1 - take one 10 ms block from the ring (NULL = wait for DMA). */
    const int16_t *blk = consume_block();
    if (!blk)
    {
      /* Watchdog: SAI DMA death (HAL aborts on error, silently) leaves the
       * ring empty forever. Restart the pipeline and start over. */
      uint32_t now = HAL_GetTick();
      if ((uint32_t)(now - s_last_block_tick) > AUDIO_STALL_TIMEOUT_MS)
      {
        printf("[SAI] stalled %lums err=0x%02lX -> restart\r\n",
               (unsigned long)(now - s_last_block_tick),
               (unsigned long)AudioStream_get_sai_error());
        AudioStream_init();  /* ring reset + VAD reset */
        AudioStream_start(); /* restart SAI DMA                  */
        s_last_block_tick = HAL_GetTick();
        s_phase = PH_IDLE;
        s_vad_prev = 0;
        preroll_reset();
      }

      return;
    }
    s_last_block_tick = HAL_GetTick();

    uint8_t act = audio_vad_active();

    /* 2 - idle: watch the VAD level, start capture on a rising edge. */
    if (s_phase == PH_IDLE)
    {
      uint8_t prev = s_vad_prev;
      s_vad_prev = act;

      if (act && !prev)
      {
        printf("[VAD] ON  e=%lu floor=%lu\r\n",
               (unsigned long)audio_vad_energy(),
               (unsigned long)audio_vad_floor());
        s_vad_on_tick = HAL_GetTick();
        s_phase = PH_CAPTURE;
        s_cap_len = 0;
        s_speech_len = 0;
        s_peak_e = 0;
        s_drop_base = AudioStream_get_ring()->droppedSamples;
        /* Prepend the idle history, oldest sample first: the window becomes
         * [history | trigger block | following blocks] up to 1 s, so the
         * fricative onset that failed to open the VAD is included. */
        {
          uint32_t pre_n = (s_preroll_fill < (uint32_t)DESIRED_SAMPLES)
                               ? s_preroll_fill
                               : (uint32_t)DESIRED_SAMPLES;
          uint32_t start = (s_preroll_fill >= PREROLL_SAMPLES)
                               ? s_preroll_w
                               : 0;
          uint32_t first = PREROLL_SAMPLES - start;
          if (first > pre_n)
            first = pre_n;
          memcpy(s_capture + s_cap_len, s_preroll + start,
                 (size_t)first * sizeof(int16_t));
          memcpy(s_capture + s_cap_len + first, s_preroll,
                 (size_t)(pre_n - first) * sizeof(int16_t));
          s_cap_len += pre_n;
        }
      }
      else if (!act && prev)
      {
        printf("[VAD] OFF active=%lums\r\n",
               (unsigned long)(HAL_GetTick() - s_vad_on_tick));
        continue; /* falling edge never feeds the capture */
      }
      else
      {
        preroll_write(blk, AS_DMA_HALF_FRAMES);
        continue; /* still idle - keep the pre-roll history rolling */
      }
    }

    /* 3 - capture: append this block (the trigger block included).
     * Speech content counts only blocks that were truly loud (above the VAD
     * hold threshold), so the 300 ms hangover tail is not counted.
     * Peak block energy is tracked for VAD threshold tuning. */
    {
      uint32_t en = audio_vad_energy();
      if (en > s_peak_e)
        s_peak_e = en;
    }
    capture_append(blk, AS_DMA_HALF_FRAMES);
    if (audio_vad_loud())
      s_speech_len += AS_DMA_HALF_FRAMES;
    else if (!act && s_speech_len < VAD_MIN_SPEECH_SAMPLES)
    {
      /* VAD already closed and almost no active speech: noise blip. */
      printf(">> blip speech=%lums peak_e=%lu discarded\r\n",
             (unsigned long)(s_speech_len * 1000UL / SAMPLE_RATE),
             (unsigned long)s_peak_e);
      s_phase = PH_IDLE;
      preroll_reset(); /* history predates the discarded capture: stale */
      continue;
    }

    /* 4 - 1 s collected: run ONE inference on the whole window. */
    if (s_cap_len >= DESIRED_SAMPLES)
    {
      run_inference();
      s_phase = PH_IDLE;
      preroll_reset(); /* history predates this capture: stale */
      /* May still be the utterance tail (or a follow-up word): only a
       * fresh OFF -> ON edge starts a new capture. */
      s_vad_prev = audio_vad_active();
      if (!s_vad_prev)
        /* VAD already closed while the loop was blocked in capture +
         * inference: the falling edge was swallowed, close the trace. */
        printf("[VAD] OFF active=%lums\r\n",
               (unsigned long)(HAL_GetTick() - s_vad_on_tick));
    }
  }
}
