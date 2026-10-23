/**
 ******************************************************************************
 * @file    audio_app.c
 * @brief   ZPL-gated single-shot KWS: the VM3011 ZPL wake line (DOUT) opens
 *          a listening session -> SAI PDM capture + PDM2PCM in the SAI IRQ
 *          -> ring buffer -> energy VAD -> 1 s utterance capture -> full
 *          mel frontend -> echodscnn inference -> single-shot print
 *          (USART1). When the session goes quiet, the SAI stops, DOUT is
 *          cleared and the MCU returns to STOP2 until the next ZPL wake.
 *
 * Power state machine (CFG_LPM_SUPPORTED = 1):
 *   session closed : SAI stopped, mic in ZPL. STOP2 allowed via
 *                    UTIL_LPM (OFF mode permanently vetoed - standby would
 *                    drop SRAM). DOUT (PA0, EXTI0 + PWR WKUP1) wakes the
 *                    MCU; main() forwards the event to AudioApp_ZplNotify.
 *   session open   : STOP2 vetoed (SAI DMA must run) -> the sequencer idles
 *                    in SLEEP between 10 ms DMA IRQs. VAD-confirmed speech
 *                    is captured and classified as before; quiet keeps the
 *                    session alive only until the stay-awake window expires.
 *
 * Data flow while a session is open (16 kHz, mono, int16, 24 dB PDM gain,
 * x128 decimation):
 *   SAI1_A PDM DMA (10 ms half-blocks, IRQ) -> PDM2PCM -> VAD + ring buffer
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
#include "wav_recorder.h"
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

/* Capture span ceiling: the capture keeps following the utterance until
 * the VAD closes (or this cap), then the model's fixed 1 s window is
 * cropped out. 1.5 s covers the 500 ms preroll + a ~1 s keyword + the
 * 300 ms VAD hangover tail with headroom. */
#define CAPTURE_MAX_SAMPLES (SAMPLE_RATE + (SAMPLE_RATE / 2))

/* 1-second utterance capture span. The capture FOLLOWS the utterance
 * (up to CAPTURE_MAX_SAMPLES below); the fixed 1 s model window is cropped
 * out afterwards, anchored to the utterance end (capture_finish). */
static int16_t s_capture[CAPTURE_MAX_SAMPLES];

/* Scratch for the consume_no_cpy wrap fallback (never taken with the
 * current all-160-sample-aligned ring geometry). */
static int16_t s_blk[AS_DMA_HALF_FRAMES];

/* ---- Pre-roll idle history -------------------------------------------------
 * Keyword onsets are fricatives (上一首 "shàng", 下一首 "xià"): x/sh noise
 * energy sits far below the VAD open threshold, so the VAD only opens at
 * the vowel - the decisive FIRST syllable then lies BEFORE the capture
 * window and the two words differ only in their truncated remnant
 * (observed margins of 2-3). Keep the last 500 ms of idle audio in a
 * rolling buffer and PREPEND it to the capture on the rising edge,
 * recovering the fricative onset: 250 ms proved too short for a mid-word
 * VAD open (observed speech spans ~520 ms for ~900 ms keywords). Training
 * places keywords at random window positions, so the shifted layout stays
 * in-distribution. */
#define PREROLL_SAMPLES (SAMPLE_RATE / 2) /* 500 ms */
static int16_t s_preroll[PREROLL_SAMPLES];
static uint32_t s_preroll_w;    /* write index = oldest sample once full */
static uint32_t s_preroll_fill; /* valid history samples, 0..PREROLL_SAMPLES */

/* ---- Utterance state machine ------------------------------------------------
 * PH_IDLE    : drain the ring, watch VAD level; rising edge -> capture.
 * PH_CAPTURE : append 10 ms blocks while the VAD stays active, up to
 *              CAPTURE_MAX_SAMPLES; when the VAD closes (hangover included)
 *              with real speech inside -> crop the fixed 1 s window
 *              anchored to the utterance end (capture_finish) -> run ONE
 *              inference -> idle. VAD closed with almost no speech ->
 *              noise blip -> discard. */
