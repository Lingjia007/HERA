#ifndef LFS_W25Q80_ADAPTER_H
#define LFS_W25Q80_ADAPTER_H

#include "lfs.h"
#include "platform_spi_flash.h"

/* LittleFS 几何参数：与 W25Q80 特性对齐
 * - erase  4KB（一个扇区 = lfs 一个 block）
 * - prog   256B（页编程粒度）
 * - 全片 1MB = 256 个 block；如需预留区域改 block_count 即可 */
#define LFS_W25Q80_READ_SIZE 256
#define LFS_W25Q80_PROG_SIZE 256
#define LFS_W25Q80_BLOCK_SIZE 4096
#define LFS_W25Q80_LOOKAHEAD_SIZE 32 /* 32*8 = 256 blocks */

/* 全部缓冲静态分配，不依赖 malloc（配合 LFS_NO_MALLOC） */
typedef struct
{
    lfs_t lfs;
    struct lfs_config cfg;
    platform_spi_flash_base_t *flash;
    uint8_t read_buf[LFS_W25Q80_READ_SIZE];
    uint8_t prog_buf[LFS_W25Q80_PROG_SIZE];
    uint8_t lookahead_buf[LFS_W25Q80_LOOKAHEAD_SIZE];
} lfs_w25q80_t;

/* 挂载文件系统；首次使用/镜像无效时自动格式化并重挂
 * 返回 LFS_ERR_OK(0) 成功，其余为 littlefs 负数错误码 */
int lfs_w25q80_mount_or_format(lfs_w25q80_t *fs, platform_spi_flash_base_t *flash);

int lfs_w25q80_unmount(lfs_w25q80_t *fs);

#endif
