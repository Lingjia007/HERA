/**
 ******************************************************************************
 * @file    audio_app.c
 * @brief   Streaming speech recognition glue: SAI capture -> ring buffer ->
 *          mel frontend stream -> echodscnn (micro_speech) inference ->
 *          KWS post-processing, with judgment/print output over USART1.
 *
 * Data flow (16 kHz, mono, int16), ported 1:1 from the F407 project
 * (STM32F407VGT6_TensorFlow-Lite-Micro, X-CUBE-AI/App/app_x-cube-ai.c):
 *   SAI1_B DMA (10 ms half-blocks, IRQ) -> ring buffer (800 ms)
 *   -> main loop: consume 2560 samples (160 ms slide) -> 1 s sliding mel
 *      frontend (49x40 features) -> echodscnn inference (int8 1960 in,
 *      14 logits out) -> kws_postprocess_feed (argmax + debounce +
 *      cooldown + softmax print).
 *
 * Labels and KWS parameters (HISTORY_M/REQUIRED_N/COOLDOWN/MIN_SCORE) are
 * identical to the F407 project. printf retargets to USART1.
 *
 * Low-power: audio capture must not be suspended, so Stop/Off modes are kept
 * disabled via UTIL_LPM. The BLE stack re-enables OFF mode in
 * APPE_SysEvtReadyProcessing (app_entry.c), which runs after AudioApp_Init,
 * therefore the disable is re-asserted on every Process call (idempotent).
 ******************************************************************************
 */
#include "audio_app.h"
#include "audio_stream.h"
#include "audio_frontend.h"
#include "audio_params.h"
#include "echodscnn.h"
#include "echodscnn_data_params.h"
#include "kws_postprocess.h"
#include "main.h"
#include "app_conf.h"
#include "stm32_lpm.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ---- AI model: echodscnn = micro_speech 14-label classifier ---------------
 * Label order matches the model output (models_cmsis labels.json): 14 output
 * classes, argmax in [0,13]. Must match AI_ECHODSCNN_OUT_1_SIZE (14). */
#define NUM_LABELS 14

/* 1 = 输出原始推理结果裸值（不经过判决算法），便于观察模型原始输出；
 * 0 = 启用 kws_postprocess 分段多数投票判决。 */
#define KWS_RAW_OUTPUT 0

static const char *const kLabelNames[NUM_LABELS] = {
    "_silence_", "_unknown_",
    "yes", "no",
    "one", "two", "three", "four", "five", "six",
    "seven", "eight", "nine", "zero"};

/* Activations buffer (io buffers are allocated inside it). */
AI_ALIGNED(32)
static uint8_t pool0[AI_ECHODSCNN_DATA_ACTIVATION_1_SIZE];

static ai_handle s_net = AI_HANDLE_NULL;
static ai_buffer *s_ai_input;
static ai_buffer *s_ai_output;

/* IO data pointers (inside pool0: io buffers are allocated in activations). */
static ai_i8 *s_data_ins[AI_ECHODSCNN_IN_NUM];
static ai_i8 *s_data_outs[AI_ECHODSCNN_OUT_NUM];

/* Streaming frontend context (1-sec window + features). */
static AudioFrontendStream s_stream;

/* Scratch buffer for the (never-taken with current geometry) wrap fallback
 * when consume_no_cpy returns NULL. */
static int16_t s_slide_buf[SLIDE_SAMPLES];

/* KWS post-processing state (logic lives in Audio/kws/). */
static kws_state_t s_kws;

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

/* Consume one slide from the ring, feed the frontend, copy features to the
 * model input. Returns 0 when features are ready for inference, -2 when the
 * ring does not yet have enough samples or the window is still filling. */
static int acquire_and_process_data(ai_i8 *data[])
{
  AudioCaptureRingBuff_t *ring = AudioStream_get_ring();
  if ((int32_t)ring->availableSamples < (int32_t)SLIDE_SAMPLES)
    return -2;

  /* Zero-copy fast path: read the slide straight out of the ring buffer.
   * Falls back to the copy variant when the region wraps past the ring end
   * (with the current geometry: 12800 % 2560 == 0, this never happens). */
  const int16_t *slide = (const int16_t *)AudioCaptureRingBuff_consume_no_cpy(ring, SLIDE_SAMPLES);
  if (slide == NULL)
  {
    AudioCaptureRingBuff_consume((uint8_t *)s_slide_buf, ring, SLIDE_SAMPLES);
    slide = s_slide_buf;
  }

  int ready = audio_frontend_stream_push(&s_stream, slide, SLIDE_SAMPLES);
  if (!ready)
    return -2; /* initial 1-second window not yet filled */

  memcpy(data[0], audio_frontend_stream_get_features(&s_stream),
         AI_ECHODSCNN_IN_1_SIZE_BYTES);
  return 0;
}

/* KWS decision: segment majority vote, one printout per utterance. */
static int post_process(ai_i8 *data[])
{
  const int8_t *out = (const int8_t *)data[0];

#if KWS_RAW_OUTPUT
  /* 裸值模式：只给出 200ms 推理结果（argmax + 分数），不打印 logits。
   * 0=_silence_, 1=_unknown_（安静/噪声）不输出。 */
  int max_idx = 0;
  for (int i = 1; i < NUM_LABELS; i++)
    if (out[i] > out[max_idx])
      max_idx = i;

  if (max_idx > 1)
    printf("[RAW] %s score=%d\r\n", kLabelNames[max_idx], out[max_idx]);
#else
  (void)kws_postprocess_feed(&s_kws, out, NUM_LABELS, kLabelNames);
#endif

  return 0;
}

void AudioApp_Init(void)
{
  printf("\r\n=== micro_speech streaming ===\r\n");

  ai_boostrap();

  /* Initialize streaming audio frontend (1-sec sliding window). */
  audio_frontend_stream_init(&s_stream);

  /* Initialize KWS post-processing state (debounce + cooldown). */
  kws_postprocess_init(&s_kws);

  AudioStream_init();
  AudioStream_start();
}

void AudioApp_Process(void)
{
  /* Keep Stop/Off modes disabled: SAI audio DMA must keep running.
   * Re-asserted here because APPE_SysEvtReadyProcessing re-enables OFF. */
  UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
  UTIL_LPM_SetOffMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);

  if (!s_net)
    return;

  int res;
  do
  {
    /* 1 - acquire from ring + slide window + preprocess */
    res = acquire_and_process_data(s_data_ins);
    if (res == -2)
      break; /* ring empty or window still filling */

    /* 2 - run inference */
    if (res == 0)
      res = ai_run();

    /* 3 - post-process predictions */
    if (res == 0)
      res = post_process(s_data_outs);
  } while (res == 0);
  /* res == -2: not enough samples yet; return to main loop and retry. */
}
