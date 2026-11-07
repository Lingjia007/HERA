/*---------------------------------------------------------------------------/
/  FatFs Configuration (stm32-mw-fatfs, R0.15)
/
/  从 CubeMX 生成的 FATFS/Target/ffconf.h（R0.12c，_FS_ 与 _USE_ 前缀）迁移：
/  按原工程取值逐项改写为 R0.15 的 FF_ 前缀选项。
/---------------------------------------------------------------------------*/

#ifndef FFCONF_DEF
#define FFCONF_DEF	80386	/* Revision ID（ff.h 校验 FF_DEFINED == FFCONF_DEF；文件尾 #endif 与本 guard 配对） */

/*---------------------------------------------------------------------------/
/  Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0
/* 0:Read/Write or 1:Read-only */

#define FF_FS_MINIMIZE	0
/* 0:Basic functions are fully enabled. */

#define FF_USE_FIND		0
/* 0:Disable f_findfirst()/f_findnext() */

#define FF_USE_MKFS		1
/* 1:Enable f_mkfs()（W25Q80 首次上电自动格式化 FAT12 需要） */

#define FF_USE_FASTSEEK	1
/* 1:Enable fast seek feature */

#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	1
/* 1:Enable f_getlabel()/f_setlabel()（U 盘卷标 HERA_FLASH） */

#define FF_USE_FORWARD	0

#define FF_USE_STRFUNC	2
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	3
/* 2:f_gets()/f_putc()/f_puts()/f_printf() with LF-CRLF conversion */

/*---------------------------------------------------------------------------/
/  Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	850
/* Latin 1（沿用原工程 CP850） */

#define FF_USE_LFN		1
#define FF_MAX_LFN		255
/* 1:Enable LFN with static working buffer on the BSS
   （目录名 AUDIO_REC 超 8.3 短名，必须 LFN 才能创建/访问） */

#define FF_LFN_UNICODE	0
/* 0:ANSI/OEM in current CP (TCHAR = char) */

#define FF_LFN_BUF		255
#define FF_SFN_BUF		12

#define FF_FS_RPATH		0
/* 0:Disable relative path */

#define FF_PATH_DEPTH	10

/*---------------------------------------------------------------------------/
/  Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define FF_VOLUMES		2
/* 2 卷：W25Q80 flash "0:" + MicroSD "1:" */

#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD1","SD2","USB1","USB2","USB3"

#define FF_MULTI_PARTITION	0
/* 0:Single partition */

#define FF_MIN_SS		512
#define FF_MAX_SS		512
/* 512B 扇区（SD SPI 与 W25Q80 块层均为 512B） */

#define FF_LBA64		0
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

/*---------------------------------------------------------------------------/
/  System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		0
/* 0:Normal（FIL 自带 512B 扇区缓冲，录音流写依赖） */

#define FF_FS_EXFAT		0
/* 0:Disable exFAT */

#define FF_FS_NORTC		0
#define FF_NORTC_MON	6
#define FF_NORTC_MDAY	4
#define FF_NORTC_YEAR	2015
/* 0:启用时间戳；get_fattime() 由 stm32-mw-fatfs diskio.c 提供 __weak
   实现（返回 0，与原 app_fatfs.c 行为一致） */

#define FF_FS_CRTIME	0
#define FF_FS_NOFSINFO	0

#define FF_FS_LOCK		2
/* 2:同时最多 2 个文件/目录打开（沿用原工程） */

#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000
/* 0:Disable re-entrancy（裸机单上下文使用） */

/*--- End of configuration options ---*/
#endif /* FFCONF_DEF */
