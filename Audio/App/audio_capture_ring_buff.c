/**
 ******************************************************************************
 * @file    audio_capture_ring_buff.c
 * @brief   Lock-free ring buffer decoupling the audio DMA producer (IRQ)
 *          from the main-loop consumer:
 *            - static caller-provided backing buffer (no malloc)
 *            - bare-metal only (no RTOS mutex path)
 *            - atomic counter update via LDREX/STREX (Cortex-M4)
 ******************************************************************************
 */
#include "audio_capture_ring_buff.h"
#include "stm32wbxx.h"
#include <string.h>

/* atomic_add via exclusive load/store; avoids __disable_irq on producer side. */
static inline void atomic_add(volatile uint32_t *p32, int32_t inc)
{
  do
  {
  } while (__STREXW(__LDREXW(p32) + inc, p32));
}

void AudioCaptureRingBuff_reset(AudioCaptureRingBuff_t *pHdle,
                                uint8_t *pBacking,
                                uint32_t nbSamples,
                                uint32_t nbFrames,
                                uint8_t nbBytesPerSample)
{
  memset(pHdle, 0, sizeof(*pHdle));
  pHdle->pData = pBacking;
  pHdle->nbSamples = nbSamples;
  pHdle->nbFrames = nbFrames;
  pHdle->nbBytesPerSample = nbBytesPerSample;
  pHdle->readSampleIndex = 0;
  pHdle->writeSampleIndex = 0;
  pHdle->availableSamples = 0;
}

void AudioCaptureRingBuff_feed(AudioCaptureRingBuff_t *pHdle,
                               const uint8_t *pData,
                               uint16_t nbSamples)
{
  if ((pData == NULL) || (pHdle->pData == NULL))
    return;

  uint32_t u32_n = (uint32_t)nbSamples;

  /* Overrun guard: never overwrite unread data. If the consumer is late,
   * drop the incoming block instead of corrupting the ring. */
  if ((pHdle->availableSamples + u32_n) > (pHdle->nbSamples * pHdle->nbFrames))
    return;

  uint32_t bps = pHdle->nbBytesPerSample;
  uint32_t idx = pHdle->writeSampleIndex;
  uint32_t idxMax = pHdle->nbSamples * pHdle->nbFrames;
  uint32_t toWrite = (u32_n < (idxMax - idx)) ? u32_n : (idxMax - idx);

  memcpy(pHdle->pData + idx * bps, pData, toWrite * bps);
  uint32_t written = toWrite;
  toWrite = u32_n - toWrite;
  pHdle->writeSampleIndex += u32_n;
  if (toWrite)
  {
    memcpy(pHdle->pData, pData + written * bps, toWrite * bps);
    pHdle->writeSampleIndex = toWrite;
  }
  atomic_add(&pHdle->availableSamples, (int32_t)u32_n);
}

uint8_t *AudioCaptureRingBuff_consume(uint8_t *pData,
                                      AudioCaptureRingBuff_t *pHdle,
                                      uint32_t nbSamples)
{
  int32_t s32_n = (int32_t)nbSamples;
  uint32_t u32_n = (uint32_t)nbSamples;

  /* Lock-free: the feed() overrun guard guarantees the producer never
   * overwrites unread data, and availableSamples is updated atomically,
   * so no __disable_irq() is needed here. */
  if (pHdle->availableSamples >= s32_n)
  {
    uint32_t bps = pHdle->nbBytesPerSample;
    uint32_t idx = pHdle->readSampleIndex;
    uint32_t idxMax = pHdle->nbSamples * pHdle->nbFrames;
    uint32_t toRead = (u32_n < (idxMax - idx)) ? u32_n : (idxMax - idx);

    memcpy(pData, pHdle->pData + idx * bps, toRead * bps);
    uint32_t read = toRead;
    toRead = u32_n - toRead;
    pHdle->readSampleIndex += u32_n;
    if (toRead)
    {
      memcpy(pData + read * bps, pHdle->pData, toRead * bps);
      pHdle->readSampleIndex = toRead;
    }
    atomic_add(&pHdle->availableSamples, -s32_n);
  }
  return pData;
}

uint8_t *AudioCaptureRingBuff_consume_no_cpy(AudioCaptureRingBuff_t *pHdle,
                                             uint32_t nbSamples)
{
  int32_t s32_n = (int32_t)nbSamples;
  uint32_t idxMax = pHdle->nbSamples * pHdle->nbFrames;

  /* Zero-copy variant: return a pointer into the ring and skip the memcpy.
   * Returns NULL if not enough samples OR if the region would wrap past the
   * end of the backing store (caller should fall back to the copy variant).
   * The feed() overrun guard guarantees the returned region stays valid
   * until the producer wraps around to it. */
  if ((pHdle->availableSamples >= s32_n) &&
      (pHdle->readSampleIndex + nbSamples <= idxMax))
  {
    uint8_t *pDestData = pHdle->pData +
        ((uint32_t)pHdle->nbBytesPerSample * pHdle->readSampleIndex);
    pHdle->readSampleIndex = (pHdle->readSampleIndex + nbSamples) % idxMax;
    atomic_add(&pHdle->availableSamples, -s32_n);
    return pDestData;
  }
  return NULL;
}
