#include "platform_w25q80_stm32_impl.h"

#define W25X_WriteEnable 0x06
#define W25X_WriteDisable 0x04
#define W25X_ReadStatusReg 0x05
#define W25X_WriteStatusReg 0x01
#define W25X_ReadData 0x03
#define W25X_FastReadData 0x0B
#define W25X_FastReadDual 0x3B
#define W25X_PageProgram 0x02
#define W25X_BlockErase 0xD8
#define W25X_SectorErase 0x20
#define W25X_ChipErase 0xC7
#define W25X_PowerDown 0xB9
#define W25X_ReleasePowerDown 0xAB
#define W25X_DeviceID 0xAB
#define W25X_ManufactDeviceID 0x90
#define W25X_JedecDeviceID 0x9F

#define W25Q80_IO_CHUNK_SIZE 4096U
#define W25Q80_BUSY_TIMEOUT 0x00FFFFFFU
#define W25Q80_CHIP_ERASE_TIMEOUT 0xFFFFFFFFU
#define W25Q80_SPI_TIMEOUT_MS 100U

static uint8_t spi_read_write_byte(w25q80_stm32_t* self, uint8_t dat)
{
    uint8_t rx_data;
    if (HAL_SPI_TransmitReceive(self->hspi, &dat, &rx_data, 1, W25Q80_SPI_TIMEOUT_MS) == HAL_OK) {
        return rx_data;
    }
    return 0xFF;
}

static int16_t send_cmd_addr(w25q80_stm32_t* self, uint8_t cmd, uint32_t addr)
{
    uint8_t buf[4];

    buf[0] = cmd;
    buf[1] = (uint8_t)(addr >> 16);
    buf[2] = (uint8_t)(addr >> 8);
    buf[3] = (uint8_t)addr;

    return (HAL_SPI_Transmit(self->hspi, buf, 4, W25Q80_SPI_TIMEOUT_MS) == HAL_OK)
               ? SPI_FLASH_STATUS_OK
               : SPI_FLASH_STATUS_ERROR;
}

