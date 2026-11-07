/**
 ******************************************************************************
 * @file    sd_diskio.c
 * @brief   FatFs R0.15 (stm32-mw-fatfs ff_gen_drv) 磁盘驱动：SD SPI 卡 (SD_reader)
 *
 * 从 STM32_HAL-SPI_SD-FatFs 仓库移植：按 ff_gen_drv 的 Diskio_drvTypeDef
 * 对接，转调 SD_reader 的 *_Card API。协议核心 (SD_reader.c) 与 HAL 适配层
 * (sd_stm32_hal.c) 未做任何修改。条件编译改用 R0.15 的 FF_FS_READONLY
 * （R0.12c 的 _USE_WRITE/_USE_IOCTL 已删除；FF_FS_READONLY==0 时
 * disk_write/disk_ioctl 均必需）。
 ******************************************************************************
 */

#include <stdint.h>
#include <stddef.h> /* NULL（旧版 ffconf.h 经 HAL 头间接提供） */
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

static DRESULT sd_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
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

#if FF_FS_READONLY == 0
static DRESULT sd_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
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
#endif /* FF_FS_READONLY == 0 */

#if FF_FS_MINIMIZE < 1 || !FF_FS_READONLY
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
        *(LBA_t *)buff = s_card->info.block_count;
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
#endif /* FF_FS_MINIMIZE < 1 || !FF_FS_READONLY */

Diskio_drvTypeDef SD_Driver =
{
    sd_initialize,
    sd_status,
    sd_read,
#if FF_FS_READONLY == 0
    sd_write,
#endif
#if FF_FS_MINIMIZE < 1 || !FF_FS_READONLY
    sd_ioctl,
#endif
};
