/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : usbd_storage_if.c
 * @version        : v3.0_Cube
 * @brief          : Memory management layer.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_storage_if.h"

/* USER CODE BEGIN INCLUDE */
#include "w25_disk.h"
#include "sd_diskio.h" /* SDPath / g_sd_mounted / g_sd_card */
/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
 * @brief Usb device.
 * @{
 */

/** @defgroup USBD_STORAGE
 * @brief Usb mass storage device module
 * @{
 */

/** @defgroup USBD_STORAGE_Private_TypesDefinitions
 * @brief Private types.
 * @{
 */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
 * @}
 */

/** @defgroup USBD_STORAGE_Private_Defines
 * @brief Private defines.
 * @{
 */

#define STORAGE_LUN_NBR 2
#define STORAGE_BLK_NBR 0x10000
#define STORAGE_BLK_SIZ 0x200

/* USER CODE BEGIN PRIVATE_DEFINES */
#include <stdio.h>
/* 双 LUN 真实数量（0 = W25Q80 flash，1 = SD 卡）。
 * 注意：生成区的 STORAGE_LUN_NBR 会被 CubeMX 重新生成为 1，但中间件
 * 只通过 GetMaxLun() 回调获取 LUN 数、从不读那个宏——LUN 数以这里
 * 的 HERA_MSC_LUN_NBR 为准，重新生成后无需任何手动补改。 */
#define HERA_MSC_LUN_NBR 2
/* LUN1 (SD) 诊断：首访问/失败打印 + 计数 + 周期进度（含最大单次耗时），
 * 用于定位盘符慢的根因：区分"操作多而慢"（吞吐瓶颈）与"操作少但有间隔"
 * （Windows 侧重试/等待）。 */
static uint32_t s_lun1_ops;    /* LUN1 读+写总次数                      */
static uint8_t s_lun1_first;   /* 首次访问是否已打印                    */
static uint32_t s_lun1_fails;  /* 读+写失败次数                         */
static uint32_t s_lun1_last_t; /* 上次操作完成时刻（测单次耗时）        */
static uint32_t s_lun1_dtmax;  /* 相邻两次操作的最大间隔（周期重置）    */
static void lun1_trace(int is_write, int r, uint32_t blk_addr, uint16_t blk_len)
{
  uint32_t now = HAL_GetTick();
  uint32_t dt = now - s_lun1_last_t;

  if (dt > s_lun1_dtmax)
  {
    s_lun1_dtmax = now - s_lun1_last_t;
  }
  s_lun1_last_t = now;
  s_lun1_ops++;
  if (s_lun1_first == 0U)
  {
    s_lun1_first = 1U;
    printf("[MSC] lun1 first read t=%lums a=%lu n=%u\r\n",
           (unsigned long)now, (unsigned long)blk_addr, blk_len);
  }
  if (r != SD_OK)
  {
    s_lun1_fails++;
    printf("[MSC] lun1 %s FAIL t=%lums a=%lu n=%u r=%d ops=%lu fails=%lu\r\n",
           is_write ? "WRITE" : "READ", (unsigned long)now,
           (unsigned long)blk_addr, blk_len, r,
           (unsigned long)s_lun1_ops, (unsigned long)s_lun1_fails);
  }
  if ((s_lun1_ops & 255U) == 0U) /* 每 256 次操作输出一条进度 */
  {
    printf("[MSC] lun1 ops=%lu t=%lums dtmax=%lums\r\n",
           (unsigned long)s_lun1_ops, (unsigned long)now,
           (unsigned long)s_lun1_dtmax);
    s_lun1_dtmax = 0U;
  }
}
/* USER CODE END PRIVATE_DEFINES */

/**
 * @}
 */

/** @defgroup USBD_STORAGE_Private_Macros
 * @brief Private macros.
 * @{
 */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
 * @}
 */

/** @defgroup USBD_STORAGE_Private_Variables
 * @brief Private variables.
 * @{
 */

/* USER CODE BEGIN INQUIRY_DATA_FS */
/** USB Mass storage Standard Inquiry Data. */
const int8_t STORAGE_Inquirydata_FS[] = {
    /* 36 * 2 */

    /* LUN 0: W25Q80 flash */
    0x00,
    0x80,
    0x02,
    0x02,
    (STANDARD_INQUIRY_DATA_LEN - 5),
    0x00,
    0x00,
    0x00,
    'S', 'T', 'M', ' ', ' ', ' ', ' ', ' ', /* Manufacturer : 8 bytes */
    'H', 'E', 'R', 'A', ' ', 'F', 'L', 'A', /* Product      : 16 Bytes */
    'S', 'H', ' ', ' ', ' ', ' ', ' ', ' ',
    '0', '.', '0', '1', /* Version      : 4 Bytes */

    /* LUN 1: SD card */
    0x00,
    0x80,
    0x02,
    0x02,
    (STANDARD_INQUIRY_DATA_LEN - 5),
    0x00,
    0x00,
    0x00,
    'S', 'T', 'M', ' ', ' ', ' ', ' ', ' ', /* Manufacturer : 8 bytes */
    'H', 'E', 'R', 'A', ' ', 'S', 'D', ' ', /* Product      : 16 Bytes */
    'C', 'A', 'R', 'D', ' ', ' ', ' ', ' ',
    '0', '.', '0', '1' /* Version      : 4 Bytes */
};
/* USER CODE END INQUIRY_DATA_FS */