typedef enum
{
  PH_IDLE = 0,
  PH_CAPTURE
} phase_t;
static phase_t s_phase;
static uint8_t s_vad_prev;         /* VAD level at the previous poll (edge detect) */
static uint32_t s_cap_len;         /* samples captured in s_capture               */
static uint32_t s_speech_len;      /* captured samples that were VAD-loud          */
static uint32_t s_loud_end;        /* cap_len just past the last VAD-loud block    */
static uint32_t s_peak_e;          /* peak block energy inside the capture (tuning) */
static uint32_t s_drop_base;       /* ring droppedSamples at capture start          */
static uint32_t s_last_block_tick; /* HAL_GetTick() of last consumed block     */
static uint32_t s_vad_on_tick;     /* HAL_GetTick() of the VAD ON edge (trace)  */
static uint8_t s_skip_blocks;      /* mic warm-up blocks to drop after stream start */

/* Minimum VAD-loud audio for the inference to run; a shorter active span
 * is a noise blip (e.g. a click) and the capture is discarded. 200 ms
 * threw away real DISTANT words: their faint tails drop under the hold
 * threshold so few blocks count as loud. 120 ms keeps them - a click
 * is well under 50 ms of loud audio. */
#define VAD_MIN_SPEECH_SAMPLES ((SAMPLE_RATE * 12U) / 100U) /* 120 ms */

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

/* ---- ZPL listen session ----------------------------------------------------
 * The VM3011 (in ZPL mode: PDM clock stopped) raises DOUT when the acoustic
 * input exceeds the configured margin over ambient -> EXTI0 / PWR WKUP1 wake
 * -> main() calls AudioApp_ZplNotify. A session: start the SAI stream (mic
 * leaves ZPL), watch for VAD-confirmed speech, run the existing capture +
 * single-shot inference, then - once it stays quiet - stop the stream (mic
 * back to ZPL), clear DOUT (re-arm the wake line) and allow STOP2 again. */
#define ZPL_LISTEN_WINDOW_MS 3000U /* quiet budget after a ZPL wake (noise) */
#define ZPL_STAY_AWAKE_MS     800U /* extend this much while speech is on   */

static VM3011_Dev_t *s_vm;      /* mic handle for the DOUT re-arm (main owns) */
static uint32_t s_awake_until;  /* HAL_GetTick deadline; 0 = session closed   */
static uint8_t s_kws_enabled;       /* 0 = KWS/VAD off (record debug mode)    */
static uint32_t s_hold_awake_until; /* debug hold-awake deadline (CLI alive)   */

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

/* Append n samples to the capture buffer. Gain is already applied by the
 * PDM2PCM filter (24 dB) in audio_stream.c (SAI IRQ), no gain here. */
static void capture_append(const int16_t *src, uint32_t n)
{
  uint32_t room = (uint32_t)CAPTURE_MAX_SAMPLES - s_cap_len;
  if (n > room)
    n = room;
  memcpy(s_capture + s_cap_len, src, (size_t)n * sizeof(int16_t));
  s_cap_len += n;
}

/* Crop the model's FIXED 1 s window out of the captured span, then run the
 * inference on it. The window is anchored to the utterance END:
 *   end = (last VAD-loud block) + 150 ms tail, clamped to the capture,
 * so the keyword tail is NEVER truncated no matter where the trigger
 * opened - only lead-in silence ahead of the keyword gets cut. If the
 * whole capture is shorter than 1 s, it is right-aligned and the missing
 * head is zero-padded (leading silence is in-distribution for the model).
 * Result: a constant window layout [lead-in | keyword | ~150 ms tail]. */
