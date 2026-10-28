#ifndef LOGMEL_H
#define LOGMEL_H
/*
 * torchaudio MelSpectrogram(sr=16000, hop=160, win=480, n_fft=512, n_mels=40)
 * + log(x + 1e-6) 的纯 C 板载版（CMSIS-DSP 加速）。
 *
 * 与训练/PC 验证版（logmel_c_test）数值一致，区别仅在接口与存储:
 *   - 不再使用内部 33KB 填充缓冲：窗口由调用方提供，且窗口两侧各预留
 *     LM_PAD 个 int16 scratch，reflect 填充就地产物（audio_app.c 的
 *     s_capture 天然满足该布局，见 capture_finish()）。
 *   - 帧循环复用 logmel_cmsis.c 的融合实现（arm_rfft_fast_f32 + 向量 log）。
 *
 * 输入: win 指向 1 s int16 PCM 窗口（LM_DESIRED 样本，不足调用方补零），
 *       win[-LM_PAD .. -1] 与 win[LM_DESIRED .. LM_DESIRED+LM_PAD) 为 scratch，
 *       内容任意（reflect 填充会覆盖它们）。
 * 输出: q 为 int8 [40][101]（mel 维在前，与模型输入 [1,40,101] 逐字节一致），
 *       量化参数 LM_MODEL_IN_SCALE / LM_MODEL_IN_ZP（模型入口 QLinear）。
 */
#include <stdint.h>

#define LM_SR        16000
#define LM_DESIRED   16000                     /* 1 s */
#define LM_N_FFT     512
#define LM_WIN       480
#define LM_HOP       160
#define LM_N_MELS    40
#define LM_N_FRAMES  101                       /* 1 + LM_DESIRED/LM_HOP */
#define LM_N_BINS    257                       /* LM_N_FFT/2 + 1 */
#define LM_PAD       (LM_N_FFT / 2)            /* center=True: reflect 填充 256 */

/* 模型输入量化参数（edge_quant 全整型, 来自 tflite input tensor）。
 * q = round(x / scale) + zero_point, 饱和到 int8。 */
#define LM_MODEL_IN_SCALE 0.086168326f
#define LM_MODEL_IN_ZP    32

typedef struct {
    int start_bin;   /* 滤波器覆盖的第一个 FFT bin */
    int width;       /* 覆盖的 bin 数 */
    const float *w;  /* weights[width]（tables.h LM_BAND_W 切片） */
} lm_band_t;

/* 计算 1 s 窗口的 log-mel 并直接量化为 int8 特征（LM_N_MELS * LM_N_FRAMES）。 */
void logmel_process_window(int16_t *win, int8_t *q);

/* float 特征独立量化（调试/自检用）: q = round(f / scale) + zero_point。 */
void logmel_quantize(const float *feat, int n, float scale, int zero_point,
                     int8_t *out);

#endif /* LOGMEL_H */
