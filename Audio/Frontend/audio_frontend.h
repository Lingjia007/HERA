#ifndef AUDIO_FRONTEND_H
#define AUDIO_FRONTEND_H

#include <stdint.h>
#include "arm_math.h"
#include "audio_params.h"

typedef struct {
    arm_rfft_instance_q15 rfft_inst;
    uint32_t noise_estimate[NUM_CHANNELS];
    int16_t fft_buffer[FFT_LENGTH];
    int16_t rfft_output[2 * FFT_LENGTH];
} AudioFrontend;

int32_t audio_frontend_init(AudioFrontend* fe);
void audio_frontend_reset(AudioFrontend* fe);
void audio_frontend_process_frame(AudioFrontend* fe, const int16_t* audio_frame, int8_t* features);
void audio_frontend_process_clip(AudioFrontend* fe, const int16_t* audio, int32_t audio_len, int8_t* features_2d);

/* ---- Streaming API ------------------------------------------------------- */
/* Sliding 1-second window with persistent noise estimate. Each push slides
 * the window by the number of new samples and recomputes the full 49-column
 * spectrogram. The first inference fires once the window is full. */
typedef struct {
    AudioFrontend fe;                                       /* noise_estimate persists  */
    int16_t       audio_window[DESIRED_SAMPLES];            /* 1-second sliding window  */
    int8_t        features[SPECTROGRAM_LENGTH * NUM_CHANNELS]; /* 49 x 40 features      */
    int32_t       samples_collected;                        /* fill progress (0..DESIRED_SAMPLES) */
    int32_t       warmed_up;                                /* 1 once first window is full */
} AudioFrontendStream;

void audio_frontend_stream_init(AudioFrontendStream* s);

/* Push n new mono samples. Returns 1 if features are freshly computed and
 * ready for inference, 0 if the window is still filling. n should normally
 * equal SLIDE_SAMPLES once streaming is steady. */
int32_t  audio_frontend_stream_push(AudioFrontendStream* s, const int16_t* samples, int32_t n);

/* Pointer to the 49*40 int8 feature buffer (row-major: [col][channel]). */
const int8_t* audio_frontend_stream_get_features(const AudioFrontendStream* s);

#endif // AUDIO_FRONTEND_H
