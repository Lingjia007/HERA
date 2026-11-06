#ifndef SD_DISKIO_H
#define SD_DISKIO_H

#include <stdint.h>
#include "ff_gen_drv.h"
#include "SD_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SD 卡逻辑卷路径（FATFS_LinkDriver 分配，形如 "1:"） */
extern char SDPath[4];
/* SD 卡 FATFS 卷对象 */
extern FATFS SDFatFs;
/* 1 = SD 卷已挂载可用（main.c 插拔/挂载逻辑维护） */
extern uint8_t g_sd_mounted;

/* ff_gen_drv 磁盘驱动：SD SPI（SD_reader） -> FatFs R0.12c */
extern Diskio_drvTypeDef SD_Driver;

#ifdef __cplusplus
}
#endif

#endif /* SD_DISKIO_H */
