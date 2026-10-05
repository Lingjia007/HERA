/**
 ******************************************************************************
 * @file    audio_stream.c
 * @brief   SAI1 Block B DMA ping-pong capture for INMP441: hardware mono
 *          (slot 1 discarded), 24-bit sample kept as top 16 bits,
 *          per-block DC removal, feeding a ring buffer.
 ******************************************************************************
 */
#include "audio_stream.h"
#include "audio_vad.h"
#include "sai.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* DMA double buffer (circular, word-aligned). SAI hardware MONO mode (RX):
 * only slot 0 (left, WS=LOW: INMP441 L/R=GND) is stored per audio frame
 * -> 1 word per frame; 24-bit sample MSB-justified in the 32-bit slot. */
static uint32_t s_dma_buf[AS_DMA_BUF_WORDS];

/* Ring buffer backing store (CPU-only access). */
static int16_t s_ring_backing[AS_RING_NB_SAMPLES * AS_RING_NB_FRAMES];

static AudioCaptureRingBuff_t s_ring;

/* Scratch buffer for one DMA half-block of mono samples (160 x int16_t). */
static int16_t s_mono[AS_DMA_HALF_FRAMES];

AudioCaptureRingBuff_t *AudioStream_get_ring(void) { return &s_ring; }

/* Last SAI error code recorded by HAL_SAI_ErrorCallback (0 = none). The HAL
 * aborts the DMA on error and calls the (default empty) weak callback - the
 * stream then dies SILENTLY; the watchdog in audio_app restarts the pipeline
 * when blocks stop arriving. */
static volatile uint32_t s_sai_error;

uint32_t AudioStream_get_sai_error(void) { return s_sai_error; }

void AudioStream_init(void)
{
  AudioCaptureRingBuff_reset(&s_ring,
                             (uint8_t *)s_ring_backing,
                             AS_RING_NB_SAMPLES,
                             AS_RING_NB_FRAMES,
                             sizeof(int16_t));
  s_sai_error = 0;
  audio_vad_init();
}

void AudioStream_start(void)
{
  /* Circular DMA gives half + full transfer callbacks. Size counts memory
   * data units (words: mono mode -> 1 word per audio frame). */
  HAL_StatusTypeDef st = HAL_SAI_Receive_DMA(&hsai_BlockB1, (uint8_t *)s_dma_buf,
                                             AS_DMA_BUF_WORDS);
  if (st != HAL_OK)
  {
    printf("SAI DMA FAIL=%" PRId32 "\r\n", (int32_t)st);
  }
}

/* Convert one DMA half-block into s_mono, remove DC offset, push to ring.
 * half_id = 0 -> first half of s_dma_buf, 1 -> second half. */
static void AudioStream_process_half(uint8_t half_id)
{
  const uint32_t *src = s_dma_buf + half_id * AS_DMA_HALFWORDS;
  uint32_t *dst = (uint32_t *)s_mono;

  /* Hardware mono mode: each word IS slot 0 (left, WS=LOW: INMP441 L/R=GND).
   * The 24-bit two's-complement sample is MSB-justified in the 32-bit slot
   * -> keep the top 16 bits (sign-preserving) for the int16 frontend.
   * Pack two int16 samples per 32-bit store: halves the store count. */
  int32_t sum = 0;
  for (uint32_t i = 0; i < AS_DMA_HALF_FRAMES; i += 2)
  {
    int16_t v0 = (int16_t)(src[i] >> 16);
    int16_t v1 = (int16_t)(src[i + 1] >> 16);
    dst[i / 2] = (uint32_t)(uint16_t)v0 | ((uint32_t)(uint16_t)v1 << 16);
    sum += v0 + v1;
  }

  /* DC removal: subtract block mean, then apply the x2 input gain with
   * int16 saturation. Everything downstream (VAD, ring, model input) works
   * in this amplified domain. */
  int16_t dc = (int16_t)(sum / (int32_t)AS_DMA_HALF_FRAMES);
  for (uint32_t i = 0; i < AS_DMA_HALF_FRAMES; i++)
  {
    int32_t v = (int32_t)(s_mono[i] - dc) * 2;
    if (v > 32767)
      v = 32767;
    else if (v < -32768)
      v = -32768;
    s_mono[i] = (int16_t)v;
  }

  /* Raw-input energy VAD (producer side, pre-consumer). */
  audio_vad_feed_block(s_mono, AS_DMA_HALF_FRAMES);

  AudioCaptureRingBuff_feed(&s_ring, (const uint8_t *)s_mono,
                            (uint16_t)AS_DMA_HALF_FRAMES);
}

/* HAL callbacks (weak symbols overridden). Only handle SAI1 Block B. */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
    AudioStream_process_half(0);
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
    AudioStream_process_half(1);
}

/* On a SAI error the HAL aborts the DMA and calls this weak callback; the
 * default implementation is EMPTY, so without this override the stream dies
 * silently. Only record the code - no printf from IRQ context. The stall
 * watchdog in audio_app detects the stopped blocks and restarts. */
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
    s_sai_error = hsai->ErrorCode;
}
