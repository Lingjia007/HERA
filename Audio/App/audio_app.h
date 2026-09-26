/**
 ******************************************************************************
 * @file    audio_app.h
 * @brief   Audio application glue: SAI capture -> ring -> mel frontend stream
 *          -> VOFA+ (USART1) telemetry.
 ******************************************************************************
 */
#ifndef AUDIO_APP_H
#define AUDIO_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* Init ring buffer, streaming mel frontend and VOFA; start SAI DMA capture.
   * Call once from main() USER CODE 2 (before MX_APPE_Init). */
  void AudioApp_Init(void);

  /* Call from main loop (USER CODE 3): consumes audio from the ring and
   * pushes the mel frontend stream. No UART traffic here. */
  void AudioApp_Process(void);

  /* Called from the SAI DMA IRQ for every 160-sample (10 ms) block.
   * Sends one VOFA+ frame over USART1 from IRQ context: the
   * plot pacing follows the audio clock and is immune to main-loop stalls. */
  void AudioApp_OnAudioBlock(const int16_t *samples, uint32_t n);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_APP_H */
