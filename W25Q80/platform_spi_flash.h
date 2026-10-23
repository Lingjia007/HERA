#ifndef PLATFORM_SPI_FLASH_H
#define PLATFORM_SPI_FLASH_H

#include <stdint.h>
#include <stddef.h>

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef enum
{
    SPI_FLASH_TYPE_W25Q80 = 0,
    SPI_FLASH_TYPE_W25Q128,
    SPI_FLASH_TYPE_W25Q64,
    SPI_FLASH_TYPE_AT25SF641,
    SPI_FLASH_TYPE_UNKNOWN
} platform_spi_flash_type_t;

typedef enum
{
    SPI_FLASH_STATUS_OK = 0,
    SPI_FLASH_STATUS_ERROR,
    SPI_FLASH_STATUS_BUSY,
    SPI_FLASH_STATUS_TIMEOUT,
    SPI_FLASH_STATUS_INVALID_PARAM
} platform_spi_flash_status_t;

typedef struct
{
    int16_t (*init)(void *ctx);
    int16_t (*deinit)(void *ctx);
    int16_t (*read)(void *ctx, uint32_t addr, uint8_t *buffer, uint32_t size);
    int16_t (*write)(void *ctx, uint32_t addr, const uint8_t *buffer, uint32_t size);
    int16_t (*erase_sector)(void *ctx, uint32_t sector);
    int16_t (*erase_block)(void *ctx, uint32_t block);
    int16_t (*erase_chip)(void *ctx);
    int16_t (*sync)(void *ctx);
    uint32_t (*get_sector_size)(void *ctx);
    uint32_t (*get_block_size)(void *ctx);
    uint32_t (*get_total_size)(void *ctx);
    uint16_t (*get_page_size)(void *ctx);
    uint16_t (*read_id)(void *ctx);
    int16_t (*wait_busy)(void *ctx);
} platform_spi_flash_ops_t;

typedef struct
{
    const platform_spi_flash_ops_t *ops;
    const char *name;
    platform_spi_flash_type_t type;
    uint32_t total_size;
    uint32_t sector_size;
    uint32_t block_size;
    uint16_t page_size;
    void *user_data;
} platform_spi_flash_base_t;

#define SPI_FLASH_INIT(flash) \
    ((flash) && (flash)->ops && (flash)->ops->init ? (flash)->ops->init((flash)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_DEINIT(flash) \
    ((flash) && (flash)->ops && (flash)->ops->deinit ? (flash)->ops->deinit((flash)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_READ(flash, addr, buffer, size) \
    ((flash) && (flash)->ops && (flash)->ops->read ? (flash)->ops->read((flash), (addr), (buffer), (size)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_WRITE(flash, addr, buffer, size) \
    ((flash) && (flash)->ops && (flash)->ops->write ? (flash)->ops->write((flash), (addr), (buffer), (size)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_ERASE_SECTOR(flash, sector) \
    ((flash) && (flash)->ops && (flash)->ops->erase_sector ? (flash)->ops->erase_sector((flash), (sector)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_ERASE_BLOCK(flash, block) \
    ((flash) && (flash)->ops && (flash)->ops->erase_block ? (flash)->ops->erase_block((flash), (block)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_ERASE_CHIP(flash) \
    ((flash) && (flash)->ops && (flash)->ops->erase_chip ? (flash)->ops->erase_chip((flash)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_SYNC(flash) \
    ((flash) && (flash)->ops && (flash)->ops->sync ? (flash)->ops->sync((flash)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_GET_SECTOR_SIZE(flash) \
    ((flash) && (flash)->ops && (flash)->ops->get_sector_size ? (flash)->ops->get_sector_size((flash)) : 0)

#define SPI_FLASH_GET_BLOCK_SIZE(flash) \
    ((flash) && (flash)->ops && (flash)->ops->get_block_size ? (flash)->ops->get_block_size((flash)) : 0)

#define SPI_FLASH_GET_TOTAL_SIZE(flash) \
    ((flash) && (flash)->ops && (flash)->ops->get_total_size ? (flash)->ops->get_total_size((flash)) : 0)

#define SPI_FLASH_GET_PAGE_SIZE(flash) \
    ((flash) && (flash)->ops && (flash)->ops->get_page_size ? (flash)->ops->get_page_size((flash)) : 0)

#define SPI_FLASH_READ_ID(flash) \
    ((flash) && (flash)->ops && (flash)->ops->read_id ? (flash)->ops->read_id((flash)) : 0)

#define SPI_FLASH_WAIT_BUSY(flash) \
    ((flash) && (flash)->ops && (flash)->ops->wait_busy ? (flash)->ops->wait_busy((flash)) : (int16_t)SPI_FLASH_STATUS_ERROR)

#define SPI_FLASH_INIT_BASE(flash_ptr, ops_ptr, flash_name, flash_type) \
    do                                                                  \
    {                                                                   \
        (flash_ptr)->ops = (ops_ptr);                                   \
        (flash_ptr)->name = (flash_name);                               \
        (flash_ptr)->type = (flash_type);                               \
        (flash_ptr)->total_size = 0;                                    \
        (flash_ptr)->sector_size = 4096;                                \
        (flash_ptr)->block_size = 65536;                                \
        (flash_ptr)->page_size = 256;                                   \
        (flash_ptr)->user_data = NULL;                                  \
    } while (0)

#endif
