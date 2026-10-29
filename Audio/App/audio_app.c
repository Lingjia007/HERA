/**
 ******************************************************************************
 * @file    audio_app.c
 * @brief   ZPL-gated single-shot KWS: the VM3011 ZPL wake line (DOUT) opens
 *          a listening session -> SAI PDM capture + PDM2PCM in the SAI IRQ
 *          -> ring buffer -> energy VAD -> 1 s utterance capture -> full
 *          mel frontend -> TFLM (bcresnet) inference -> single-shot print
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
 *      logmel (40x101 int8) -> TFLM int8 net (4040 in, 14 logits out)
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
 * Two-phase flow: a ZPL wake opens a session in WAIT-WAKE phase - only an
 * utterance classified as "小艺小艺" (label 7, gated by KWS_MIN_*) enters
 * the COMMAND phase: a 5 s window in which every VAD-segmented utterance
 * is recognized as a command; each VAD-detected speech postpones the
 * window by 5 s, 5 s of quiet closes the session (STOP2, mic back to ZPL).
 ******************************************************************************
 */
#include "audio_app.h"
#include "audio_stream.h"
#include "audio_vad.h"
#include "logmel.h"
#include "audio_params.h"
#include "tflm_c.h"
#include "network_tflite_data.h"
#include "main.h"
#include "app_conf.h"
#include "stm32_lpm.h"
#include "wav_recorder.h"
#include "usb_msc.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ---- AI model: native TFLM runtime (tflm_c wrapper) ------------------------
 * 14-label int8 classifier, input [1,40,101] logmel int8, output [1,14] int8
 * logits. Label order matches the model output (training label_map):
 * 0=_silence_, 1=_unknown_, then the 12 keywords. */
#define NUM_LABELS 14

static const char *const kLabelNames[NUM_LABELS] = {
    "_silence_", "_unknown_",
    "上一首", "下一首",
    "停止播放", "关闭降噪",
    "开启降噪", "小艺小艺",
    "微信支付", "挂断电话",
    "接听电话", "播放音乐",
    "调大音量", "调小音量"};

/* Tensor arena (tensor memory) + TFLM interpreter handle. */
static uint8_t s_tensor_arena[TFLM_NETWORK_TENSOR_AREA_SIZE]
    __attribute__((aligned(16)));
static uint32_t s_tflm; /* interpreter handle, 0 = not created */
static struct tflm_c_tensor_info s_in, s_out;

/* ---- Window lives in the capture ring --------------------------------------
 * No dedicated capture buffer: the SAI FIFO ring (audio_stream.c) is sized to
 * AS_RING_CAPACITY = 101 x 160 = 16160 samples (~1.01 s) and keeps rolling in
 * IDLE, so it ALWAYS holds the most recent ~1 s of audio - including the
 * fricative onsets before the VAD rising edge (x/sh of 上一首/下一首 sit
 * below the VAD open threshold; up to ~730 ms of pre-trigger history is
 * reachable, vs the 500 ms the old dedicated pre-roll buffer provided).
 * capture_finish anchors the fixed 1 s model window to the utterance end
 * (last VAD-loud block + 150 ms tail) via ring slots - no copies, no prepend.
 * The 1.35 s "capture span ceiling" is gone: a long utterance simply means
 * the window covers its last second (the keyword HEAD may be trimmed for
 * utterances longer than the ring, never the tail - same guarantee as
 * before, the cap below only bounds the session, not memory). */
#define CAPTURE_CAP_SAMPLES (SAMPLE_RATE * 4U) /* safety cap: 4 s of VAD-open */

/* Scratch for the consume_no_cpy wrap fallback (never taken with the
 * current all-160-sample-aligned ring geometry). */
static int16_t s_blk[AS_DMA_HALF_FRAMES];

/* ---- Utterance state machine ------------------------------------------------
 * PH_IDLE    : drain the ring, watch VAD level; rising edge -> capture.
 * PH_CAPTURE : count 10 ms blocks while the VAD stays active (audio stays in
 *              the rolling ring, nothing is copied); when the VAD closes
 *              (hangover included) with real speech inside -> anchor the
 *              fixed 1 s window to the utterance end (capture_finish) ->
 *              run ONE inference -> idle. VAD closed with almost no speech
 *              -> noise blip -> discard. */
