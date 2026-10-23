/**
 ******************************************************************************
 * @file w25_disk.h
 * @brief W25Q80 统一 512B 块设备层：FatFS(user_diskio) 与 USB MSC
 *        (usbd_storage_if) 共用，保证两侧看到同一份数据（单 cache）。
 *
 * W25Q80 擦除粒度 4KB、页编程 256B；本层用 4KB write-back cache 吸收
 * 512B 粒度的读写（RMW），顺序写同一 4KB 块只擦一次。
 *
 * 调用上下文：主循环（FatFS）与 USB 中断（MSC）。互斥策略：USB 已挂载
 * (configured) 期间固件端禁止写操作（业务层保证），本层自身不加锁。
 * USB 挂载期间读操作走同一 cache，数据一致。
 ******************************************************************************
 */
#ifndef W25_DISK_H
#define W25_DISK_H

#include <stdint.h>
#include "stm32wbxx_hal.h"
#include "platform_w25q80_stm32_impl.h"

#define W25DISK_SECTOR_SIZE 512U
#define W25DISK_SECTOR_COUNT (W25Q80_TOTAL_SIZE / W25DISK_SECTOR_SIZE) /* 2048 */

/* 只记录 SPI 参数（不访问硬件），可在外设 Init 前调用。
 * W25Disk 内部持有 w25q80_stm32_t 实例，首次 IO 时 lazy init。 */
void W25Disk_Bind(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);

/* 驱动是否就绪（ID 读取成功） */
uint8_t W25Disk_Ready(void);

/* 驱动句柄（诊断打印 ID/JEDEC 用） */
w25q80_stm32_t *W25Disk_Flash(void);

/* 读 nblocks 个 512B 扇区到 buf。返回 0 成功。 */
int32_t W25Disk_Read(uint8_t *buf, uint32_t lba, uint32_t nblocks);

/* 写 nblocks 个 512B 扇区（经 4KB cache，命中同块只改不落盘）。返回 0 成功。 */
int32_t W25Disk_Write(const uint8_t *buf, uint32_t lba, uint32_t nblocks);

/* 脏 cache 落盘（擦除 4KB + 页编程）。返回 0 成功。 */
int32_t W25Disk_Flush(void);

#endif /* W25_DISK_H */