/* USER CODE BEGIN PRIVATE_VARIABLES */

/* USER CODE END PRIVATE_VARIABLES */

/**
 * @}
 */

/** @defgroup USBD_STORAGE_Exported_Variables
 * @brief Public variables.
 * @{
 */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
 * @}
 */

/** @defgroup USBD_STORAGE_Private_FunctionPrototypes
 * @brief Private functions declaration.
 * @{
 */

static int8_t STORAGE_Init_FS(uint8_t lun);
static int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size);
static int8_t STORAGE_IsReady_FS(uint8_t lun);
static int8_t STORAGE_IsWriteProtected_FS(uint8_t lun);
static int8_t STORAGE_Read_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_Write_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_GetMaxLun_FS(void);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
 * @}
 */

USBD_StorageTypeDef USBD_Storage_Interface_fops_FS =
    {
        STORAGE_Init_FS,
        STORAGE_GetCapacity_FS,
        STORAGE_IsReady_FS,
        STORAGE_IsWriteProtected_FS,
        STORAGE_Read_FS,
        STORAGE_Write_FS,
        STORAGE_GetMaxLun_FS,
        (int8_t *)STORAGE_Inquirydata_FS};

/* Private functions ---------------------------------------------------------*/
/**
 * @brief  Initializes over USB FS IP
 * @param  lun:
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_Init_FS(uint8_t lun)
{
  /* USER CODE BEGIN 2 */
  if (lun == 1U)
  {
    /* SD 卡由 SD_AutoMount（main.c）就绪，USB 侧无需初始化；
     * 就绪性由 IsReady 实时探测 */
    return (USBD_OK);
  }
  return (W25Disk_Ready() != 0U) ? (USBD_OK) : (USBD_FAIL);
  /* USER CODE END 2 */
}

/**
 * @brief  .
 * @param  lun: .
 * @param  block_num: .
 * @param  block_size: .
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
  /* USER CODE BEGIN 3 */
  if (lun == 1U)
  {
    if (g_sd_card.info.initialized == 0U)
    {
      return (USBD_FAIL);
    }
    *block_num = g_sd_card.info.block_count; /* CSD 计算的总块数 */
    *block_size = 512U;
    return (USBD_OK);
  }
  *block_num = STORAGE_BLK_NBR;
  *block_size = STORAGE_BLK_SIZ;
  return (USBD_OK);
  /* USER CODE END 3 */
}

/**
 * @brief  .
 * @param  lun: .
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_IsReady_FS(uint8_t lun)
{
  /* USER CODE BEGIN 4 */
  if (lun == 1U)
  {
    /* 只查缓存标志，绝不做 SPI 探测：本回调在 USB ISR 里、每条 SCSI
     * 命令前都会被调用，CMD58 探测会与主循环 500ms 在位轮询撞锁
     * (SD_BUSY -> SCSI 失败)，Windows 对 not-ready LUN 按数秒~数十秒
     * 的节奏重试，导致盘符迟迟不出现。拔卡期间的读操作自然失败，
     * 会话结束后主循环轮询再作清理。 */
    return (g_sd_card.info.initialized != 0U) ? (USBD_OK) : (USBD_FAIL);
  }
  return (W25Disk_Ready() != 0U) ? (USBD_OK) : (USBD_FAIL);
  /* USER CODE END 4 */
}

/**
 * @brief  .
 * @param  lun: .
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_IsWriteProtected_FS(uint8_t lun)
{
  /* USER CODE BEGIN 5 */
  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
 * @brief  .
 * @param  lun: .
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_Read_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  /* USER CODE BEGIN 6 */
  if (lun == 1U)
  {
    int r = (blk_len == 1U) ? SD_Read_Block_Card(&g_sd_card, blk_addr, buf)
                            : SD_Read_Multi_Block_Card(&g_sd_card, blk_addr, buf,
                                                       blk_len);
    lun1_trace(0, r, blk_addr, blk_len);
    return (r == SD_OK) ? (USBD_OK) : (USBD_FAIL);
  }
  return (W25Disk_Read(buf, blk_addr, blk_len) == 0) ? (USBD_OK) : (USBD_FAIL);
  /* USER CODE END 6 */
}

/**
 * @brief  .
 * @param  lun: .
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_Write_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  /* USER CODE BEGIN 7 */
  if (lun == 1U)
  {
    int r = (blk_len == 1U) ? SD_Write_Block_Card(&g_sd_card, blk_addr, buf)
                            : SD_Write_Multi_Block_Card(&g_sd_card, blk_addr, buf,
                                                        blk_len);
    lun1_trace(1, r, blk_addr, blk_len);
    return (r == SD_OK) ? (USBD_OK) : (USBD_FAIL);
  }
  /* 写命令完成即落盘（SCSI 语义：数据已持久） */
  if (W25Disk_Write(buf, blk_addr, blk_len) != 0)
  {
    return (USBD_FAIL);
  }
  return (W25Disk_Flush() == 0) ? (USBD_OK) : (USBD_FAIL);
  /* USER CODE END 7 */
}

/**
 * @brief  .
 * @param  None
 * @retval .
 */
int8_t STORAGE_GetMaxLun_FS(void)
{
  /* USER CODE BEGIN 8 */
  return (HERA_MSC_LUN_NBR - 1);
  /* USER CODE END 8 */
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
 * @}
 */

/**
 * @}
 */
