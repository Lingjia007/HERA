/**
 ******************************************************************************
 * @file    audio_app.h
 * @brief   Streaming speech recognition glue: SAI capture -> ring ->
 *          mel frontend stream -> echodscnn inference -> KWS post-processing
 *          (judgment/print over USART1).
 ******************************************************************************
 */
#ifndef AUDIO_APP_H
#define AUDIO_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* Init network, ring buffer, streaming mel frontend and KWS post-process;
   * start SAI DMA capture. Call once from main() USER CODE 2 (before
   * MX_APPE_Init). */
  void AudioApp_Init(void);

  /* Call from main loop (USER CODE 3): drains the ring, slides the frontend,
   * runs inference and KWS post-processing (debounce + cooldown + print). */
  void AudioApp_Process(void);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_APP_H */