typedef enum
{
  PH_IDLE = 0,
  PH_CAPTURE
} phase_t;
static phase_t s_phase;
static uint8_t s_vad_prev;         /* VAD level at the previous poll (edge detect) */
static uint32_t s_cap_len;         /* samples consumed since the rising edge     */
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

/* ---- 两阶段识别 ------------------------------------------------------------
 * 阶段 1（等唤醒词）：ZPL 声音唤醒开监听，VAD 截段推理，只有 argmax 命中
 *   "小艺小艺"（且过 KWS_MIN_* 门限）才进入阶段 2；其他语音不唤醒。
 * 阶段 2（命令窗口）：唤醒后 5s 窗口，VAD 每截到一段语音就跑一次 1s 窗
 *   推理识别命令词；只要有 VAD 检测（语音活动）窗口就顺延 5s，安静满
 *   5s 关会话回 STOP2。 */
#define CMD_WINDOW_MS 5000U /* 唤醒后命令窗口，VAD 检测到语音则顺延   */
#define KWS_WAKE_LABEL 7U   /* kLabelNames[7] = "小艺小艺"（唤醒词）  */

static VM3011_Dev_t *s_vm;      /* mic handle for the DOUT re-arm (main owns) */
static uint32_t s_awake_until;  /* HAL_GetTick deadline; 0 = session closed   */
static uint8_t s_cmd_mode;      /* 0 = 等唤醒词；1 = 已唤醒（命令窗口内）     */
static uint8_t s_kws_enabled = 1U;  /* 1 = KWS 唤醒识别默认开；0 = 录音调试模式 */
static uint32_t s_hold_awake_until; /* debug hold-awake deadline (CLI alive)   */
static uint8_t s_usb_stopped;        /* USB disconnected for STOP2 (restarted by UsbMsc_Poll) */

/* ---- Per-node profiler ----------------------------------------------------
 * Prints the wall time of every NN operator on each invoke: locates the hot
 * operator when inference feels slow. get_time granularity is 1 ms. Set
 * KWS_NODE_PROFILER to 0 once the diagnosis is done (stdout spam). */
#define KWS_NODE_PROFILER 0
#if KWS_NODE_PROFILER
static uint64_t prof_get_time(int mode)
{
  (void)mode;
  return (uint64_t)HAL_GetTick();
}
static int prof_notify(const void *cookie, uint32_t flags,
                       const struct tflm_c_node *node)
{
  (void)cookie;
  (void)flags;
  printf("  ai#%-3u %-24s %lums\r\n", node->node_info.idx,
         node->node_info.name != NULL ? node->node_info.name : "?",
         (unsigned long)node->node_info.dur);
  return 0;
}
static struct tflm_c_observer_options s_prof_opts = {
    prof_notify, prof_get_time, NULL, OBSERVER_FLAGS_TIME_ONLY};
#endif

/* Create the TFLM interpreter, bind input/output tensors and sanity-check
 * their geometry against the frontend (40x101 int8 in, 14 int8 out). */
