/**
 ******************************************************************************
 * @file    vm3011.h
 * @brief   VM3011 (Vesper) low-power PDM digital MEMS microphone I2C control
 *          driver, ported from the Zephyr driver (SPDX-License-Identifier:
 *          Apache-2.0, Copyright (c) 2017 IpTronix S.r.l.) to the STM32 HAL
 *          I2C API.
 *
 * Scope of the port:
 *  - Register-level I2C control (WoS thresholds, bandpass filter, WDT,
 *    fast-mode count, DOUT clear, ambient level read) is fully preserved.
 *  - The Zephyr/nrfx PDM data path is NOT ported: on STM32WB55 the PDM
 *    audio stream is captured by SAI in PDM mode (see Audio/App), while
 *    this driver handles the I2C control plane only.
 *
 * PDM clocking / operating modes (host SAI responsibility, datasheet):
 *  - PDM clock stopped             -> ZPL (ZeroPower Listening; the DOUT pin
 *                                     is raised when the acoustic input
 *                                     exceeds the WoS threshold)
 *  - 200 kHz .. 250 kHz            -> STANDBY
 *  - 350 kHz .. 900 kHz            -> LOW_POWER
 *  - 1.1 MHz  .. 4 MHz             -> NORMAL
 * Supported output rates (decimation 64, or 80 for 4.8/16 kHz):
 *   4800 (80x, 384 kHz), 6000 (64x, 384 kHz), 8000 (64x, 512 kHz),
 *   9600 (64x, 614.4 kHz), 11025 (64x, 705.6 kHz), 12000 (64x, 768 kHz)
 *   -> LOW_POWER; 16000 (80x, 1.28 MHz), 32000 (64x, 2.048 MHz),
 *   44100 (64x, 2.822 MHz), 48000 (64x, 3.072 MHz) -> NORMAL.
 *
 * Usage example:
 *   VM3011_Dev_t mic;
 *   VM3011_Config_t cfg;
 *   VM3011_ConfigDefault(&cfg);             // Zephyr driver defaults
 *   cfg.wos_dout_thresh = 3;                 // optional overrides
 *   if (VM3011_Init(&mic, &hi2c1, VM3011_I2C_ADDR_DEFAULT, &cfg)
 *       == VM3011_OK) { ... }
 ******************************************************************************
 */
#ifndef VM3011_H
#define VM3011_H

#include "stm32wbxx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- Return codes -------------------------------------------------------- */
#define VM3011_OK         0    /* success                                     */
#define VM3011_ERR_I2C   (-1)  /* HAL I2C transfer failed / timed out         */
#define VM3011_ERR_PARAM (-2) /* invalid parameter                           */
#define VM3011_ERR_NODEV (-3) /* device not detected (I2C_CNTRL != 0x00)     */

/* ---- I2C address (7-bit), selected by the LR pin --------------------------
 * LR = GND -> 0x60 (left channel, PDM data on falling CLK edge)
 * LR = VDD -> 0x61 (right channel, PDM data on rising CLK edge)
 * The driver shifts the address internally for the HAL. */
#define VM3011_I2C_ADDR_L        0x60U
#define VM3011_I2C_ADDR_R        0x61U
#define VM3011_I2C_ADDR_DEFAULT  VM3011_I2C_ADDR_L

/* I2C transaction timeout (ms), overridable from build flags */
#ifndef VM3011_I2C_TIMEOUT_MS
#define VM3011_I2C_TIMEOUT_MS    100U
#endif

/* Startup delay (ms): wait for ZPL to settle before talking to the device */
#ifndef VM3011_STARTUP_DELAY_MS
#define VM3011_STARTUP_DELAY_MS  1000U
#endif

/* ---- User-programmable registers ---------------------------------------- */
#define VM3011_REG_I2C_CNTRL                 0x00U
#define VM3011_REG_WOS_PGA_GAIN              0x01U
#define VM3011_REG_WOS_FILTER                0x02U
#define VM3011_REG_WOS_PGA_MIN_THR           0x03U
#define VM3011_REG_WOS_PGA_MAX_THR           0x04U
#define VM3011_REG_WOS_THRESHOLD             0x05U

