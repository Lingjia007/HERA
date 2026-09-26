#include "audio_frontend.h"
#include "mel_filterbank_data.h"
#include <string.h>

// ============================================================
// Integer math helpers (fixed point)
// ============================================================

static int32_t most_significant_bit_32(uint32_t x) {
    if (x == 0) return 32;
    return 32 - __builtin_clz(x);
}

static int32_t most_significant_bit_64(uint64_t x) {
    if (x == 0) return 64;
    return 64 - __builtin_clzll(x);
}

static uint32_t sqrt_32(uint32_t num) {
    if (num == 0) return 0;
    uint32_t res = 0;
    uint32_t bit = 1u << 30;
    while (bit > num) bit >>= 2;
    while (bit != 0) {
        if (num >= res + bit) {
            num -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    if (num > res && res != 0xFFFF) res++;
    return res;
}

static uint64_t sqrt_64(uint64_t num) {
    if ((num >> 32) == 0) return sqrt_32((uint32_t)num);
    uint64_t res = 0;
    int32_t max_bit_number = 64 - most_significant_bit_64(num);
    max_bit_number |= 1;
    uint64_t bit = 1ULL << (63 - max_bit_number);
    int32_t iterations = (63 - max_bit_number) / 2 + 1;
    while (iterations > 0) {
        if (num >= res + bit) {
            num -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
        iterations--;
    }
    if (num > res && res != 0xFFFFFFFFULL) res++;
    return res;
}

static int32_t log2_fraction_part_32(int32_t x, int32_t log2x) {
    int32_t frac = x - (1 << log2x);
    if (log2x < K_LOG_SCALE_LOG2)
        frac <<= (K_LOG_SCALE_LOG2 - log2x);
    else
        frac >>= (log2x - K_LOG_SCALE_LOG2);

    int32_t base_seg = frac >> (K_LOG_SCALE_LOG2 - K_LOG_SEGMENTS_LOG2);
    int32_t seg_unit = (1 << K_LOG_SCALE_LOG2) >> K_LOG_SEGMENTS_LOG2;
    int32_t c0 = LOG_LUT[base_seg];
    int32_t c1 = LOG_LUT[base_seg + 1];
    int32_t seg_base = seg_unit * base_seg;
    int32_t rel_pos = ((c1 - c0) * (frac - seg_base)) >> K_LOG_SCALE_LOG2;
    return frac + c0 + rel_pos;
}

static int32_t log_32(int32_t x, int32_t out_scale) {
    int32_t integer = most_significant_bit_32((uint32_t)x) - 1;
    int32_t fraction = log2_fraction_part_32(x, integer);
    int64_t log2 = ((int64_t)integer << K_LOG_SCALE_LOG2) + fraction;
    int32_t round_val = K_LOG_SCALE / 2;
    int64_t loge = (K_LOG_COEFF * log2 + round_val) >> K_LOG_SCALE_LOG2;
    int32_t loge_scaled = (int32_t)((out_scale * loge + round_val) >> K_LOG_SCALE_LOG2);
    return loge_scaled;
}

// ============================================================
// Audio frontend implementation
// ============================================================

int32_t audio_frontend_init(AudioFrontend* fe) {
    memset(fe, 0, sizeof(AudioFrontend));
    arm_status status = arm_rfft_init_q15(&fe->rfft_inst, FFT_LENGTH, 0, 1);
    return (status == ARM_MATH_SUCCESS) ? 0 : -1;
}

void audio_frontend_reset(AudioFrontend* fe) {
    memset(fe->noise_estimate, 0, sizeof(fe->noise_estimate));
}

/* Large scratch buffers made static to avoid stack overflow: this function
 * is called 49x per inference from the main loop (not reentrant, not in IRQ),
 * so static BSS storage is safe and saves ~2.7 KB of stack per call. */
static int16_t  s_windowed[FRAME_LEN];
static uint32_t s_energy[NUM_SPECTRAL_BINS];
static uint64_t s_filterbank_output[NUM_CHANNELS + 1];
static uint32_t s_sqrt_output[NUM_CHANNELS + 1];
static uint32_t s_noise_reduced[NUM_CHANNELS];
static int16_t  s_log_output[NUM_CHANNELS];

void audio_frontend_process_frame(AudioFrontend* fe, const int16_t* audio_frame, int8_t* features) {
    int32_t i;

    // Step 1: Hann windowing
    // Manual loop: (a * b) >> WINDOW_SCALING_BITS
    // arm_mult_q15 does (a * b) >> 15 (Q15 scaling), which is wrong here
    for (i = 0; i < FRAME_LEN; i++) {
        int32_t val = ((int32_t)audio_frame[i] * HANN_WINDOW[i]) >> WINDOW_SCALING_BITS;
        s_windowed[i] = (int16_t)val;
    }

    // Step 2: FFT auto-scale — arm_absmax_q15 + arm_shift_q15
    int16_t max_val;
    uint32_t max_idx;
    arm_absmax_q15(s_windowed, FRAME_LEN, &max_val, &max_idx);
    int32_t scale_bits = 0;
    if (max_val > 0) {
        scale_bits = 16 - most_significant_bit_32((uint32_t)max_val) - 1;
        if (scale_bits < 0) scale_bits = 0;
    }
    if (scale_bits > 0) {
        arm_shift_q15(s_windowed, scale_bits, s_windowed, FRAME_LEN);
    }

    // Step 3: Zero-pad to FFT_LENGTH and RFFT — arm_rfft_q15
    memcpy(fe->fft_buffer, s_windowed, FRAME_LEN * sizeof(int16_t));
    memset(fe->fft_buffer + FRAME_LEN, 0, (FFT_LENGTH - FRAME_LEN) * sizeof(int16_t));
    arm_rfft_q15(&fe->rfft_inst, fe->fft_buffer, fe->rfft_output);

    // Step 4: Spectral energy (uint32 precision, no CMSIS-DSP primitive)
    // arm_cmplx_mag_squared_q15 outputs Q15, we need full uint32 range
    memset(s_energy, 0, sizeof(s_energy));
    for (i = SPECTRAL_START_INDEX; i < SPECTRAL_END_INDEX; i++) {
        int32_t re = fe->rfft_output[2 * i];
        int32_t im = fe->rfft_output[2 * i + 1];
        s_energy[i] = (uint32_t)((re * re + im * im) & 0xFFFFFFFF);
    }

    // Step 5: Mel filterbank accumulation (no CMSIS-DSP primitive for sparse uint64)
    uint64_t weight_accum = 0, unweight_accum = 0;
    for (i = 0; i <= NUM_CHANNELS; i++) {
        int32_t freq_start = CHANNEL_FREQUENCY_STARTS[i];
        int32_t weight_start = CHANNEL_WEIGHT_STARTS[i];
        int32_t width = CHANNEL_WIDTHS[i];
        for (int32_t j = 0; j < width; j++) {
            int32_t idx = freq_start + j;
            if (idx < NUM_SPECTRAL_BINS) {
                uint64_t val = (uint64_t)s_energy[idx];
                weight_accum += (uint64_t)(uint16_t)MEL_WEIGHTS[weight_start + j] * val;
                unweight_accum += (uint64_t)(uint16_t)MEL_UNWEIGHTS[weight_start + j] * val;
            }
        }
        s_filterbank_output[i] = weight_accum;
        weight_accum = unweight_accum;
        unweight_accum = 0;
    }

    // Step 6: Square root scaling (custom sqrt_64, no CMSIS-DSP for uint64)
    for (i = 0; i <= NUM_CHANNELS; i++) {
        s_sqrt_output[i] = (uint32_t)(sqrt_64(s_filterbank_output[i]) >> scale_bits);
    }

    // Step 7: Spectral subtraction (uint32 overflow wrapping, no CMSIS-DSP primitive)
    for (i = 0; i < NUM_CHANNELS; i++) {
        int32_t smoothing, one_minus_smoothing;
        if ((i & 1) == 0) {
            smoothing = EVEN_SMOOTHING;
            one_minus_smoothing = ONE_MINUS_EVEN;
        } else {
            smoothing = ODD_SMOOTHING;
            one_minus_smoothing = ONE_MINUS_ODD;
        }

        uint32_t signal_scaled_up = s_sqrt_output[i] << SMOOTHING_BITS;
        uint32_t product1 = (uint32_t)(((uint64_t)signal_scaled_up * smoothing) & 0xFFFFFFFF);
        uint32_t product2 = (uint32_t)(((uint64_t)fe->noise_estimate[i] * one_minus_smoothing) & 0xFFFFFFFF);
        fe->noise_estimate[i] = (uint32_t)((((uint64_t)product1 + product2) & 0xFFFFFFFF)) >> SPECTRAL_SUBTRACTION_BITS;

        uint32_t estimate_scaled_up = fe->noise_estimate[i];
        if (estimate_scaled_up > signal_scaled_up) {
            estimate_scaled_up = signal_scaled_up;
        }

        uint32_t floor_val = (uint32_t)(((uint64_t)s_sqrt_output[i] * MIN_SIGNAL_INT) >> SPECTRAL_SUBTRACTION_BITS);
        uint32_t subtracted = (signal_scaled_up - estimate_scaled_up) >> SMOOTHING_BITS;
        s_noise_reduced[i] = (subtracted > floor_val) ? subtracted : floor_val;
    }

    // Step 8: Log scaling (custom LUT-based log, no CMSIS-DSP equivalent)
    for (i = 0; i < NUM_CHANNELS; i++) {
        int32_t val = (int32_t)s_noise_reduced[i];
        if (val > 0) {
            s_log_output[i] = (int16_t)log_32(val, 6);
            if (s_log_output[i] > 32767) s_log_output[i] = 32767;
        } else {
            s_log_output[i] = 0;
        }
    }

    // Step 9: Int8 quantization (simple arithmetic, no CMSIS-DSP benefit)
    for (i = 0; i < NUM_CHANNELS; i++) {
        int32_t feat = ((int32_t)s_log_output[i] * INT8_VALUE_SCALE + INT8_VALUE_DIV / 2) / INT8_VALUE_DIV;
        feat -= INT8_OFFSET;
        if (feat < -128) feat = -128;
        if (feat > 127) feat = 127;
        features[i] = (int8_t)feat;
    }
}

void audio_frontend_process_clip(AudioFrontend* fe, const int16_t* audio, int32_t audio_len, int8_t* features_2d) {
    int32_t num_frames = 1 + (DESIRED_SAMPLES - FRAME_LEN) / WINDOW_STRIDE;
    for (int32_t f = 0; f < num_frames; f++) {
        int32_t start = f * WINDOW_STRIDE;
        const int16_t* frame = audio + start;
        audio_frontend_process_frame(fe, frame, features_2d + f * NUM_CHANNELS);
    }
}

/* ============================================================
 * Streaming frontend: sliding 1-second window
 * ============================================================ */

void audio_frontend_stream_init(AudioFrontendStream* s) {
    memset(s, 0, sizeof(*s));
    audio_frontend_init(&s->fe);
    /* Do NOT reset noise_estimate between calls - it accumulates. */
}

int32_t audio_frontend_stream_push(AudioFrontendStream* s, const int16_t* samples, int32_t n) {
    if (n <= 0 || n > DESIRED_SAMPLES) return 0;

    if (s->samples_collected < DESIRED_SAMPLES) {
        /* Initial fill phase: append samples until the window is full. */
        int32_t space = DESIRED_SAMPLES - s->samples_collected;
        int32_t to_copy = (n < space) ? n : space;
        memcpy(s->audio_window + s->samples_collected, samples,
               (size_t)to_copy * sizeof(int16_t));
        s->samples_collected += to_copy;

        if (s->samples_collected < DESIRED_SAMPLES)
            return 0;

        /* Window just filled: compute features for the first time. */
        audio_frontend_process_clip(&s->fe, s->audio_window, DESIRED_SAMPLES,
                                    s->features);
        s->warmed_up = 1;
        return 1;
    }

    /* Steady state: slide the window left by n samples, append n new ones. */
    memmove(s->audio_window,
            s->audio_window + n,
            (size_t)(DESIRED_SAMPLES - n) * sizeof(int16_t));
    memcpy(s->audio_window + (DESIRED_SAMPLES - n), samples,
           (size_t)n * sizeof(int16_t));

    /* Recompute the full 49-column spectrogram (noise_estimate persists). */
    audio_frontend_process_clip(&s->fe, s->audio_window, DESIRED_SAMPLES,
                                s->features);
    return 1;
}

const int8_t* audio_frontend_stream_get_features(const AudioFrontendStream* s) {
    return s->features;
}
