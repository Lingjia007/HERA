/**
 ******************************************************************************
 * @file    audio_stream.h
 * @brief   Real-time audio capture via SAI1 Block B DMA ping-pong for INMP441.
 *
 * Hardware assumptions (matching CubeMX-generated sai.c):
 *   - SAI1_Block_B master RX, I2S Philips, 32-bit slots, MONO mode, 16 kHz
 *   - DMA1_Channel1, circular, word (32-bit) alignment
 *
 * SAI hardware MONO mode (RX, 2 slots): per RM0434 the hardware discards
 * slot 1 and stores only slot 0 data in the FIFO -> the DMA sees exactly
 * ONE 32-bit word per audio frame. INMP441 must have L/R=GND (outputs on
 * the LEFT channel, WS=LOW = slot 0); if it were wired for the right
 * channel the hardware mono mode would discard its data.
 *
 * The 24-bit two's-complement sample is MSB-justified in the 32-bit slot;
 * keep the top 16 bits (sign-preserving) to feed the int16 mel frontend.
 *
 * Pipeline: DMA half/full IRQ -> keep top 16 bits -> per-block DC removal
 * -> x2 input gain (int16 saturation) -> energy VAD -> ring buffer. The VAD
 * and everything downstream work in the amplified domain. Main loop consumes
 * from the ring.
 *******************************************************************************/
#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

#include <stdint.h>
#include "audio_capture_ring_buff.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- Audio geometry ------------------------------------------------------ */
#define AS_SAMPLE_RATE_HZ 16000

/* 10 ms per DMA half-buffer = 160 audio frames.
 * SAI hardware MONO mode (RX): slot 1 discarded by hardware -> DMA moves
 * 1 word (32-bit slot 0) per audio frame -> 160 words per half buffer. */
#define AS_DMA_HALF_FRAMES 160
#define AS_DMA_HALFWORDS (AS_DMA_HALF_FRAMES)     /* 160 words: 1 per frame */
#define AS_DMA_BUF_WORDS (AS_DMA_HALFWORDS * 2)   /* 320 words: double buffer */

/* Ring buffer geometry: 4 frames x 3200 samples = 12800 samples (~800 ms) */
#define AS_RING_NB_SAMPLES 3200
#define AS_RING_NB_FRAMES 4

  /* Initialize the ring buffer and backing storage. */
  void AudioStream_init(void);

  /* Start SAI1 Block B DMA ping-pong capture. */
  void AudioStream_start(void);

  /* Access the ring buffer handle (consumer side). */
  AudioCaptureRingBuff_t *AudioStream_get_ring(void);

  /* Last SAI error code recorded by the HAL error callback (0 = none).
   * Bits (hal_sai.h): 0x01 OVR, 0x02 UDR, 0x04 AFSDET, 0x08 LFSDET,
   * 0x10 CNRDY, 0x20 WCKCFG, 0x80 DMA. */
  uint32_t AudioStream_get_sai_error(void);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_STREAM_H */
