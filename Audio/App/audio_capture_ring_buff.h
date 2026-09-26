/**
  ******************************************************************************
  * @file    audio_capture_ring_buff.h
  * @brief   Ring (FIFO) buffer decoupling the audio DMA producer (IRQ) from
  *          the main-loop consumer. Static backing buffer, no RTOS hooks.
  ******************************************************************************
  */
#ifndef AUDIO_CAPTURE_RING_BUFF_H
#define AUDIO_CAPTURE_RING_BUFF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Producer = I2S DMA IRQ, consumer = main loop.
 * "availableSamples" is shared across ISR/main contexts and is marked volatile
 * so the compiler always re-reads it (updates are atomic via LDREX/STREX). */
typedef struct
{
  uint8_t      *pData;             /* Backing store (provided by caller)            */
  uint8_t       nbBytesPerSample;  /* Bytes per sample (2 for int16_t)              */
  uint32_t      nbFrames;          /* Number of frame groups inside the ring        */
  uint32_t      nbSamples;         /* Samples per frame group                       */
  uint32_t      readSampleIndex;   /* Next sample to consume                        */
  uint32_t      writeSampleIndex;  /* Next sample to produce                        */
  volatile uint32_t availableSamples; /* Shared ISR/main: samples available to consume */
} AudioCaptureRingBuff_t;

/* Bind a caller-provided backing buffer and configure geometry. */
void AudioCaptureRingBuff_reset(AudioCaptureRingBuff_t *pHdle,
                                uint8_t *pBacking,
                                uint32_t nbSamples,
                                uint32_t nbFrames,
                                uint8_t  nbBytesPerSample);

/* Producer: write nbSamples samples from pData into the ring (IRQ context). */
void AudioCaptureRingBuff_feed(AudioCaptureRingBuff_t *pHdle,
                               const uint8_t *pData,
                               uint16_t nbSamples);

/* Consumer: copy nbSamples samples out into pData (main loop). Returns pData. */
uint8_t *AudioCaptureRingBuff_consume(uint8_t *pData,
                                      AudioCaptureRingBuff_t *pHdle,
                                      uint32_t nbSamples);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_CAPTURE_RING_BUFF_H */
