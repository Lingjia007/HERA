/**
 ******************************************************************************
 * @file    audio_stream.c
 * @brief   SAI1 Block B DMA ping-pong capture for INMP441: mono 16-bit,
 *          per-block DC removal, feeding a ring buffer.
 ******************************************************************************
 */
#include "audio_stream.h"
#include "audio_app.h"
#include "sai.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* DMA double buffer (circular, word-aligned). One 32-bit word per frame:
 * INMP441 24-bit sample MSB-justified in a 32-bit slot. */
static uint32_t s_dma_buf[AS_DMA_BUF_WORDS];

/* Ring buffer backing store (CPU-only access). */
static int16_t s_ring_backing[AS_RING_NB_SAMPLES * AS_RING_NB_FRAMES];

static AudioCaptureRingBuff_t s_ring;

/* Scratch buffer for one DMA half-block of mono samples (160 x int16_t). */
static int16_t s_mono[AS_DMA_HALF_FRAMES];

AudioCaptureRingBuff_t *AudioStream_get_ring(void) { return &s_ring; }

void AudioStream_init(void)
{
  AudioCaptureRingBuff_reset(&s_ring,
                             (uint8_t *)s_ring_backing,
                             AS_RING_NB_SAMPLES,
                             AS_RING_NB_FRAMES,
                             sizeof(int16_t));
}

void AudioStream_start(void)
{
  /* Circular DMA gives half + full transfer callbacks. Size counts memory
   * data units (words: 1 word per audio frame, mono slot 0). */
  HAL_StatusTypeDef st = HAL_SAI_Receive_DMA(&hsai_BlockB1, (uint8_t *)s_dma_buf,
                                             AS_DMA_BUF_WORDS);
  if (st != HAL_OK)
  {
    printf("SAI DMA FAIL=%" PRId32 "\r\n", (int32_t)st);
  }
}

/* Convert one DMA half-block to int16 and push to ring.
 * half_id = 0 -> first half of s_dma_buf, 1 -> second half. */
static void AudioStream_process_half(uint8_t half_id)
{
  const uint32_t *src = s_dma_buf + half_id * AS_DMA_HALF_FRAMES;

  int32_t sum = 0;
  for (uint32_t i = 0; i < AS_DMA_HALF_FRAMES; i++)
  {
    /* INMP441: 24-bit two's complement, MSB-justified -> keep top 16 bits
     * (sign-preserving) for the int16 display/frontend pipeline. */
    int16_t v = (int16_t)(src[i] >> 16);
    s_mono[i] = v;
    sum += v;
  }

  /* DC removal: subtract block mean. */
  int16_t dc = (int16_t)(sum / (int32_t)AS_DMA_HALF_FRAMES);
  for (uint32_t i = 0; i < AS_DMA_HALF_FRAMES; i++)
    s_mono[i] = (int16_t)(s_mono[i] - dc);

  AudioCaptureRingBuff_feed(&s_ring, (const uint8_t *)s_mono,
                            (uint16_t)AS_DMA_HALF_FRAMES);

  /* Display TX from the DMA IRQ: the plot pacing follows the audio clock
   * and is immune to main-loop stalls (mel frontend FFT, BLE work). */
  AudioApp_OnAudioBlock(s_mono, AS_DMA_HALF_FRAMES);
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
