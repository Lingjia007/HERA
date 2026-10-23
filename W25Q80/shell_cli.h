#ifndef SHELL_CLI_H
#define SHELL_CLI_H

/* USART1 行命令接口。Shell_CLI_Process() 需在主循环轮询。
 * 注意：STOP2 睡眠期间 UART 无时钟，命令只在唤醒状态下可用；
 * Process 会在 RX 掉线（睡眠唤醒/错误）后自动重新武装接收。
 * 文件命令走 FatFS("0:")（W25Q80），与 USB U 盘同一份数据。 */
void Shell_CLI_Init(void);
void Shell_CLI_Process(void);

/* YMODEM 传输期间挂起/恢复 CLI 的 RX 中断（协议独占串口） */
void Shell_CLI_Pause(void);
void Shell_CLI_Resume(void);

#endif
