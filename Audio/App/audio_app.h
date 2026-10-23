/**
 ******************************************************************************
 * @file    audio_app.h
 * @brief   ZPL-gated single-shot KWS: the VM3011 DOUT (ZPL) wake line opens
 *          a listening session (SAI PDM capture + PDM2PCM -> ring buffer ->
 *          energy VAD -> 1 s utterance capture -> mel frontend -> echodscnn
 *          inference -> single-shot print over USART1). When the session
 *          times out with no speech, the SAI is stopped and the MCU returns
 *          to STOP2 until the next ZPL wake.
 ******************************************************************************
 */
#ifndef AUDIO_APP_H
#define AUDIO_APP_H

#include <stdint.h>
#include "vm3011.h"

/* 1 = ZPL(DOUT) 唤醒 + STOP2 睡眠的省电架构（设计目标形态）。
 * 0 = 连续采集调试模式：SAI 常开、只允许 SLEEP 空闲、永不进 STOP2；
 *     DOUT 事件只打日志。远距离/门限调试期间先置 0，日志连续可见。 */
#define AUDIO_ZPL_WAKE 0

#ifdef __cplusplus
extern "C"
{
#endif

  /* Init the network, ring buffer, streaming mel frontend and KWS
   * post-processing. vm is the VM3011 handle (owned by main): its DOUT is
   * cleared on session close so the ZPL wake line re-arms. The SAI stream
   * is NOT started here - the first ZPL wake opens the first session.
   * Call once from main() USER CODE 2 (before MX_APPE_Init). */
  void AudioApp_Init(VM3011_Dev_t *vm);

  /* A VM3011 ZPL event was observed (DOUT rising edge or STOP2 wake):
   * open/extend a listening session - start the SAI stream if it was
   * stopped, and refresh the stay-awake window. */
  void AudioApp_ZplNotify(void);

  /* Call from main loop (USER CODE 3): drains the ring, watches the VAD,
   * runs the capture + inference + KWS post-processing, and closes the
   * session (stop stream, clear DOUT, allow STOP2) when it goes quiet. */
  void AudioApp_Process(void);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_APP_H */
