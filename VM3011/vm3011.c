/**
 ******************************************************************************
 * @file    vm3011.c
 * @brief   VM3011 (Vesper) PDM digital MEMS microphone I2C control driver,
 *          STM32 HAL implementation. Ported from the Zephyr driver
 *          (SPDX-License-Identifier: Apache-2.0,
 *           Copyright (c) 2017 IpTronix S.r.l.).
 *
 * The original read-modify-write register logic is preserved one-to-one;
 * the Zephyr i2c_reg_read/write_byte calls are replaced by
 * HAL_I2C_Mem_Read/HAL_I2C_Mem_Write. The nrfx PDM data path is not ported:
 * on this board PDM audio is captured by SAI (see Audio/App).
 ******************************************************************************
 */
#include "vm3011.h"

/* ---- Raw I2C register access -------------------------------------------- */

int VM3011_ReadReg(VM3011_Dev_t *dev, uint8_t reg, uint8_t *val)
{
  if ((dev == NULL) || (dev->hi2c == NULL) || (val == NULL))
  {
    return VM3011_ERR_PARAM;
  }

  if (HAL_I2C_Mem_Read(dev->hi2c, (uint16_t)(dev->addr7 << 1), reg,
                       I2C_MEMADD_SIZE_8BIT, val, 1U,
                       VM3011_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return VM3011_ERR_I2C;
  }
  return VM3011_OK;
}

int VM3011_WriteReg(VM3011_Dev_t *dev, uint8_t reg, uint8_t val)
{
  if ((dev == NULL) || (dev->hi2c == NULL))
  {
    return VM3011_ERR_PARAM;
  }

  if (HAL_I2C_Mem_Write(dev->hi2c, (uint16_t)(dev->addr7 << 1), reg,
                        I2C_MEMADD_SIZE_8BIT, &val, 1U,
                        VM3011_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return VM3011_ERR_I2C;
  }
  return VM3011_OK;
}

/* ---- Register helpers (logic ported 1:1 from the Zephyr driver) --------- */

int VM3011_ClearDout(VM3011_Dev_t *dev)
{
  uint8_t buff;
  int ret = VM3011_ReadReg(dev, VM3011_REG_I2C_CNTRL, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  buff |= (uint8_t)(0xFFU & VM3011_DOUT_CLEAR_Msk);
  return VM3011_WriteReg(dev, VM3011_REG_I2C_CNTRL, buff);
}

int VM3011_WdtConfigure(VM3011_Dev_t *dev, uint8_t wdt_dly, bool enable)
{
  uint8_t value = (uint8_t)((wdt_dly << VM3011_WDT_DELAY_Pos) & VM3011_WDT_DELAY_Msk);
  uint8_t buff;

  if (enable)
  {
    value |= (uint8_t)(0xFFU & VM3011_WDT_ENABLE_Msk);
  }

  int ret = VM3011_ReadReg(dev, VM3011_REG_I2C_CNTRL, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  buff &= (uint8_t)~(VM3011_WDT_DELAY_Msk | VM3011_WDT_ENABLE_Msk);
  buff |= value;
  return VM3011_WriteReg(dev, VM3011_REG_I2C_CNTRL, buff);
}

int VM3011_WosPgaMinThrSet(VM3011_Dev_t *dev, float pga_gain_db)
{
  uint8_t buff;
  int ret = VM3011_ReadReg(dev, VM3011_REG_WOS_PGA_MIN_THR, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  uint8_t pga_min_gain = VM3011_DB_TO_PGA(pga_gain_db);

  buff &= (uint8_t)~VM3011_WOS_PGA_MIN_THR_Msk;
  buff |= (uint8_t)(pga_min_gain & VM3011_WOS_PGA_MIN_THR_Msk);

  return VM3011_WriteReg(dev, VM3011_REG_WOS_PGA_MIN_THR, buff);
}

int VM3011_WosPgaMaxThrSet(VM3011_Dev_t *dev, float pga_gain_db)
{
  uint8_t buff;
  int ret = VM3011_ReadReg(dev, VM3011_REG_WOS_PGA_MAX_THR, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  uint8_t pga_max_gain = VM3011_DB_TO_PGA(pga_gain_db);

  buff &= (uint8_t)~VM3011_WOS_PGA_MAX_THR_Msk;
  buff |= (uint8_t)(pga_max_gain & VM3011_WOS_PGA_MAX_THR_Msk);

  return VM3011_WriteReg(dev, VM3011_REG_WOS_PGA_MAX_THR, buff);
}

int VM3011_BpfSet(VM3011_Dev_t *dev, uint8_t wos_lpf_freq, uint8_t wos_hpf_freq)
{
  uint8_t buff = (uint8_t)((wos_lpf_freq & VM3011_WOS_BANDPASS_LPF_Msk) |
                           ((wos_hpf_freq << VM3011_WOS_BANDPASS_HPF_Pos) &
                            VM3011_WOS_BANDPASS_HPF_Msk));

  return VM3011_WriteReg(dev, VM3011_REG_WOS_FILTER, buff);
}

int VM3011_FastModeCntSet(VM3011_Dev_t *dev, uint8_t fast_mode_cnt)
{
  uint8_t buff;
  int ret = VM3011_ReadReg(dev, VM3011_REG_WOS_PGA_MIN_THR, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  buff &= (uint8_t)~VM3011_FAST_MODE_CNT_Msk;
  buff |= (uint8_t)((fast_mode_cnt << VM3011_FAST_MODE_CNT_Pos) & VM3011_FAST_MODE_CNT_Msk);

  return VM3011_WriteReg(dev, VM3011_REG_WOS_PGA_MIN_THR, buff);
}

int VM3011_WosRmsSet(VM3011_Dev_t *dev, uint8_t value)
{
  uint8_t buff;
  int ret = VM3011_ReadReg(dev, VM3011_REG_WOS_PGA_MAX_THR, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  buff &= (uint8_t)~VM3011_WOS_RMS_Msk;
  buff |= (uint8_t)((value << VM3011_WOS_RMS_Pos) & VM3011_WOS_RMS_Msk);

  return VM3011_WriteReg(dev, VM3011_REG_WOS_PGA_MAX_THR, buff);
}

int VM3011_WosDoutThreshSet(VM3011_Dev_t *dev, uint8_t wos_thresh)
{
  /* WOS_THRESH register value should not be 0 */
  if (wos_thresh == 0U)
  {
    return VM3011_ERR_PARAM;
  }

  uint8_t buff = (uint8_t)(wos_thresh & VM3011_WOS_THRESHOLD_Msk);
  return VM3011_WriteReg(dev, VM3011_REG_WOS_THRESHOLD, buff);
}

int VM3011_AmbSoundLvlGet(VM3011_Dev_t *dev, float *amb_sound_lvl_db)
{
  if (amb_sound_lvl_db == NULL)
  {
    return VM3011_ERR_PARAM;
  }

  uint8_t buff;
  int ret = VM3011_ReadReg(dev, VM3011_REG_WOS_PGA_GAIN, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  /* Translate PGA gain to dB using the LUT */
  *amb_sound_lvl_db = VM3011_PGA_TO_DB(buff);
  return VM3011_OK;
}

int VM3011_PgaGainGet(VM3011_Dev_t *dev, uint8_t *pga_gain)
{
  if (pga_gain == NULL)
  {
    return VM3011_ERR_PARAM;
  }

  uint8_t buff;
  int ret = VM3011_ReadReg(dev, VM3011_REG_WOS_PGA_GAIN, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  *pga_gain = (uint8_t)(buff & VM3011_WOS_PGA_GAIN_Msk);
  return VM3011_OK;
}

/* ---- Init ---------------------------------------------------------------- */

void VM3011_ConfigDefault(VM3011_Config_t *cfg)
{
  if (cfg == NULL)
  {
    return;
  }

  cfg->wdt_enable = false;     /* watchdog timer disabled                 */
  cfg->wdt_delay = 0U;         /* 8 ms (only used when WDT is enabled)    */
  cfg->wos_lpf = 0U;           /* low-pass  2 kHz                         */
  cfg->wos_hpf = 3U;           /* high-pass 800                           */
  cfg->fast_mode_cnt = 0U;     /* fast mode disabled                      */
  cfg->wos_rms = 0U;           /* comparator low-pass 1 Hz                 */
  cfg->wos_dout_thresh = 1U;   /* 2x margin, 6.0 dBSPL                    */
  cfg->pga_min_thr_db = 45.0f; /* quiet library                           */
  cfg->pga_max_thr_db = 90.0f; /* truck / loud speaker                    */
}

int VM3011_Init(VM3011_Dev_t *dev, I2C_HandleTypeDef *hi2c, uint16_t i2c_addr7,
                const VM3011_Config_t *cfg)
{
  if ((dev == NULL) || (hi2c == NULL))
  {
    return VM3011_ERR_PARAM;
  }

  dev->hi2c = hi2c;
  dev->addr7 = (uint16_t)(i2c_addr7 & 0x7FU);

  /* Wait for ZPL to settle properly before initiating communication. */
  HAL_Delay(VM3011_STARTUP_DELAY_MS);

  /* As the device does not contain a chip ID register, read the I2C_CNTRL
   * register. The value 0x00 should return. */
  uint8_t buff;
  int ret = VM3011_ReadReg(dev, VM3011_REG_I2C_CNTRL, &buff);
  if (ret != VM3011_OK)
  {
    return ret;
  }
  if (buff != 0x00U)
  {
    return VM3011_ERR_NODEV;
  }

  VM3011_Config_t c;
  if (cfg != NULL)
  {
    c = *cfg;
  }
  else
  {
    VM3011_ConfigDefault(&c);
  }

  ret = VM3011_WdtConfigure(dev, c.wdt_delay, c.wdt_enable);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  ret = VM3011_BpfSet(dev, c.wos_lpf, c.wos_hpf);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  /* NOTE: fast-mode count (reg03 bits6:5) and PGA MIN/MAX thresholds
   * (reg03/04 bits4:0) are left at FACTORY defaults. Measured on HW: writing
   * DB_TO_PGA()-encoded values (03=1F/04=01) inverted the tracking band and
   * pinned the PGA at code 1 (~90 dB, stuck); factory 03=40/04=1F tracks
   * normally (quiet room -> code 31). */
  /* ret = VM3011_FastModeCntSet(dev, c.fast_mode_cnt); */
  /* if (ret != VM3011_OK) { return ret; } */

  ret = VM3011_WosRmsSet(dev, c.wos_rms);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  /* ret = VM3011_WosPgaMinThrSet(dev, c.pga_min_thr_db); */
  /* if (ret != VM3011_OK) { return ret; } */

  /* ret = VM3011_WosPgaMaxThrSet(dev, c.pga_max_thr_db); */
  /* if (ret != VM3011_OK) { return ret; } */

  ret = VM3011_WosDoutThreshSet(dev, c.wos_dout_thresh);
  if (ret != VM3011_OK)
  {
    return ret;
  }

  return VM3011_OK;
}