static void run_inference(void); /* (defined after the preroll helpers) */
static void capture_finish(void)
{
  uint32_t tail = (SAMPLE_RATE * 3U) / 20U; /* 150 ms trailing quiet */
  uint32_t end = s_loud_end + tail;
  if (end < (uint32_t)DESIRED_SAMPLES)
    end = (uint32_t)DESIRED_SAMPLES; /* always feed a full 1 s window */
  if (end > s_cap_len)
    end = s_cap_len; /* clamp to what was actually captured */

  if (s_cap_len <= (uint32_t)DESIRED_SAMPLES)
  {
    /* Shorter than 1 s: right-align to the window end, zero-pad the head. */
    uint32_t shift = (uint32_t)DESIRED_SAMPLES - s_cap_len;
    memmove(s_capture + shift, s_capture, (size_t)s_cap_len * sizeof(int16_t));
    memset(s_capture, 0, (size_t)shift * sizeof(int16_t));
  }
  else
  {
    /* Window fully inside: slide [end - 1 s, end) down to the buffer start. */
    memmove(s_capture, s_capture + (end - (uint32_t)DESIRED_SAMPLES),
            (size_t)DESIRED_SAMPLES * sizeof(int16_t));
  }
  run_inference();
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

void AudioApp_Init(VM3011_Dev_t *vm)
{
  printf("\r\n=== micro_speech ZPL wake single-shot ===\r\n");

  s_vm = vm;

  ai_boostrap();

  /* Frontend for full 1 s clips (noise_estimate persists between clips). */
  audio_frontend_init(&s_fe);

  /* AudioStream_init sets up the ring + PDM filter + VAD. */
  AudioStream_init();
#if AUDIO_ZPL_WAKE
  /* The stream is NOT started: the first ZPL wake (AudioApp_ZplNotify)
   * opens it. */
#else
  /* 连续采集调试模式：立即开流，SAI 常开。 */
  AudioStream_start();
  s_skip_blocks = 3U; /* drop the mic wake transient */
#endif

  s_phase = PH_IDLE;
  s_vad_prev = 0;
  s_cap_len = 0;
  s_speech_len = 0;
  s_peak_e = 0;
  s_drop_base = 0;
  s_last_block_tick = HAL_GetTick();
  s_awake_until = 0U;
}

void AudioApp_ZplNotify(void)
{
#if AUDIO_ZPL_WAKE
  uint32_t now = HAL_GetTick();

  if (s_awake_until == 0U)
  {
    /* Fresh wake from STOP2: open a listening session with a clean pipeline.
     * The mic wakes from ZPL as soon as the PDM clock restarts. */
    printf("[ZPL] wake DOUT=%d -> listen %lus\r\n",
           (int)HAL_GPIO_ReadPin(ZPL_GPIO_Port, ZPL_Pin),
           (unsigned long)(ZPL_LISTEN_WINDOW_MS / 1000U));
    AudioStream_init(); /* ring + VAD + PDM filter reset */
    AudioStream_start();
    s_skip_blocks = 3U; /* drop the mic wake transient (~30 ms) */
    s_phase = PH_IDLE;
    s_vad_prev = 0;
    s_last_block_tick = now;
    preroll_reset();
  }
  else
  {
    /* Already listening: a re-asserted DOUT (sustained sound) only
     * refreshes the stay-awake window. */
    printf("[ZPL] re-arm while listening\r\n");
  }

  s_awake_until = now + ZPL_LISTEN_WINDOW_MS;
#else
  /* 调试模式（连续采集）：DOUT 事件只记录，采集状态不变。可用于观察
   * ZPL 触发时机与语音的实际对应关系。 */
  printf("[ZPL] DOUT evt (ZPL wake disabled - continuous capture)\r\n");
#endif
}

#if AUDIO_ZPL_WAKE
/* Close the session: stop the PDM clock (mic -> ZPL), clear the DOUT
 * indication so a new acoustic event re-arms the wake line, then let the
 * MCU drop back to STOP2 (AudioApp_Process re-enables it). */
static void zpl_sleep(void)
{
  printf("[ZPL] sleep\r\n");
  AudioStream_stop();
  /* Settle time for the ZPL re-entry after the PDM clock stops. */
  HAL_Delay(20U);
  if (s_vm != NULL)
    (void)VM3011_ClearDout(s_vm);
  s_awake_until = 0U;
}
#endif

void AudioApp_SetKwsEnabled(uint8_t en)
{
  s_kws_enabled = en;
  printf("[KWS] %s\r\n", (en != 0U) ? "ON" : "OFF");
}

uint8_t AudioApp_GetKwsEnabled(void)
{
  return s_kws_enabled;
}

void AudioApp_HoldAwake(uint32_t ms)
{
  uint32_t until = HAL_GetTick() + ms;
  if ((int32_t)(until - s_hold_awake_until) > 0)
    s_hold_awake_until = until;
}

void AudioApp_RequestSleep(void)
{
  if (WavRecorder_active())
  {
    printf("[LPM] recording - sleep refused\r\n");
    return;
  }
  s_hold_awake_until = 0U;
#if AUDIO_ZPL_WAKE
  zpl_sleep();
#endif
  printf("[LPM] STOP2 armed (KEY/PC12 or sound wakes, UART sleeps)\r\n");
}

void AudioApp_Process(void)
{
  /* OFF mode (standby) is permanently vetoed here: standby would drop SRAM
   * and the wake line. APPE_SysEvtReadyProcessing re-enables OFF for
   * CFG_LPM_APP after init, so the veto is re-asserted every call. */
  UTIL_LPM_SetOffMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);

#if AUDIO_ZPL_WAKE
  if (s_awake_until == 0U || !s_net)
  {
    /* Debug hold-awake window (CLI activity): veto STOP2 so the shell
     * stays usable; the sequencer idles in SLEEP instead. */
    if (HAL_GetTick() < s_hold_awake_until)
    {
      UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
      return;
    }

    /* Session closed: allow STOP2 - the VM3011 DOUT wake line (PA0) brings
     * us back on the next acoustic event above the margin. */
    UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_ENABLE);
    return;
  }

  /* Session open: veto STOP2 (SAI DMA must keep running) -> the sequencer
   * idles in SLEEP between the 10 ms DMA IRQs. */
  UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