static void cs_on(w25q80_stm32_t* self, uint8_t state)
{
    HAL_GPIO_WritePin(self->cs_port, self->cs_pin, state ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void write_enable(w25q80_stm32_t* self)
{
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_WriteEnable);
    cs_on(self, 0);
}

static int16_t wait_busy_impl(w25q80_stm32_t* self)
{
    uint8_t byte = 0;
    uint32_t timeout = W25Q80_BUSY_TIMEOUT;

    do {
        cs_on(self, 1);
        spi_read_write_byte(self, W25X_ReadStatusReg);
        byte = spi_read_write_byte(self, 0xFF);
        cs_on(self, 0);

        if ((byte & 0x01) == 0) {
            return SPI_FLASH_STATUS_OK;
        }
        if (timeout-- == 0) {
            return SPI_FLASH_STATUS_TIMEOUT;
        }
    } while (1);
}

static int16_t w25q80_init(void* ctx)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);

    cs_on(self, 0);

    self->device_id = 0;
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_ManufactDeviceID);
    spi_read_write_byte(self, 0x00);
    spi_read_write_byte(self, 0x00);
    spi_read_write_byte(self, 0x00);
    self->device_id |= spi_read_write_byte(self, 0xFF) << 8;
    self->device_id |= spi_read_write_byte(self, 0xFF);
    cs_on(self, 0);

    if (self->device_id == 0xFFFF || self->device_id == 0x0000) {
        return SPI_FLASH_STATUS_ERROR;
    }

    self->base.total_size = W25Q80_TOTAL_SIZE;
    self->base.sector_size = W25Q80_SECTOR_SIZE;
    self->base.block_size = W25Q80_BLOCK_SIZE;
    self->base.page_size = W25Q80_PAGE_SIZE;

    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q80_deinit(void* ctx)
{
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q80_read(void* ctx, uint32_t addr, uint8_t* buffer, uint32_t size)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);
    uint32_t remaining = size;
    uint32_t offset = addr;

    if (buffer == NULL || addr + size > self->base.total_size) {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }

    while (remaining > 0) {
        uint32_t chunk = (remaining > W25Q80_IO_CHUNK_SIZE) ? W25Q80_IO_CHUNK_SIZE : remaining;

        cs_on(self, 1);
        if (send_cmd_addr(self, W25X_ReadData, offset) != SPI_FLASH_STATUS_OK) {
            cs_on(self, 0);
            return SPI_FLASH_STATUS_ERROR;
        }
        if (HAL_SPI_Receive(self->hspi, buffer, (uint16_t)chunk, W25Q80_SPI_TIMEOUT_MS) != HAL_OK) {
            cs_on(self, 0);
            return SPI_FLASH_STATUS_ERROR;
        }
        cs_on(self, 0);

        buffer += chunk;
        offset += chunk;
        remaining -= chunk;
    }

    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q80_write(void* ctx, uint32_t addr, const uint8_t* buffer, uint32_t size)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);
    uint32_t remaining = size;
    uint32_t offset = addr;

    if (buffer == NULL || addr + size > self->base.total_size) {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }

    /* 页编程不能跨页，按页边界拆分 */
    while (remaining > 0) {
        uint32_t page_left = W25Q80_PAGE_SIZE - (offset % W25Q80_PAGE_SIZE);
        uint32_t chunk = (remaining < page_left) ? remaining : page_left;

        write_enable(self);
        if (wait_busy_impl(self) != SPI_FLASH_STATUS_OK) {
            return SPI_FLASH_STATUS_TIMEOUT;
        }

        cs_on(self, 1);
        if (send_cmd_addr(self, W25X_PageProgram, offset) != SPI_FLASH_STATUS_OK) {
            cs_on(self, 0);
            return SPI_FLASH_STATUS_ERROR;
        }
        if (HAL_SPI_Transmit(self->hspi, buffer, (uint16_t)chunk, W25Q80_SPI_TIMEOUT_MS) != HAL_OK) {
            cs_on(self, 0);
            return SPI_FLASH_STATUS_ERROR;
        }
        cs_on(self, 0);

        if (wait_busy_impl(self) != SPI_FLASH_STATUS_OK) {
            return SPI_FLASH_STATUS_TIMEOUT;
        }

        buffer += chunk;
        offset += chunk;
        remaining -= chunk;
    }

    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q80_erase_sector(void* ctx, uint32_t sector)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);
    uint32_t addr;

    if (sector >= W25Q80_SECTOR_COUNT) {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }

    addr = sector * W25Q80_SECTOR_SIZE;

    write_enable(self);
    if (wait_busy_impl(self) != SPI_FLASH_STATUS_OK) {
        return SPI_FLASH_STATUS_TIMEOUT;
    }

    cs_on(self, 1);
    send_cmd_addr(self, W25X_SectorErase, addr);
    cs_on(self, 0);

    return wait_busy_impl(self);
}

static int16_t w25q80_erase_block(void* ctx, uint32_t block)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);
    uint32_t addr;

    if (block >= W25Q80_BLOCK_COUNT) {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }

    addr = block * W25Q80_BLOCK_SIZE;

    write_enable(self);
    if (wait_busy_impl(self) != SPI_FLASH_STATUS_OK) {
        return SPI_FLASH_STATUS_TIMEOUT;
    }

    cs_on(self, 1);
    send_cmd_addr(self, W25X_BlockErase, addr);
    cs_on(self, 0);

    return wait_busy_impl(self);
}