/* ---- Bit field positions / masks ---------------------------------------- */
#define VM3011_DOUT_CLEAR_Pos               (4U)
#define VM3011_WDT_DELAY_Pos                 (1U)
#define VM3011_WOS_BANDPASS_HPF_Pos          (2U)
#define VM3011_FAST_MODE_CNT_Pos             (5U)
#define VM3011_WOS_RMS_Pos                   (5U)

#define VM3011_DOUT_CLEAR_Msk               (0x01U << 4)
#define VM3011_WDT_DELAY_Msk                 (0x03U << 1)
#define VM3011_WDT_ENABLE_Msk                (0x01U << 0)

#define VM3011_WOS_PGA_GAIN_Msk              (0x1FU << 0)
#define VM3011_WOS_BANDPASS_LPF_Msk          (0x03U << 0)
#define VM3011_WOS_BANDPASS_HPF_Msk          (0x03U << 2)

#define VM3011_FAST_MODE_CNT_Msk             (0x03U << 5)
#define VM3011_WOS_PGA_MIN_THR_Msk           (0x1FU << 0)

#define VM3011_WOS_RMS_Msk                   (0x01U << 5)
#define VM3011_WOS_PGA_MAX_THR_Msk           (0x1FU << 0)

#define VM3011_WOS_THRESHOLD_Msk            (0x07U << 0)

/* ---- Programmable Gain Amplifier gain LUT calculator -------------------- */
#define VM3011_PGA_MAX_DB                    91.5f
#define VM3011_PGA_DB_STEPS                  1.5f
#define VM3011_PGA_TO_DB(x)                  \
  (VM3011_PGA_MAX_DB - (((uint8_t)((x) & VM3011_WOS_PGA_GAIN_Msk)) * VM3011_PGA_DB_STEPS))
#define VM3011_DB_TO_PGA(x)                  \
  ((uint8_t)((VM3011_PGA_MAX_DB - (x)) / VM3011_PGA_DB_STEPS))

/* ---- Operating modes vs PDM clock frequency (see file header) ------------ */
#define VM3011_STANDBY_MODE_MIN              200000UL
#define VM3011_STANDBY_MODE_MAX              250000UL

#define VM3011_LOW_POWER_MODE_MIN            350000UL
#define VM3011_LOW_POWER_MODE_MAX            900000UL

#define VM3011_NORMAL_MODE_MIN               1100000UL
#define VM3011_NORMAL_MODE_MAX               4000000UL

typedef enum
{
  VM3011_MODE_ZPL = 0,   /* PDM clock stopped                             */
  VM3011_MODE_STANDBY,   /* 200 kHz .. 250 kHz                            */
  VM3011_MODE_LOW_POWER, /* 350 kHz .. 900 kHz                            */
  VM3011_MODE_NORMAL      /* 1.1 MHz  .. 4 MHz                             */
} VM3011_OpMode_t;

/* ---- WOS DOUT threshold margins (register 0x05 value -> margin/dBSPL) --- */
/*  1 -> 2x -> 6.0 dBSPL   2 -> 3x -> 9.5 dBSPL   3 -> 4x -> 12.0 dBSPL
 *  4 -> 5x -> 14.0 dBSPL 5 -> 6x -> 15.5 dBSPL  6 -> 7x -> 16.9 dBSPL
 *  7 -> 8x -> 18.0 dBSPL */

typedef enum
{
  VM3011_HZ_4800 = 4800UL,
  VM3011_HZ_6000 = 6000UL,
  VM3011_HZ_8000 = 8000UL,
  VM3011_HZ_9600 = 9600UL,
  VM3011_HZ_11025 = 11025UL,
  VM3011_HZ_12000 = 12000UL,
  VM3011_HZ_16000 = 16000UL,
  VM3011_HZ_32000 = 32000UL,
  VM3011_HZ_44100 = 44100UL,
  VM3011_HZ_48000 = 48000UL
} VM3011_SampleRate_t;

/* ---- Device handle ------------------------------------------------------- */
typedef struct
{
  I2C_HandleTypeDef *hi2c;  /* HAL I2C handle, e.g. &hi2c1 (from CubeMX)    */
  uint16_t addr7;           /* 7-bit I2C address (VM3011_I2C_ADDR_L/_R)    */
} VM3011_Dev_t;

