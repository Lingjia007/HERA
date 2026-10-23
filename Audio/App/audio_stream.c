/**
 ******************************************************************************
 * @file    audio_stream.c
 * @brief   SAI1 Block A PDM DMA capture + PDM2PCM decimation (x128) feeding
 *          the energy VAD and the capture ring buffer.
 ******************************************************************************
 */
#include "audio_stream.h"
#include "audio_vad.h"
#include "sai.h"
#include "main.h"
#include "pdm2pcm_glo.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* Microphone slot of the PDM pair feeding the pipeline (0 = first mic,
 * 1 = second mic on the opposite clock edge). One PDM_Filter call converts
 * the slot selected by the input/output pointer offset. */
#define AS_PDM_MIC_CHANNEL 0U

/* Circular DMA buffer, byte units: 2 x 5120 bytes (~20 ms of PDM data).
 * The two 8-bit slots (microphone pair) are interleaved byte-wise. */
static uint8_t s_pdm_buf[AS_PDM_BUF_BYTES] __attribute__((aligned(4)));

/* PDM2PCM output scratch: 160 samples with out_ptr_channels = 2, i.e. the
 * converted microphone is written at every second index. */
static int16_t s_pcm_stereo[AS_DMA_HALF_FRAMES * 2U];

/* Mono block fed to the VAD / ring buffer (slot 0 microphone). */
static int16_t s_mono[AS_DMA_HALF_FRAMES];

/* Ring buffer backing store (CPU-only access). */
static int16_t s_ring_backing[AS_RING_NB_SAMPLES * AS_RING_NB_FRAMES];

static AudioCaptureRingBuff_t s_ring;

static PDM_Filter_Handler_t s_pdm;
static PDM_Filter_Config_t s_pdm_cfg;
static uint32_t s_pdm_error;

/* Last SAI error code recorded by HAL_SAI_ErrorCallback (0 = none). The HAL
 * aborts the DMA on error and calls the (default empty) weak callback - the
 * stream then dies SILENTLY; the watchdog in audio_app restarts the pipeline
 * when blocks stop arriving. */
static volatile uint32_t s_sai_error;

/* The PDM2PCM archive ships WITHOUT .ARM.attributes, so the linker accepts it
 * in this hard-float project - but its code is softfp: the libm calls inside
 * PDM_Filter_setConfig() (powf/roundf) pass their arguments in r0/r1 while the
 * hard-float libm reads s0/s1. The Q-format gain the library stores in its
 * internal memory (handler offset 56) therefore becomes 0, and every output
 * sample is computed as smlal(accumulator, 0) -> a constant 0 stream.
 *
 * Recompute the exact same value here, where the float ABI is correct:
 *   gain = round(scale * 10^(mic_gain/20)) with scale = 2^22 for x64 / x128
 *        = round(4194304 * 10^(24/20)) = 66475239.
 * pInternalMemory[11] is the 32-bit gain word at handler offset 56. */
#define PDM_Q_GAIN_X64_X128_24DB 66475239U

static void AudioStream_patch_gain(PDM_Filter_Handler_t *h)
{
  h->pInternalMemory[11] = (uint32_t *)PDM_Q_GAIN_X64_X128_24DB;
}

AudioCaptureRingBuff_t *AudioStream_get_ring(void) { return &s_ring; }

uint32_t AudioStream_get_sai_error(void) { return s_sai_error; }

