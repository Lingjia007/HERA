#ifndef AUDIO_PARAMS_H
#define AUDIO_PARAMS_H

// Audio parameters
#define SAMPLE_RATE          16000
#define WINDOW_SIZE_MS       30
#define WINDOW_STRIDE_MS     20
#define DESIRED_SAMPLES      16000   // SAMPLE_RATE * 1000 / 1000
#define FRAME_LEN            480     // SAMPLE_RATE * WINDOW_SIZE_MS / 1000
#define WINDOW_STRIDE        320     // SAMPLE_RATE * WINDOW_STRIDE_MS / 1000
#define FFT_LENGTH           512     // next pow2 >= FRAME_LEN
#define NUM_CHANNELS         40
#define SPECTROGRAM_LENGTH   49      // 1 + (DESIRED_SAMPLES - FRAME_LEN) / WINDOW_STRIDE
#define NUM_SPECTRAL_BINS    257     // FFT_LENGTH / 2 + 1
#define LOWER_BAND_HZ        125
#define UPPER_BAND_HZ        7500

// Streaming inference parameters: every SLIDE_SAMPLES of new audio, the
// 1-second window slides and inference runs. SLIDE_COLS is the number of
// spectrogram columns advanced per slide (= SLIDE_SAMPLES / WINDOW_STRIDE).
#define SLIDE_COLS           10
#define SLIDE_SAMPLES        (SLIDE_COLS * WINDOW_STRIDE)   // 3200 samples = 200 ms
#define SLIDE_MS             (SLIDE_SAMPLES * 1000 / SAMPLE_RATE)  // 200 ms

// Window / filter bank scaling
#define WINDOW_SCALING_BITS          12
#define FILTER_BANK_SCALING_BITS     12
#define FILTER_BANK_POST_SCALING_BITS 6
#define FILTER_BANK_WEIGHT_SCALING_BITS 12

// Spectral subtraction
#define SPECTRAL_SUBTRACTION_BITS    14
#define SMOOTHING_BITS               10

// Int8 quantization for raw features: [45,80] -> [-128,127]
// int8 = (log * 255 + 17) / 35 - 455
#define INT8_VALUE_SCALE     255
#define INT8_VALUE_DIV       35
#define INT8_OFFSET          455     // 45 * 255 / 35 + 128

// Feature enhancement
#define FEATURE_QUANT_SCALE       0.02243382f
#define FEATURE_QUANT_ZERO_POINT  (-64)
#define FEATURE_QUANT_SCALE_Q31   48176256
#define FEATURE_QUANT_SCALE_Q15   735
#define DELTA_WINDOW             2
#define DELTA_DENOM              10  // 2 * (1^2 + 2^2)

// Dequantization: int8 raw -> float32
// float_val = (int8_val + 128) * 26.0 / 255.0
#define DEQUANT_SCALE       (26.0f / 255.0f)  // ~0.10196
#define DEQUANT_OFFSET      128

#endif // AUDIO_PARAMS_H
