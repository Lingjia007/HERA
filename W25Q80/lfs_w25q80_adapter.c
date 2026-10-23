#include "lfs_w25q80_adapter.h"
#include <stdio.h>

static int lfs_w25q80_read(const struct lfs_config *c, lfs_block_t block,
                           lfs_off_t off, void *buffer, lfs_size_t size)
{
    lfs_w25q80_t *fs = (lfs_w25q80_t *)c->context;
    uint32_t addr = block * LFS_W25Q80_BLOCK_SIZE + off;

    if (fs == NULL || block >= (fs->cfg.block_count) || (off + size) > LFS_W25Q80_BLOCK_SIZE)
    {
        return LFS_ERR_INVAL;
    }

    if (SPI_FLASH_READ(fs->flash, addr, (uint8_t *)buffer, size) != SPI_FLASH_STATUS_OK)
    {
        printf("[LFS] read err addr=0x%06lX size=%lu\r\n",
               (unsigned long)addr, (unsigned long)size);
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int lfs_w25q80_prog(const struct lfs_config *c, lfs_block_t block,
                           lfs_off_t off, const void *buffer, lfs_size_t size)
{
    lfs_w25q80_t *fs = (lfs_w25q80_t *)c->context;
    uint32_t addr = block * LFS_W25Q80_BLOCK_SIZE + off;

    if (fs == NULL || block >= (fs->cfg.block_count) || (off + size) > LFS_W25Q80_BLOCK_SIZE)
    {
        return LFS_ERR_INVAL;
    }

    if (SPI_FLASH_WRITE(fs->flash, addr, (const uint8_t *)buffer, size) != SPI_FLASH_STATUS_OK)
    {
        printf("[LFS] prog err addr=0x%06lX size=%lu\r\n",
               (unsigned long)addr, (unsigned long)size);
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int lfs_w25q80_erase(const struct lfs_config *c, lfs_block_t block)
{
    lfs_w25q80_t *fs = (lfs_w25q80_t *)c->context;

    if (fs == NULL || block >= (fs->cfg.block_count))
    {
        return LFS_ERR_INVAL;
    }

    if (SPI_FLASH_ERASE_SECTOR(fs->flash, block) != SPI_FLASH_STATUS_OK)
    {
        printf("[LFS] erase err block=%lu\r\n", (unsigned long)block);
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int lfs_w25q80_sync(const struct lfs_config *c)
{
    lfs_w25q80_t *fs = (lfs_w25q80_t *)c->context;

    if (fs == NULL)
    {
        return LFS_ERR_INVAL;
    }

    if (SPI_FLASH_SYNC(fs->flash) != SPI_FLASH_STATUS_OK)
    {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

int lfs_w25q80_mount_or_format(lfs_w25q80_t *fs, platform_spi_flash_base_t *flash)
{
    if (fs == NULL || flash == NULL)
    {
        return LFS_ERR_INVAL;
    }

    fs->flash = flash;
    fs->cfg.context = fs;
    fs->cfg.read = lfs_w25q80_read;
    fs->cfg.prog = lfs_w25q80_prog;
    fs->cfg.erase = lfs_w25q80_erase;
    fs->cfg.sync = lfs_w25q80_sync;
    fs->cfg.read_size = LFS_W25Q80_READ_SIZE;
    fs->cfg.prog_size = LFS_W25Q80_PROG_SIZE;
    fs->cfg.block_size = LFS_W25Q80_BLOCK_SIZE;
    fs->cfg.block_count = SPI_FLASH_GET_TOTAL_SIZE(flash) / LFS_W25Q80_BLOCK_SIZE;
    fs->cfg.block_cycles = 500; /* 元数据块磨损均衡窗口 */
    fs->cfg.cache_size = LFS_W25Q80_PROG_SIZE;
    fs->cfg.lookahead_size = LFS_W25Q80_LOOKAHEAD_SIZE;
    fs->cfg.read_buffer = fs->read_buf;
    fs->cfg.prog_buffer = fs->prog_buf;
    fs->cfg.lookahead_buffer = fs->lookahead_buf;

    int err = lfs_mount(&fs->lfs, &fs->cfg);
    if (err != LFS_ERR_OK)
    {
        printf("[LFS] mount err=%d, formatting...\r\n", err);
        err = lfs_format(&fs->lfs, &fs->cfg);
        if (err == LFS_ERR_OK)
        {
            err = lfs_mount(&fs->lfs, &fs->cfg);
        }
    }
    return err;
}

int lfs_w25q80_unmount(lfs_w25q80_t *fs)
{
    if (fs == NULL)
    {
        return LFS_ERR_INVAL;
    }
    return lfs_unmount(&fs->lfs);
}