#else
  /* 连续采集调试模式：SAI 常开，只允许 SLEEP 空闲，永不进 STOP2。 */
  UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
  if (!s_net)
    return;
#endif

  for (;;)
  {
    /* 1 - take one 10 ms block from the ring (NULL = wait for DMA). */
    const int16_t *blk = consume_block();
    if (!blk)
    {
      uint32_t now = HAL_GetTick();

      /* Watchdog: SAI DMA death (HAL aborts on error, silently) leaves the
       * ring empty forever. Restart the pipeline and start over. */
      if ((uint32_t)(now - s_last_block_tick) > AUDIO_STALL_TIMEOUT_MS)
      {
        printf("[SAI] stalled %lums err=0x%02lX -> restart\r\n",
               (unsigned long)(now - s_last_block_tick),
               (unsigned long)AudioStream_get_sai_error());
        AudioStream_init();  /* ring reset + VAD reset */
        AudioStream_start(); /* restart SAI DMA                  */
        s_skip_blocks = 3U;  /* drop the mic wake transient        */
        s_last_block_tick = now;
        s_phase = PH_IDLE;
        s_vad_prev = 0;
        preroll_reset();
        return;
      }

      /* No fresh audio: in the ZPL architecture, close the session once the
       * window expired and the VAD is fully closed (hangover tail included
       * in the window via the refresh below). Recording keeps the session. */
#if AUDIO_ZPL_WAKE
      if (s_phase == PH_IDLE && !audio_vad_active() && !WavRecorder_active() &&
          (int32_t)(now - s_awake_until) >= 0)
        zpl_sleep();
#endif

      return;
    }
    s_last_block_tick = HAL_GetTick();

    /* Mic ZPL -> NORMAL warm-up: the first blocks after a stream start are
     * a wake transient, not audio (observed e~5000 vs floor~480). It must
     * not fake-open the VAD nor poison the floor / preroll history. */
    if (s_skip_blocks > 0U)
    {
      s_skip_blocks--;
      continue;
    }

    /* WAV recorder tap: while recording, every block goes to the file and the
     * KWS/VAD path is suspended (ring is owned by the recorder). */
    if (WavRecorder_active())
    {
      WavRecorder_feed(blk, AS_DMA_HALF_FRAMES);
      continue;
    }

    /* KWS off (record debug mode): drain the ring only - no VAD capture
     * and no inference. The session still closes on the quiet window. */
    if (!s_kws_enabled)
      continue;

    uint8_t act = audio_vad_active();
#if AUDIO_ZPL_WAKE
    /* Quiet deadline: stay awake a bit longer while speech is around. */
    if (act != 0U || s_phase == PH_CAPTURE)
      s_awake_until = HAL_GetTick() + ZPL_STAY_AWAKE_MS;
#endif

    /* 2 - idle: watch the VAD level, start capture on a rising edge. */
    if (s_phase == PH_IDLE)
    {
      uint8_t prev = s_vad_prev;
      s_vad_prev = act;

      if (act && !prev)
      {
        printf("[VAD] ON  e=%lu thr=%lu floor=%lu\r\n",
               (unsigned long)audio_vad_energy(),
               (unsigned long)audio_vad_open_threshold(),
               (unsigned long)audio_vad_floor());
        s_vad_on_tick = HAL_GetTick();
        s_phase = PH_CAPTURE;
        s_cap_len = 0;
        s_speech_len = 0;
        s_loud_end = 0;
        s_peak_e = 0;
        s_drop_base = AudioStream_get_ring()->droppedSamples;
        /* Prepend the idle history, oldest sample first: the capture starts
         * as [history | trigger block | following blocks], so the fricative
         * onset that failed to open the VAD is included; the fixed 1 s
         * model window is cropped later (capture_finish). */
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
     * s_loud_end tracks the capture-local end of the last loud block - the
     * fixed model window is later cropped relative to it (capture_finish).
     * Peak block energy is tracked for VAD threshold tuning. */
    {
      uint32_t en = audio_vad_energy();
      if (en > s_peak_e)
        s_peak_e = en;
    }
    capture_append(blk, AS_DMA_HALF_FRAMES);
    if (audio_vad_loud())
    {
      s_speech_len += AS_DMA_HALF_FRAMES;
      s_loud_end = s_cap_len;
    }

    /* 4 - utterance finished: the VAD is fully closed (300 ms hangover
     * included in 'act') after real speech inside -> crop the fixed 1 s
     * window anchored to the keyword end and run ONE inference. */
    if (!act && s_speech_len >= VAD_MIN_SPEECH_SAMPLES)
    {
      capture_finish();
      s_phase = PH_IDLE;
      preroll_reset(); /* history predates this capture: stale */
      /* May still be the utterance tail (or a follow-up word): only a
       * fresh OFF -> ON edge starts a new capture. */
      s_vad_prev = audio_vad_active();
      printf("[VAD] OFF active=%lums\r\n",
             (unsigned long)(HAL_GetTick() - s_vad_on_tick));
      continue;
    }

    /* Blip: the VAD closed but almost no speech was inside (a click). */
    if (!act && s_speech_len < VAD_MIN_SPEECH_SAMPLES)
    {
      printf(">> blip speech=%lums peak_e=%lu discarded\r\n",
             (unsigned long)(s_speech_len * 1000UL / SAMPLE_RATE),
             (unsigned long)s_peak_e);
      s_phase = PH_IDLE;
      preroll_reset(); /* history predates the discarded capture: stale */
      continue;
    }

    /* Safety cap: speech still running at the buffer limit - take what
     * there is (capture_finish keeps the last loud block + tail inside
     * the window; the keyword HEAD may be trimmed, never the tail). */
    if (s_cap_len >= CAPTURE_MAX_SAMPLES)
    {
      capture_finish();
      s_phase = PH_IDLE;
      preroll_reset();
      s_vad_prev = audio_vad_active();
      if (!s_vad_prev)
        printf("[VAD] OFF active=%lums\r\n",
               (unsigned long)(HAL_GetTick() - s_vad_on_tick));
    }
  }
}