void AudioStream_init(void)
{
  AudioCaptureRingBuff_reset(&s_ring,
                             (uint8_t *)s_ring_backing,
                             AS_RING_NB_SAMPLES,
                             AS_RING_NB_FRAMES,
                             sizeof(int16_t));
  s_sai_error = 0;
  audio_vad_init();

  /* The PDM2PCM library verifies its internal tables with the CRC
   * peripheral, so the CRC clock must be running before PDM_Filter_Init. */
  __HAL_RCC_CRC_CLK_ENABLE();

  /* Same PDM filter setup as the SAI_AudioRecord_PDM reference example,
   * adapted to the VM3011 2 MHz PDM clock: x128 decimation, 24 dB. */
  s_pdm.bit_order = PDM_FILTER_BIT_ORDER_MSB;
  s_pdm.endianness = PDM_FILTER_ENDIANNESS_LE;
  s_pdm.high_pass_tap = 2122358088U;
  s_pdm.in_ptr_channels = 2;
  s_pdm.out_ptr_channels = 2;
  s_pdm_error = PDM_Filter_Init(&s_pdm);

  s_pdm_cfg.decimation_factor = PDM_FILTER_DEC_FACTOR_128;
  s_pdm_cfg.output_samples_number = AS_DMA_HALF_FRAMES;
  s_pdm_cfg.mic_gain = 24;
  if (s_pdm_error == 0U)
  {
    s_pdm_error = PDM_Filter_setConfig(&s_pdm, &s_pdm_cfg);
    AudioStream_patch_gain(&s_pdm);
  }

  if (s_pdm_error != 0U)
  {
    printf("PDM2PCM FAIL=0x%08" PRIX32 "\r\n", s_pdm_error);
  }
}

void AudioStream_start(void)
{
  /* STOP2 (ZPL sleep) stops PLLSAI1, the SAI1 kernel clock. The CCIPR
   * selection is retained, so only the PLL itself must be re-enabled and
   * waited for before the SAI is fed audio bits again. Idempotent. */
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLSAI1RDY) == 0U)
  {
    __HAL_RCC_PLLSAI1_ENABLE();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLSAI1RDY) == 0U)
    {
    }
  }

  /* Circular DMA gives half + full transfer callbacks. Size counts memory
   * data units (bytes: SAI data size is 8-bit in PDM mode). */
  HAL_StatusTypeDef st = HAL_SAI_Receive_DMA(&hsai_BlockA1, s_pdm_buf,
                                             AS_PDM_BUF_BYTES);
  if (st != HAL_OK)
  {
    printf("SAI DMA FAIL=%" PRId32 "\r\n", (int32_t)st);
  }
}

void AudioStream_stop(void)
{
  /* HAL_SAI_DMAStop aborts the circular DMA and disables the SAI: the PDM
   * clock stops and the VM3011 falls back to ZPL (wake-on-sound) mode. */
  (void)HAL_SAI_DMAStop(&hsai_BlockA1);
}

/* Convert one DMA half-block to PCM and push it to the VAD / ring.
 * half_id = 0 -> first half of s_pdm_buf, 1 -> second half. */
static void AudioStream_process_half(uint8_t half_id)
{
  const uint8_t *src = s_pdm_buf + ((uint32_t)half_id * AS_PDM_HALF_BYTES) +
                       AS_PDM_MIC_CHANNEL;

  if (s_pdm_error == 0U)
  {
    /* Legacy PDM2PCM API: the pointer offsets select the microphone and
     * in/out_ptr_channels give the interleave stride. */
    if (PDM_Filter((void *)src, (void *)(s_pcm_stereo + AS_PDM_MIC_CHANNEL),
                   &s_pdm) != 0U)
    {
      s_pdm_error = 1U;
      memset(s_mono, 0, sizeof(s_mono));
    }
    else
    {
      for (uint32_t i = 0; i < AS_DMA_HALF_FRAMES; i++)
      {
        s_mono[i] = s_pcm_stereo[2U * i + AS_PDM_MIC_CHANNEL];
      }
    }
  }
  else
  {
    memset(s_mono, 0, sizeof(s_mono));
  }

  /* Raw-input energy VAD (producer side, pre-consumer). */
  audio_vad_feed_block(s_mono, AS_DMA_HALF_FRAMES);

  AudioCaptureRingBuff_feed(&s_ring, (const uint8_t *)s_mono,
                            (uint16_t)AS_DMA_HALF_FRAMES);
}

/* HAL callbacks (weak symbols overridden). Only handle SAI1 Block A. */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
    AudioStream_process_half(0);
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
    AudioStream_process_half(1);
}

/* On a SAI error the HAL aborts the DMA and calls this weak callback; the
 * default implementation is EMPTY, so without this override the stream dies
 * silently. Only record the code - no printf from IRQ context. The stall
 * watchdog in audio_app detects the stopped blocks and restarts. */
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
    s_sai_error = hsai->ErrorCode;
}
