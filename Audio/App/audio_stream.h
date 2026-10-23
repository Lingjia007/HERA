/**
 ******************************************************************************
 * @file    audio_stream.h
 * @brief   Real-time audio capture via SAI1 Block A PDM + PDM2PCM decimation.
 *
 * Hardware assumptions (matching CubeMX-generated sai.c and the
 * NUCLEO-U385RG-Q SAI_AudioRecord_PDM reference example):
 *   - SAI1_Block_A master RX, free protocol, PDM enabled, 1 microphone pair,
 *     8-bit slots (2 per frame), PDM CK = 2 MHz
 *   - DMA1_Channel1, circular, byte alignment
 *
 * Each 8-bit slot word carries 8 consecutive PDM bits, and the two slots of
 * a microphone pair are interleaved byte-wise in the DMA stream. PDM2PCM
 * decimates by 128 (2 MHz per microphone -> 15.625 kHz PCM) and applies the
 * high-pass filter and 24 dB gain, so no DC removal / extra gain is done here.
 *
 * Pipeline: DMA half/full IRQ -> PDM_Filter (slot 0 mic) -> 10 ms mono block
 * -> energy VAD -> ring buffer. Main loop consumes from the ring.
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

/* PDM half-buffer: 5120 bytes = 2560 bytes (20480 PDM bits) per microphone
 * = 160 PCM samples at x128 decimation (~10 ms at 2 MHz PDM per mic). */
#define AS_PDM_HALF_BYTES 5120U
#define AS_PDM_BUF_BYTES (2U * AS_PDM_HALF_BYTES)

/* PCM frames per DMA half-buffer (~10 ms) */
#define AS_DMA_HALF_FRAMES 160

/* Ring buffer geometry: 4 frames x 3200 samples = 12800 samples (~800 ms) */
#define AS_RING_NB_SAMPLES 3200
#define AS_RING_NB_FRAMES 4

  /* Initialize the ring buffer, the PDM2PCM filter and the VAD. */
  void AudioStream_init(void);

  /* Start SAI1 Block A PDM DMA ping-pong capture. */
  void AudioStream_start(void);

  /* Stop the SAI1 PDM DMA (PDM clock stops -> the VM3011 falls back to
   * ZPL mode and can raise DOUT again). Safe to call while not started. */
  void AudioStream_stop(void);

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
