#ifndef PLATFORM_W25Q80_STM32_IMPL_H
#define PLATFORM_W25Q80_STM32_IMPL_H

#include "platform_spi_flash.h"
#include "stm32wbxx_hal.h"

/* W25Q80: 1MB, 4KB sector, 64KB block, 256B page */
#define W25Q80_TOTAL_SIZE (1 * 1024 * 1024)
#define W25Q80_SECTOR_SIZE 4096
#define W25Q80_BLOCK_SIZE 65536
#define W25Q80_PAGE_SIZE 256
#define W25Q80_SECTOR_COUNT (W25Q80_TOTAL_SIZE / W25Q80_SECTOR_SIZE)
#define W25Q80_BLOCK_COUNT (W25Q80_TOTAL_SIZE / W25Q80_BLOCK_SIZE)

typedef struct {
    platform_spi_flash_base_t base;
    SPI_HandleTypeDef* hspi;
    GPIO_TypeDef* cs_port;
    uint16_t cs_pin;
    uint16_t device_id;
} w25q80_stm32_t;

void platform_w25q80_stm32_register(w25q80_stm32_t *flash, SPI_HandleTypeDef *hspi,
                                    GPIO_TypeDef *cs_port, uint16_t cs_pin, const char *name);

/* 诊断用：读取 JEDEC ID（W25Q80 应为 EF 40 14） */
int16_t platform_w25q80_stm32_read_jedec_id(w25q80_stm32_t *flash, uint8_t id[3]);

#endif
