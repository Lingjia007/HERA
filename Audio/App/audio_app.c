/**
 ******************************************************************************
 * @file    audio_app.c
 * @brief   Audio application glue: SAI capture -> ring buffer -> mel frontend,
 *          with VOFA+ telemetry over USART1.
 *
 * Data flow (16 kHz, mono, int16):
 *   SAI1_B DMA (10 ms half-blocks, IRQ) -> ring buffer (800 ms)
 *   -> IRQ (audio_stream.c callback): AudioApp_OnAudioBlock sends ONE VOFA+
 *      frame per 10 ms block (100 frame/s). TX pacing = audio clock, so the
 *      plot stays smooth even while the main loop is busy.
 *   -> main loop: consume 3200 samples (200 ms) -> 1 s sliding mel frontend
 *      (49x40 features, heavy: kept off the display path).
 *      ch0 = freshest sample, ch1 = block RMS, ch2 = block peak,
 *      ch3 = 1.0 pulse on the first frame after fresh features.
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
#include "VOFA.h"
#include "main.h"
#include "app_conf.h"
#include "stm32_lpm.h"
#include <stdint.h>
#include <string.h>

/* 200 ms batch = one frontend slide (SLIDE_SAMPLES) */
#define APP_BATCH_SAMPLES SLIDE_SAMPLES

static AudioFrontendStream s_stream;
static int16_t s_batch[APP_BATCH_SAMPLES];
static volatile uint8_t s_ready_pulse; /* set in main loop, consumed in IRQ */
static uint8_t s_started;

/* Integer square root. */
static uint32_t isqrt32(uint32_t num)
{
  uint32_t res = 0;
  uint32_t bit = 1u << 30;
  if (num == 0)
    return 0;
  while (bit > num)
    bit >>= 2;
  while (bit != 0)
  {
    if (num >= res + bit)
    {
      num -= res + bit;
      res = (res >> 1) + bit;
    }
    else
    {
      res >>= 1;
    }
    bit >>= 2;
  }
  if (num > res && res != 0xFFFF)
    res++;
  return res;
}

void AudioApp_Init(void)
{
  AudioStream_init();
  audio_frontend_stream_init(&s_stream);
  Vofa_Init();
  s_started = 1;
  AudioStream_start();
}

void AudioApp_Process(void)
{
  if (!s_started)
    return;

  /* Keep Stop/Off modes disabled: SAI audio DMA must keep running.
   * Re-asserted here because APPE_SysEvtReadyProcessing re-enables OFF. */
  UTIL_LPM_SetStopMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);
  UTIL_LPM_SetOffMode(1U << CFG_LPM_APP, UTIL_LPM_DISABLE);

  AudioCaptureRingBuff_t *ring = AudioStream_get_ring();

  /* Frontend only: 200 ms slide. Display TX lives in the DMA IRQ
   * (AudioApp_OnAudioBlock) so FFT time here cannot stall the plot. */
  if (ring->availableSamples >= APP_BATCH_SAMPLES)
  {
    AudioCaptureRingBuff_consume((uint8_t *)s_batch, ring, APP_BATCH_SAMPLES);

    if (audio_frontend_stream_push(&s_stream, s_batch,
                                   APP_BATCH_SAMPLES) == 1)
      s_ready_pulse = 1; /* one-frame pulse when features are fresh */
  }
}

void AudioApp_OnAudioBlock(const int16_t *samples, uint32_t n)
{
  if (!s_started)
    return;

  /* Block statistics for ch1/ch2. */
  uint64_t acc = 0;
  int16_t peak = 0;
  for (uint32_t i = 0; i < n; i++)
  {
    int32_t v = samples[i];
    acc += (uint64_t)((int64_t)v * v);
    int16_t a = (v >= 0) ? (int16_t)v : (int16_t)(-v);
    if (a > peak)
      peak = a;
  }

  VOFA_Handle.fdata[0] = (float)samples[n - 1];
  VOFA_Handle.fdata[1] = (float)isqrt32((uint32_t)(acc / n));
  VOFA_Handle.fdata[2] = (float)peak;
  VOFA_Handle.fdata[3] = (s_ready_pulse) ? 1.0f : 0.0f;
  s_ready_pulse = 0;
  /* Blocking 20-byte TX (~1.7 ms @115200) inside the DMA IRQ.
   * Only this IRQ writes VOFA. */
  Vofa_Send();
}
