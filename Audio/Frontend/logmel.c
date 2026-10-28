/*
 * LogMel 前端板载实现（CMSIS-DSP 加速 + 窗口原地填充）。
 *
 * 数值语义与 PC 验证版（logmel_c_test/logmel_cmsis.c，已对 torchaudio
 * golden PASS <=1 LSB @int8）完全一致，仅两处工程化调整:
 *  1) 不用内部 s_pad 大缓冲：reflect 填充直接写在调用方窗口两侧的
 *     LM_PAD int16 scratch 里（源与目标区间不相交，可就地写）。
 *     左:  win[-P+i] = win[P-i]        (i = 0..P-1)
 *     右:  win[N+i]  = win[N-2-i]      (i = 0..P-1)
 *  2) 每帧算完 mel 后直接按 LM_MODEL_IN_SCALE/ZP 量化 int8，省去
 *     4040 float 特征缓冲（约 16KB RAM）。
 *
 * RAM 占用: s_fft_in/s_fft_out/s_pow/s_mel ≈ 5.3KB 静态（Hann 窗直接索引
 *     tables.h 的 flash 常量 LM_HANN480，不在 RAM 里建副本）。
 * 上板 Cortex-M4F: -DARM_MATH_CM4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard。
 */
#include "logmel.h"
#include "tables.h"
#include "arm_math.h"
#include <string.h>

/* ---- 内部静态缓冲（单线程） ------------------------------------------ */
static float s_fft_in[LM_N_FFT], s_fft_out[LM_N_FFT];
static float s_pow[LM_N_BINS];
static float s_mel[LM_N_MELS];                 /* 每帧 mel 和，供向量 log */
static arm_rfft_fast_instance_f32 S_rfft;
static int   s_ready = 0;

/* 窗函数布局（原 s_win 缓冲已省）: 16 零 | hann480 | 16 零，
 * 等价于对 LM_HANN480 直接做偏移索引。 */
#define LM_WIN_LO ((LM_N_FFT - LM_WIN) / 2)
#define LM_WIN_HI (LM_WIN_LO + LM_WIN)

static void lm_setup(void)
{
    if (s_ready)
        return;
    arm_rfft_fast_init_f32(&S_rfft, LM_N_FFT);
    s_ready = 1;
}

/* round-half-away-from-zero + 饱和（与 PC 端量化/训练导出约定一致）。 */
static inline int8_t lm_quant1(float x)
{
    int v = (int)(x * (1.0f / LM_MODEL_IN_SCALE) +
                  (x >= 0.0f ? 0.5f : -0.5f)) + LM_MODEL_IN_ZP;
    if (v < -128)
        v = -128;
    if (v > 127)
        v = 127;
    return (int8_t)v;
}

void logmel_process_window(int16_t *win, int8_t *q)
{
    lm_setup();

    /* reflect 填充（就地写入两侧 scratch，源/目标不相交） */
    for (int i = 0; i < LM_PAD; i++) {
        win[-LM_PAD + i]        = win[LM_PAD - i];
        win[LM_DESIRED + i]     = win[LM_DESIRED - 2 - i];
    }

    for (int t = 0; t < LM_N_FRAMES; t++) {
        const int16_t *src = win - LM_PAD + t * LM_HOP;

        /* 融合循环: int16 -> float（×2^-15, 精确）+ Hann 加窗, 单遍完成。
         * 窗增益直接索引 flash 常量 LM_HANN480（两侧 i 越界处增益为 0）。 */
        for (int i = 0; i < LM_N_FFT; i++)
        {
            float g = (i >= LM_WIN_LO && i < LM_WIN_HI)
                          ? LM_HANN480[i - LM_WIN_LO] : 0.0f;
            s_fft_in[i] = (float)src[i] * (1.0f / 32768.0f) * g;
        }

        arm_rfft_fast_f32(&S_rfft, s_fft_in, s_fft_out, 0);  /* CMSIS 实 FFT */

        /* 打包格式: out[0]=DC, out[1]=Nyquist, 之后 Re/Im 交替 */
        s_pow[0] = s_fft_out[0] * s_fft_out[0];
        s_pow[LM_N_BINS - 1] = s_fft_out[1] * s_fft_out[1];
        for (int k = 1; k < LM_N_BINS - 1; k++)
            s_pow[k] = s_fft_out[2 * k] * s_fft_out[2 * k] +
                       s_fft_out[2 * k + 1] * s_fft_out[2 * k + 1];

        for (int b = 0; b < LM_N_MELS; b++) {  /* 稀疏 mel 投影 */
            const lm_band_t *bd = &LM_BANDS[b];
            float m = 0.0f;
            for (int j = 0; j < bd->width; j++)
                m += bd->w[j] * s_pow[bd->start_bin + j];
            s_mel[b] = m;
        }

        /* +eps 后向量快速 log，再量化并按 [mel][t] 布局散写 */
        arm_offset_f32(s_mel, 1e-6f, s_mel, LM_N_MELS);
        arm_vlog_f32(s_mel, s_mel, LM_N_MELS);
        for (int b = 0; b < LM_N_MELS; b++)
            q[b * LM_N_FRAMES + t] = lm_quant1(s_mel[b]);
    }
}

void logmel_quantize(const float *feat, int n, float scale, int zero_point,
                     int8_t *out)
{
    for (int i = 0; i < n; i++) {
        int v = (int)(feat[i] / scale +
                      (feat[i] >= 0.0f ? 0.5f : -0.5f)) + zero_point;
        if (v < -128)
            v = -128;
        if (v > 127)
            v = 127;
        out[i] = (int8_t)v;
    }
}