/* ---- Configuration (replaces the Zephyr Kconfig options) ---------------- */
typedef struct
{
  bool wdt_enable;          /* watchdog timer for the I2C logic            */
  uint8_t wdt_delay;        /* 0..3 -> 8 / 16 / 32 / 64 ms                 */
  uint8_t wos_lpf;          /* WOS bandpass low-pass:  0..3 -> 2/4/6/8 kHz */
  uint8_t wos_hpf;          /* WOS bandpass high-pass: 0..3 -> 200/300/400/800 */
  uint8_t fast_mode_cnt;   /* 0 = disabled; 1..3 = 2/4/6 comparator trips  */
  uint8_t wos_rms;          /* DOUT comparator sampling: 0 -> 1 Hz, 1 -> 2 Hz */
  uint8_t wos_dout_thresh; /* DOUT margin above ambient noise, 1..7 (see table) */
  float pga_min_thr_db;     /* min PGA tracking threshold, dB SPL (default 45) */
  float pga_max_thr_db;     /* max PGA tracking threshold, dB SPL (default 90) */
} VM3011_Config_t;

/* Fill cfg with the Zephyr driver defaults (WDT off, LPF 2 kHz, HPF 800,
 * fast mode off, RMS 1 Hz, DOUT margin 1, 45/90 dB SPL). */
void VM3011_ConfigDefault(VM3011_Config_t *cfg);

/* ---- API ----------------------------------------------------------------- */

/* Bind dev to a HAL I2C handle, wait for ZPL settle, detect the device
 * (VM3011_REG_I2C_CNTRL must read 0x00) and apply cfg (NULL = defaults).
 * Returns VM3011_OK / VM3011_ERR_PARAM / VM3011_ERR_I2C / VM3011_ERR_NODEV. */
int VM3011_Init(VM3011_Dev_t *dev, I2C_HandleTypeDef *hi2c, uint16_t i2c_addr7,
                const VM3011_Config_t *cfg);

/* Raw 8-bit register access (read-modify-write helpers below build on these) */
int VM3011_ReadReg(VM3011_Dev_t *dev, uint8_t reg, uint8_t *val);
int VM3011_WriteReg(VM3011_Dev_t *dev, uint8_t reg, uint8_t val);

/* Clear the DOUT indication (sets DOUT_CLEAR in I2C_CNTRL, other bits kept) */
int VM3011_ClearDout(VM3011_Dev_t *dev);

/* Configure the watchdog timer that recovers the I2C logic if the host
 * stalls SCL mid-transaction. wdt_dly: 0..3 -> 8/16/32/64 ms. */
int VM3011_WdtConfigure(VM3011_Dev_t *dev, uint8_t wdt_dly, bool enable);

/* Set the min/max PGA tracking thresholds, in dB SPL (0 .. 91.5, 1.5 dB/step).
 * The ambient tracking loop keeps the PGA gain inside [min, max]. */
int VM3011_WosPgaMinThrSet(VM3011_Dev_t *dev, float pga_gain_db);
int VM3011_WosPgaMaxThrSet(VM3011_Dev_t *dev, float pga_gain_db);

/* Set the WoS bandpass filter corners:
 * wos_lpf_freq 0..3 -> 2/4/6/8 kHz
 * wos_hpf_freq 0..3 -> 200/300/400/800 */
int VM3011_BpfSet(VM3011_Dev_t *dev, uint8_t wos_lpf_freq, uint8_t wos_hpf_freq);

/* Fast Mode Count: number of window comparator trips (same direction) before
 * the ZPL feedback loop clock speeds up x16. 0 disables. */
int VM3011_FastModeCntSet(VM3011_Dev_t *dev, uint8_t fast_mode_cnt);

/* DOUT comparator low-pass corner: 0 -> 1 Hz, 1 -> 2 Hz */
int VM3011_WosRmsSet(VM3011_Dev_t *dev, uint8_t value);

/* Margin above the average acoustic noise level that triggers DOUT (1..7,
 * see the dBSPL table above). 0 is invalid. */
int VM3011_WosDoutThreshSet(VM3011_Dev_t *dev, uint8_t wos_thresh);

/* Current ambient sound level estimate, in dB SPL (from the PGA gain LUT) */
int VM3011_AmbSoundLvlGet(VM3011_Dev_t *dev, float *amb_sound_lvl_db);

/* Raw current PGA gain code (register 0x01, bits 4:0) */
int VM3011_PgaGainGet(VM3011_Dev_t *dev, uint8_t *pga_gain);

#ifdef __cplusplus
}
#endif
#endif /* VM3011_H */
