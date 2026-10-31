/**
 ******************************************************************************
 * @file    sd_diskio.c
 * @brief   FatFs R0.12c (CubeMX ff_gen_drv) 磁盘驱动：SD SPI 卡 (SD_reader)
 *
 * 从 STM32_HAL-SPI_SD-FatFs 仓库移植：原仓库的 diskio.c 面向 FatFs R0.15
 * (FF_ 前缀 API)，本工程链接的是 CubeMX R0.12c 中间件，故按 ff_gen_drv 的
 * Diskio_drvTypeDef 重新对接，转调 SD_reader 的 *_Card API。协议核心
 * (SD_reader.c) 与 HAL 适配层 (sd_stm32_hal.c) 未做任何修改。
 ******************************************************************************
 */

#include <stdint.h>
#include "sd_diskio.h"

char SDPath[4] = {0};   /* 由 FATFS_LinkDriver 填充，如 "1:" */
FATFS SDFatFs;          /* SD 卷的文件系统对象 */

static SD_Card *s_card = &g_sd_card;

static DSTATUS sd_status(BYTE pdrv)
{
    (void)pdrv; /* 单盘：lun 与物理盘号无关 */
    return s_card->info.initialized ? 0 : STA_NOINIT;
}

static DSTATUS sd_initialize(BYTE pdrv)
{
    (void)pdrv;
    if (s_card->info.initialized)
    {
        return 0;
    }
    return (SD_Init_Card(s_card) > 0) ? 0 : STA_NOINIT;
}

static DRESULT sd_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    int r;
    (void)pdrv;

    if (!s_card->info.initialized)
    {
        return RES_NOTRDY;
    }
    if (buff == NULL || count == 0U)
    {
        return RES_PARERR;
    }
    r = (count == 1U)
        ? SD_Read_Block_Card(s_card, (uint32_t)sector, buff)
        : SD_Read_Multi_Block_Card(s_card, (uint32_t)sector, buff, (uint32_t)count);
    return (r == SD_OK) ? RES_OK : RES_ERROR;
}

#if _USE_WRITE == 1
static DRESULT sd_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    int r;
    (void)pdrv;

    if (!s_card->info.initialized)
    {
        return RES_NOTRDY;
    }
    if (buff == NULL || count == 0U)
    {
        return RES_PARERR;
    }
    r = (count == 1U)
        ? SD_Write_Block_Card(s_card, (uint32_t)sector, buff)
        : SD_Write_Multi_Block_Card(s_card, (uint32_t)sector, buff, (uint32_t)count);
    return (r == SD_OK) ? RES_OK : RES_ERROR;
}
#endif /* _USE_WRITE == 1 */

#if _USE_IOCTL == 1
static DRESULT sd_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;

    if (!s_card->info.initialized)
    {
        return RES_NOTRDY;
    }

    switch (cmd)
    {
    case CTRL_SYNC:      /* 写路径同步阻塞，无挂起缓存 */
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = s_card->info.block_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SD_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE: /* 擦除块大小（扇区计），未知返回 1 */
        *(DWORD *)buff = 1U;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef SD_Driver =
{
    sd_initialize,
    sd_status,
    sd_read,
#if  _USE_WRITE == 1
    sd_write,
#endif
#if  _USE_IOCTL == 1
    sd_ioctl,
#endif
};
