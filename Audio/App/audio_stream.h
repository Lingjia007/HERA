/**
 ******************************************************************************
 * @file    audio_stream.h
 * @brief   Real-time audio capture via SAI1 Block B DMA ping-pong for INMP441.
 *
 * Hardware assumptions (matching CubeMX-generated sai.c):
 *   - SAI1_Block_B master RX, I2S Philips, 32-bit slots, MONO mode, 16 kHz
 *   - DMA1_Channel1, circular, word (32-bit) alignment
 *
 * INMP441 sends a 24-bit two's-complement sample MSB-justified in a 32-bit
 * slot, so the DMA transfers one word per audio frame. SAI mono mode stores
 * only slot 0 data in the FIFO. INMP441 with L/R=GND outputs on the LEFT
 * channel, which is slot 0 in I2S Philips (WS=LOW). If the mic were wired for
 * the right channel, reconfigure the SAI slots in CubeMX (stereo) and
 * deinterleave here.
 *
 * Pipeline: DMA half/full IRQ -> per-block DC removal -> ring buffer.
 * Main loop consumes from the ring.
 ******************************************************************************
 */
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

/* 10 ms per DMA half-buffer = 160 mono samples.
 * DMA moves 1 word (32-bit) per audio frame: 160 words per half buffer. */
#define AS_DMA_HALF_FRAMES 160
#define AS_DMA_BUF_WORDS (AS_DMA_HALF_FRAMES * 2) /* 320 words: double buffer */

/* Ring buffer geometry: 4 frames x 3200 samples = 12800 samples (~800 ms) */
#define AS_RING_NB_SAMPLES 3200
#define AS_RING_NB_FRAMES 4

  /* Initialize the ring buffer and backing storage. */
  void AudioStream_init(void);

  /* Start SAI1 Block B DMA ping-pong capture. */
  void AudioStream_start(void);

  /* Access the ring buffer handle (consumer side). */
  AudioCaptureRingBuff_t *AudioStream_get_ring(void);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_STREAM_H */
