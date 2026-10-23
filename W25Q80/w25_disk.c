/**
 ******************************************************************************
 * @file w25_disk.c
 * @brief W25Q80 512B 块设备层实现（详见 w25_disk.h）。
 *
 * cache 策略（write-back，单行 4KB）：
 *   - 写：改 cache 标脏；换行或 Flush 时「擦除 4KB + 16 页编程」落盘。
 *     顺序写（录音数据流）连续 8 个 512B 落在同一 cache 行，只擦 1 次。
 *   - 读：命中直接回；未命中且行脏先落盘，再从芯片读入。
 *   - FatFS 的 CTRL_SYNC（f_sync/f_close）与 MSC 写命令完成时都会 Flush，
 *     掉电最多丢 cache 内未落盘的 <4KB 数据。
 ******************************************************************************
 */
#include "w25_disk.h"
#include <string.h>

static w25q80_stm32_t s_flash;
static uint8_t s_bound;      /* Bind 已调用 */
static uint8_t s_inited;     /* SPI_FLASH_INIT 成功 */

/* ---- SPI 竞态防护 -----------------------------------------------------
 * W25Disk 的两个调用方：主循环（FatFS）与 USB_LP 中断（MSC）。HAL SPI
 * 非重入，主循环传输被 MSC 抢断会把 CS 时序切碎、数据错乱（表现：
 * PC 枚举后读到的 BPB 损坏 -> "无法识别"）。
 * 防护：主循环上下文进入 SPI 操作前禁 USB_LP 中断（USB 硬件对主机
 * NAK，事务挂起，使能后继续，协议层无损）；USB_LP 中断自身无需处理。
 * 音频/串口中断优先级更高且不碰 SPI，不受影响。 */
static uint8_t spi_guard_enter(void)
{
    if (__get_IPSR() == 0U) /* thread mode = 主循环 */
    {
        HAL_NVIC_DisableIRQ(USB_LP_IRQn);
        return 1U;
    }
    return 0U;
}

static void spi_guard_exit(uint8_t guard)
{
    if (guard != 0U)
    {
        HAL_NVIC_EnableIRQ(USB_LP_IRQn);
    }
}

static uint8_t s_cache[W25Q80_SECTOR_SIZE]; /* 4KB cache 行 */
static uint32_t s_cache_row;               /* cache 行号 = addr/4096 */
static uint8_t s_cache_valid;              /* cache 数据与芯片一致（非脏） */
static uint8_t s_cache_dirty;              /* cache 有未落盘修改 */

/* 首次 IO 时 lazy init（幂等：成功后短路）。
 * 失败退避：连续失败 3 次后不再重试——否则 PC 端 MSC 轮询
 * (STORAGE_IsReady) 会在 USB 中断里反复触发 SPI init，形成优先级 0
 * 的中断风暴，饿死 SysTick/USART1（表现：串口 CLI 无响应）。 */
#define W25DISK_INIT_MAX_FAIL 3U
static uint8_t s_init_fails;

static int ensure_init(void)
{
    if (s_inited)
    {
        return 0;
    }
    if (!s_bound || s_init_fails >= W25DISK_INIT_MAX_FAIL)
    {
        return -1;
    }
    if (SPI_FLASH_INIT(&s_flash.base) != SPI_FLASH_STATUS_OK)
    {
        s_init_fails++;
        return -2;
    }
    s_inited = 1;
    return 0;
}

void W25Disk_Bind(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin)
{
    platform_w25q80_stm32_register(&s_flash, hspi, cs_port, cs_pin, "w25q80");
    s_bound = 1;
    s_inited = 0;
    s_cache_valid = 0;
    s_cache_dirty = 0;
}

uint8_t W25Disk_Ready(void)
{
    uint8_t guard = spi_guard_enter();
    uint8_t ok = (ensure_init() == 0) ? 1U : 0U;
    spi_guard_exit(guard);
    return ok;
}

w25q80_stm32_t *W25Disk_Flash(void)
{
    return &s_flash;
}

