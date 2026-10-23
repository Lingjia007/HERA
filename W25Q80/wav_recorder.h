#ifndef WAV_RECORDER_H
#define WAV_RECORDER_H

#include <stdint.h>
#include "lfs.h"

/* 16 kHz mono S16 PCM (与 KWS 采集管线一致的音频几何） */
#define WAV_REC_SAMPLE_RATE 16000U
#define WAV_REC_CHANNELS   1U
#define WAV_REC_BITS       16U

/* 1MB flash / 32 KB/s ≈ 32.7s 上限，留 fs 余量取 28s 自动停止 */
#define WAV_REC_MAX_SECONDS 28U

/* 绑定文件系统（mount 成功后调用一次） */
void WavRecorder_Init(lfs_t *lfs);

/* 开始录音：创建 RECxxxx.WAV，写入占位 WAV 头。0 成功。 */
int WavRecorder_start(void);

/* 停止并落盘：回填最终 WAV 头、close。返回数据字节数，<0 失败。 */
int WavRecorder_stop(void);

/* 音频块喂入（10ms/160 采样，由 audio_app 的消费循环调用）。
 * 写满/出错会自动停止录音。 */
void WavRecorder_feed(const int16_t *pcm, uint32_t n);

uint8_t WavRecorder_active(void);

#endif