static int ai_boostrap(void)
{
  TfLiteStatus st = tflm_c_create(g_tflm_network_model_data, s_tensor_arena,
                                  TFLM_NETWORK_TENSOR_AREA_SIZE, &s_tflm);
  if (st != kTfLiteOk || s_tflm == 0U)
  {
    printf("AI - tflm_c_create failed (%d)\r\n", (int)st);
    s_tflm = 0U;
    return -1;
  }

  if (tflm_c_input(s_tflm, 0, &s_in) != kTfLiteOk ||
      tflm_c_output(s_tflm, 0, &s_out) != kTfLiteOk)
  {
    printf("AI - tensor binding failed\r\n");
    tflm_c_destroy(s_tflm);
    s_tflm = 0U;
    return -1;
  }

#if KWS_NODE_PROFILER
  (void)tflm_c_observer_register(s_tflm, &s_prof_opts);
  (void)tflm_c_observer_start(s_tflm);
#endif

  printf("[TFLM] %s arena=%u used=%ld in=%uB(%lu,%lu,%lu) sc=%g zp=%d "
         "out=%uB nodes=%ld\r\n",
         TFLM_NETWORK_NAME, (unsigned)TFLM_NETWORK_TENSOR_AREA_SIZE,
         tflm_c_arena_used_bytes(s_tflm), (unsigned)s_in.bytes,
         s_in.shape.size >= 1 ? s_in.shape.data[0] : 0,
         s_in.shape.size >= 2 ? s_in.shape.data[1] : 0,
         s_in.shape.size >= 3 ? s_in.shape.data[2] : 0,
         (double)s_in.scale, s_in.zero_point, (unsigned)s_out.bytes,
         tflm_c_operators_size(s_tflm));

  if (s_in.bytes != LM_N_MELS * LM_N_FRAMES ||
      s_out.bytes != NUM_LABELS)
  {
    printf("AI - tensor size mismatch! expect in=%d out=%d\r\n",
           LM_N_MELS * LM_N_FRAMES, NUM_LABELS);
    tflm_c_destroy(s_tflm);
    s_tflm = 0U;
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

/* Anchor the model's FIXED 1 s window inside the rolling capture ring, then
 * run the inference on it. The window is anchored to the utterance END:
 *   end = (last VAD-loud block) + 150 ms tail, clamped to the capture,
 * so the keyword tail is NEVER truncated no matter where the trigger
 * opened - only lead-in silence ahead of the keyword gets cut. The window
 * data is NOT copied: logmel reads it through ring slots (reflect padding
 * is computed on the fly by the accessor, see logmel_process_window_ring).
 * If the captured span is shorter than 1 s the missing head is zero-padded
 * (leading silence is in-distribution) - same layout as before:
 * [lead-in | keyword | ~150 ms tail]. */
static int run_inference(const int16_t *base, uint32_t cap, uint32_t slot0,
                         uint32_t lead_zeros);
static int capture_finish(void)
{
  uint32_t tail = (SAMPLE_RATE * 3U) / 20U; /* 150 ms trailing quiet */
  uint32_t end = s_loud_end + tail;
  AudioCaptureRingBuff_t *rg = AudioStream_get_ring();
  uint32_t cap = (uint32_t)rg->nbSamples * rg->nbFrames; /* 16160 */

  if (end < (uint32_t)DESIRED_SAMPLES)
    end = (uint32_t)DESIRED_SAMPLES; /* always feed a full 1 s window */
  if (end > s_cap_len)
    end = s_cap_len; /* clamp to what was actually captured */

  /* Ring slot arithmetic: readSampleIndex is the slot just past the last
   * consumed sample; the window ends (cap_len - end) samples earlier. */
  uint32_t gap = s_cap_len - end;
  uint32_t slot_end = (rg->readSampleIndex + cap - (gap % cap)) % cap;
  uint32_t slot0 = (slot_end + cap - (uint32_t)DESIRED_SAMPLES) % cap;

  /* Zero-pad count for the window head: samples that predate either the
   * stream start or the ring coverage (normal path: 0; short capture:
   * 1 s - end; stuck-VAD safety path: whatever fell out of the ring). */
  uint32_t inring = (s_cap_len < cap) ? s_cap_len : cap;
  int32_t lead32 = (int32_t)DESIRED_SAMPLES - (int32_t)end +
                   (int32_t)s_cap_len - (int32_t)inring;
  uint32_t lead_zeros = (lead32 > 0) ? (uint32_t)lead32 : 0U;

  /* 推理期间本次识别已不需要新音频：停掉 SAI（PDM 时钟停，mic 短暂回
   * ZPL），省下每 10 ms 一次的 DMA/PDM2PCM/VAD/ring 中断对 CPU 的抢占
   * （缩短推理），也杜绝推理期间的 ring overrun 丢样本。推完立即恢复
   * 采集、刷新 watchdog 基准并丢弃 mic ZPL->NORMAL 唤醒瞬态。 */
  AudioStream_stop();
  int r = run_inference((const int16_t *)rg->pData, cap, slot0, lead_zeros);
  AudioStream_start();
  s_last_block_tick = HAL_GetTick();
  s_skip_blocks = 3U; /* ~30 ms 瞬态，不喂 VAD/环 */
  return r;
}

/* 单条语音识别结果的两阶段处理：
 * 命令窗口内：窗口顺延（"有 VAD 检测就延后"），命令词照常打印；
 * 等唤醒阶段：仅 "小艺小艺" 过门限才进入命令窗口。 */
static void handle_utterance(int lbl)
{
  if (s_cmd_mode != 0U)
  {
    s_awake_until = HAL_GetTick() + CMD_WINDOW_MS;
    return;
  }
  if (lbl == (int)KWS_WAKE_LABEL)
  {
    s_cmd_mode = 1U;
    s_awake_until = HAL_GetTick() + CMD_WINDOW_MS;
    printf("[WAKE] 小艺小艺 -> cmd window %lus\r\n",
           (unsigned long)(CMD_WINDOW_MS / 1000U));
  }
}

/* Full 1 s clip -> 40x101 logmel int8 features -> one TFLM inference ->
 * single-shot print: argmax over all 14 labels, margin = top1 - top2.
 * One line per detected utterance, for threshold tuning.
 * Returns the argmax label index (0/1 = silence/unknown), -1 on AI error.
 * The window is read through ring slots (base/cap/slot0/lead_zeros, see
 * capture_finish); the frontend writes straight into the model input
 * tensor (same [40][101] mel-major int8 layout, byte size checked in
 * AudioApp_Init). No staging buffer needed. */
static int run_inference(const int16_t *base, uint32_t cap, uint32_t slot0,
                         uint32_t lead_zeros)
{
  uint32_t t0 = HAL_GetTick();
  logmel_process_window_ring(base, cap, slot0, lead_zeros,
                             (int8_t *)s_in.data);
  uint32_t fe_ms = HAL_GetTick() - t0; /* logmel frontend only */

  if (tflm_c_invoke(s_tflm) != kTfLiteOk)
  {
    printf("AI - tflm_c_invoke failed\r\n");
    return -1;
  }
  /* NN only (subtract the frontend); fe+nn printed once per utterance. */
  uint32_t infer_ms = HAL_GetTick() - t0 - fe_ms;
  printf("[KWS] time logmel=%lums nn=%lums\r\n",
         (unsigned long)fe_ms, (unsigned long)infer_ms);

  const int8_t *out = (const int8_t *)s_out.data;
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
    printf("[KWS] RESULT #%-2d %-8s score=%d margin=%d speech=%lums peak_e=%lu drop=%lu infer=%lums\r\n",
           max_i, kLabelNames[max_i], out[max_i], margin,
           (unsigned long)speech_ms, (unsigned long)s_peak_e,
           (unsigned long)dropped, (unsigned long)infer_ms);
  else if (max_i > 1)
    printf("[KWS] weak   #%-2d %-8s score=%d margin=%d speech=%lums peak_e=%lu drop=%lu infer=%lums\r\n",
           max_i, kLabelNames[max_i], out[max_i], margin,
           (unsigned long)speech_ms, (unsigned long)s_peak_e,
           (unsigned long)dropped, (unsigned long)infer_ms);
  else
    printf("[KWS] reject  #%-2d %-9s score=%d margin=%d speech=%lums peak_e=%lu drop=%lu infer=%lums\r\n",
           max_i, kLabelNames[max_i], out[max_i], margin,
           (unsigned long)speech_ms, (unsigned long)s_peak_e,
           (unsigned long)dropped, (unsigned long)infer_ms);

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

  return max_i;
}

void AudioApp_Init(VM3011_Dev_t *vm)
{
  printf("\r\n=== logmel + TFLM ZPL wake single-shot ===\r\n");

  s_vm = vm;

  ai_boostrap();

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
  s_cmd_mode = 0U;
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
    s_cmd_mode = 0U; /* 新会话从"等唤醒词"开始 */
    s_last_block_tick = now;
  }
  else
  {
    /* Already listening: a re-asserted DOUT (sustained sound) only
     * refreshes the stay-awake window - 只延长、不缩短（命令窗口 5s
     * 不被 3s 预算覆盖）。 */
    printf("[ZPL] re-arm while listening\r\n");
  }

  {
    uint32_t win = (s_cmd_mode != 0U) ? CMD_WINDOW_MS : ZPL_LISTEN_WINDOW_MS;
    if ((int32_t)(now + win - s_awake_until) > 0)
      s_awake_until = now + win;
  }
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
  s_cmd_mode = 0U; /* 会话关闭，下次唤醒重新等 "小艺小艺" */
  /* STOP2 后 USB IP 无时钟：断开 DP（PC 视为拔出），唤醒后
   * UsbMsc_Poll 重新枚举。 */
  UsbMsc_NotifySleep();
  s_usb_stopped = 1U;
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
  if (UsbMsc_Mounted())
  {
    printf("[LPM] usb U-disk mounted - sleep refused\r\n");
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
  if (s_awake_until == 0U || s_tflm == 0U)
  {
    /* Debug hold-awake window (CLI activity) or USB U-disk mounted: veto
     * STOP2 so the shell / the disk stay usable; the sequencer idles in
     * SLEEP instead. */
    if (HAL_GetTick() < s_hold_awake_until || UsbMsc_Mounted())
    {
      s_usb_stopped = 0U;
      UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
      return;
    }

    /* Session closed: allow STOP2 - the VM3011 DOUT wake line (PA0) brings
     * us back on the next acoustic event above the margin. USB must be
     * disconnected first (once per transition - USBD_Stop is not idempotent
     * enough to call every tick); the wake path restarts it via Poll. */
    if (s_usb_stopped == 0U)
    {
      UsbMsc_NotifySleep();
      s_usb_stopped = 1U;
    }
    UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_ENABLE);
    return;
  }

  /* Session open: veto STOP2 (SAI DMA must keep running) -> the sequencer
   * idles in SLEEP between the 10 ms DMA IRQs. */
  UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
#else
  /* 连续采集调试模式：SAI 常开，只允许 SLEEP 空闲，永不进 STOP2。 */
  UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
  if (s_tflm == 0U)
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
     * not fake-open the VAD nor poison the floor history. */
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
    /* Quiet deadline: 命令窗口内每次 VAD 活动顺延 5s（"有检测就延后"）；
     * 等唤醒阶段只小幅续期（+800ms），说不出唤醒词就尽快回睡。 */
    if (act != 0U || s_phase == PH_CAPTURE)
    {
      s_awake_until = HAL_GetTick() +
                      ((s_cmd_mode != 0U) ? CMD_WINDOW_MS : ZPL_STAY_AWAKE_MS);
    }
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
        /* No history copy needed: the rolling ring already holds the last
         * ~1 s of idle audio, so the fricative onset before this edge is
         * inside the window that capture_finish will anchor. */
      }
      else if (!act && prev)
      {
        printf("[VAD] OFF active=%lums\r\n",
               (unsigned long)(HAL_GetTick() - s_vad_on_tick));
        continue; /* falling edge never feeds the capture */
      }
      else
      {
        continue; /* still idle - the ring keeps the history rolling */
      }
    }

    /* 3 - capture: count this block (the trigger block included). The audio
     * itself stays in the rolling ring - nothing is copied. Speech content
     * counts only blocks that were truly loud (above the VAD hold
     * threshold), so the 300 ms hangover tail is not counted.
     * s_loud_end tracks the capture-local end of the last loud block - the
     * fixed model window is later anchored relative to it (capture_finish).
     * Peak block energy is tracked for VAD threshold tuning. */
    {
      uint32_t en = audio_vad_energy();
      if (en > s_peak_e)
        s_peak_e = en;
    }
    s_cap_len += AS_DMA_HALF_FRAMES;
    if (audio_vad_loud())
    {
      s_speech_len += AS_DMA_HALF_FRAMES;
      s_loud_end = s_cap_len;
    }

    /* 4 - utterance finished: the fixed 1 s window is complete once the
     * 150 ms quiet tail after the last loud block is inside the capture -
     * infer IMMEDIATELY instead of waiting out the VAD 300 ms hangover
     * (that trailing audio would not enter the window anyway):
     * 感知延迟直接少 150 ms。 */
    if (s_speech_len >= VAD_MIN_SPEECH_SAMPLES &&
        s_cap_len >= s_loud_end + ((SAMPLE_RATE * 3U) / 20U))
    {
      handle_utterance(capture_finish());
      s_phase = PH_IDLE;
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
      continue;
    }

    /* Safety cap: VAD stuck open for 4 s - take what there is
     * (capture_finish keeps the last loud block + tail inside the window;
     * the keyword HEAD may be trimmed, never the tail). */
    if (s_cap_len >= CAPTURE_CAP_SAMPLES)
    {
      handle_utterance(capture_finish());
      s_phase = PH_IDLE;
      s_vad_prev = audio_vad_active();
      if (!s_vad_prev)
        printf("[VAD] OFF active=%lums\r\n",
               (unsigned long)(HAL_GetTick() - s_vad_on_tick));
    }
  }
}
