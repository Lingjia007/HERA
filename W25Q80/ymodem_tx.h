#ifndef YMODEM_TX_H
#define YMODEM_TX_H

/* YMODEM(CRC, 1K) 发送器：把 FAT(W25Q80) 里的文件经 USART1 发给上位机。
 * 用法：上位机（Tera Term/Xshell/SecureCRT）选"YMODEM 接收"，
 *       然后设备端执行 `ym <文件名>`（备用导出路径，主路径是 USB U 盘）。
 * 返回 0 成功；负数为错误码。 */
int YmodemTx_Send(const char *path);

#endif