static int16_t w25q80_erase_chip(void* ctx)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);
    uint32_t timeout = W25Q80_CHIP_ERASE_TIMEOUT;
    uint8_t byte = 0;

    write_enable(self);
    if (wait_busy_impl(self) != SPI_FLASH_STATUS_OK) {
        return SPI_FLASH_STATUS_TIMEOUT;
    }

    cs_on(self, 1);
    spi_read_write_byte(self, W25X_ChipErase);
    cs_on(self, 0);

    do {
        cs_on(self, 1);
        spi_read_write_byte(self, W25X_ReadStatusReg);
        byte = spi_read_write_byte(self, 0xFF);
        cs_on(self, 0);

        if ((byte & 0x01) == 0) {
            return SPI_FLASH_STATUS_OK;
        }
        if (timeout-- == 0) {
            return SPI_FLASH_STATUS_TIMEOUT;
        }
    } while (1);
}

static int16_t w25q80_sync(void* ctx)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);
    return wait_busy_impl(self);
}

static uint32_t w25q80_get_sector_size(void* ctx)
{
    return W25Q80_SECTOR_SIZE;
}

static uint32_t w25q80_get_block_size(void* ctx)
{
    return W25Q80_BLOCK_SIZE;
}

static uint32_t w25q80_get_total_size(void* ctx)
{
    return W25Q80_TOTAL_SIZE;
}

static uint16_t w25q80_get_page_size(void* ctx)
{
    return W25Q80_PAGE_SIZE;
}

static uint16_t w25q80_read_id(void* ctx)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);
    return self->device_id;
}

static int16_t w25q80_wait_busy(void* ctx)
{
    w25q80_stm32_t* self = container_of(ctx, w25q80_stm32_t, base);
    return wait_busy_impl(self);
}

static const platform_spi_flash_ops_t w25q80_ops = {
    .init = w25q80_init,
    .deinit = w25q80_deinit,
    .read = w25q80_read,
    .write = w25q80_write,
    .erase_sector = w25q80_erase_sector,
    .erase_block = w25q80_erase_block,
    .erase_chip = w25q80_erase_chip,
    .sync = w25q80_sync,
    .get_sector_size = w25q80_get_sector_size,
    .get_block_size = w25q80_get_block_size,
    .get_total_size = w25q80_get_total_size,
    .get_page_size = w25q80_get_page_size,
    .read_id = w25q80_read_id,
    .wait_busy = w25q80_wait_busy,
};

void platform_w25q80_stm32_register(w25q80_stm32_t *flash, SPI_HandleTypeDef *hspi,
                                    GPIO_TypeDef *cs_port, uint16_t cs_pin, const char *name)
{
    if (flash == NULL || hspi == NULL || cs_port == NULL)
    {
        return;
    }

    flash->hspi = hspi;
    flash->cs_port = cs_port;
    flash->cs_pin = cs_pin;
    flash->device_id = 0;

    SPI_FLASH_INIT_BASE(&flash->base, &w25q80_ops, name, SPI_FLASH_TYPE_W25Q80);
    flash->base.total_size = W25Q80_TOTAL_SIZE;
    flash->base.sector_size = W25Q80_SECTOR_SIZE;
    flash->base.block_size = W25Q80_BLOCK_SIZE;
    flash->base.page_size = W25Q80_PAGE_SIZE;
}

int16_t platform_w25q80_stm32_read_jedec_id(w25q80_stm32_t *flash, uint8_t id[3])
{
    uint8_t cmd = W25X_JedecDeviceID;

    if (flash == NULL || id == NULL)
    {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }

    cs_on(flash, 1);
    if (HAL_SPI_Transmit(flash->hspi, &cmd, 1, W25Q80_SPI_TIMEOUT_MS) != HAL_OK)
    {
        cs_on(flash, 0);
        return SPI_FLASH_STATUS_ERROR;
    }
    if (HAL_SPI_Receive(flash->hspi, id, 3, W25Q80_SPI_TIMEOUT_MS) != HAL_OK)
    {
        cs_on(flash, 0);
        return SPI_FLASH_STATUS_ERROR;
    }
    cs_on(flash, 0);

    return SPI_FLASH_STATUS_OK;
}
