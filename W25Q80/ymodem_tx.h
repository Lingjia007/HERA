#ifndef YMODEM_TX_H
#define YMODEM_TX_H

#include "lfs.h"

/* YMODEM(CRC, 1K) 发送器：把 littlefs 里的文件经 USART1 发给上位机。
 * 用法：上位机（Tera Term/Xshell/SecureCRT）选"YMODEM 接收"，
 *       然后设备端执行 `ym <文件名>`。
 * 返回 0 成功；负数为错误码。 */
int YmodemTx_Send(lfs_t *lfs, const char *path);

#endif