/* 脏行落盘：擦 4KB + 页编程回写 */
static int32_t cache_flush_row(void)
{
    uint32_t base;

    if (!s_cache_dirty)
    {
        return 0;
    }
    base = s_cache_row * W25Q80_SECTOR_SIZE;
    if (SPI_FLASH_ERASE_SECTOR(&s_flash.base, s_cache_row) != SPI_FLASH_STATUS_OK)
    {
        return -1;
    }
    if (SPI_FLASH_WRITE(&s_flash.base, base, s_cache, sizeof(s_cache)) != SPI_FLASH_STATUS_OK)
    {
        return -2;
    }
    s_cache_dirty = 0;
    return 0;
}

int32_t W25Disk_Flush(void)
{
    int32_t r;
    uint8_t guard = spi_guard_enter();

    if (ensure_init() != 0)
    {
        spi_guard_exit(guard);
        return -1;
    }
    r = cache_flush_row();
    spi_guard_exit(guard);
    return r;
}

/* 载入包含 addr..addr+len 的行；返回 0 成功 */
static int32_t cache_load_row(uint32_t row)
{
    if (s_cache_valid && s_cache_row == row)
    {
        return 0;
    }
    /* 换行前先把脏数据落盘 */
    if (cache_flush_row() != 0)
    {
        return -1;
    }
    s_cache_row = row;
    if (SPI_FLASH_READ(&s_flash.base, row * W25Q80_SECTOR_SIZE,
                       s_cache, sizeof(s_cache)) != SPI_FLASH_STATUS_OK)
    {
        s_cache_valid = 0;
        return -2;
    }
    s_cache_valid = 1;
    return 0;
}

int32_t W25Disk_Read(uint8_t *buf, uint32_t lba, uint32_t nblocks)
{
    uint32_t done = 0;
    uint8_t guard;

    if (buf == NULL || lba + nblocks > W25DISK_SECTOR_COUNT)
    {
        return -1;
    }
    guard = spi_guard_enter();
    if (ensure_init() != 0)
    {
        spi_guard_exit(guard);
        return -2;
    }

    /* 读也经 cache：MSC（PC）与 FatFS 共享同一份数据，写后未落盘仍可读回 */
    while (done < nblocks)
    {
        uint32_t addr = (lba + done) * W25DISK_SECTOR_SIZE;
        uint32_t row = addr / W25Q80_SECTOR_SIZE;
        uint32_t off = addr % W25Q80_SECTOR_SIZE;
        uint32_t chunk = W25Q80_SECTOR_SIZE - off;
        if (chunk > (nblocks - done) * W25DISK_SECTOR_SIZE)
        {
            chunk = (nblocks - done) * W25DISK_SECTOR_SIZE;
        }

        if (cache_load_row(row) != 0)
        {
            spi_guard_exit(guard);
            return -3;
        }
        memcpy(buf + done * W25DISK_SECTOR_SIZE, s_cache + off, chunk);
        done += chunk / W25DISK_SECTOR_SIZE;
    }
    spi_guard_exit(guard);
    return 0;
}

int32_t W25Disk_Write(const uint8_t *buf, uint32_t lba, uint32_t nblocks)
{
    uint32_t done = 0;
    uint8_t guard;

    if (buf == NULL || lba + nblocks > W25DISK_SECTOR_COUNT)
    {
        return -1;
    }
    guard = spi_guard_enter();
    if (ensure_init() != 0)
    {
        spi_guard_exit(guard);
        return -2;
    }

    while (done < nblocks)
    {
        uint32_t addr = (lba + done) * W25DISK_SECTOR_SIZE;
        uint32_t row = addr / W25Q80_SECTOR_SIZE;
        uint32_t off = addr % W25Q80_SECTOR_SIZE;
        uint32_t chunk = W25Q80_SECTOR_SIZE - off;
        if (chunk > (nblocks - done) * W25DISK_SECTOR_SIZE)
        {
            chunk = (nblocks - done) * W25DISK_SECTOR_SIZE;
        }

        if (cache_load_row(row) != 0)
        {
            spi_guard_exit(guard);
            return -3;
        }
        memcpy(s_cache + off, buf + done * W25DISK_SECTOR_SIZE, chunk);
        s_cache_dirty = 1;
        done += chunk / W25DISK_SECTOR_SIZE;
    }
    spi_guard_exit(guard);
    return 0;
}
