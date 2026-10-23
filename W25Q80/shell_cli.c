/**
 ******************************************************************************
 * @file shell_cli.c
 * @brief USART1 命令行：ls/rm/df/rec/stop/ym/info/help。
 *        单字节中断接收 -> 行缓冲 -> 主循环解析。
 *        KEY(PC12) 与 `rec` 命令等效（按键翻转录音）。
 ******************************************************************************
 */
#include "shell_cli.h"
#include "wav_recorder.h"
#include "ymodem_tx.h"
#include "audio_app.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CLI_LINE_MAX 64

static lfs_t *s_lfs;
static uint8_t s_rx;
static char s_line[CLI_LINE_MAX];
static volatile uint16_t s_len;
static volatile uint8_t s_line_rdy;
static uint8_t s_menu_pending; /* '3'/'4' = 下一行输入是文件名 */

static void arm_rx(void)
{
    (void)HAL_UART_Receive_IT(&huart1, &s_rx, 1U);
}

/* F407 风格控制菜单（数字+回车，或直接输文本命令） */
static void print_menu(void)
{
    printf("\r\n========= HERA Menu =========\r\n");
    printf(" 1 rec on/off   2 ls files\r\n");
    printf(" 3 ymodem send  4 rm file\r\n");
    printf(" 5 df usage     6 kws %s\r\n",
           (AudioApp_GetKwsEnabled() != 0U) ? "ON" : "OFF");
    printf(" 7 sleep now    8 info\r\n");
    printf("digit+Enter, or cmd: help ls rm df rec stop ym kws sleep\r\n");
    printf("> ");
}

void Shell_CLI_Init(lfs_t *lfs)
{
    s_lfs = lfs;
    s_len = 0;
    s_line_rdy = 0;
    s_menu_pending = 0;
    /* USART1 RX 中断（向量在 main.c USER CODE 4），优先级低于音频 DMA */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    arm_rx();
    AudioApp_HoldAwake(600000U); /* 开机保活 10 分钟，之后靠串口活动续期 */
    print_menu();
}

void Shell_CLI_Pause(void)
{
    (void)HAL_UART_AbortReceive(&huart1);
}

void Shell_CLI_Resume(void)
{
    s_len = 0;
    s_line_rdy = 0;
    arm_rx();
}

/* 单字节接收回调（弱符号覆盖） */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }
    AudioApp_HoldAwake(300000U); /* 串口活动续期保活，STOP2 被否决 */
    (void)HAL_UART_Receive_IT(&huart1, &s_rx, 1U);

    if (s_rx == (uint8_t)'\r' || s_rx == (uint8_t)'\n')
    {
        if (s_len != 0U)
        {
            s_line[s_len] = '\0';
            s_line_rdy = 1U; /* 不再接收新字节，行处理完后重新武装 */
        }
        return;
    }
    if (s_line_rdy)
    {
        return; /* 上一行未处理，丢弃溢出输入 */
    }
    if (s_rx == 0x08U || s_rx == 0x7FU) /* 退格 */
    {
        if (s_len != 0U)
        {
            s_len--;
            (void)HAL_UART_Transmit(&huart1, (uint8_t *)"\b \b", 3U, 10U);
        }
        return;
    }
    if (s_len < CLI_LINE_MAX - 1U)
    {
        s_line[s_len++] = (char)s_rx;
        (void)HAL_UART_Transmit(&huart1, &s_rx, 1U, 10U); /* 回显 */
    }
}

static void cmd_help(void)
{
    printf("menu             show the menu\r\n"
           "ls               list files\r\n"
           "rm <file>        delete file\r\n"
           "df               fs usage\r\n"
           "rec              toggle recording (KEY/PC12 same)\r\n"
           "stop             stop recording\r\n"
           "ym <file>        send file via YMODEM\r\n"
           "kws on|off       toggle speech recognition\r\n"
           "sleep            enter STOP2 now (KEY/sound wakes)\r\n"
           "info             system info\r\n");
}

static void cmd_ls(void)
{
    lfs_dir_t d;
    struct lfs_info info;
    uint32_t cnt = 0;

    if (s_lfs == NULL)
    {
        printf("fs not mounted\r\n");
        return;
    }
    if (lfs_dir_open(s_lfs, &d, "/") != LFS_ERR_OK)
    {
        printf("dir open failed\r\n");
        return;
    }
    while (lfs_dir_read(s_lfs, &d, &info) > 0)
    {
        if (info.type == LFS_TYPE_REG)
        {
            printf("%-14s %6lu B\r\n", info.name, (unsigned long)info.size);
            cnt++;
        }
    }
    (void)lfs_dir_close(s_lfs, &d);
    printf("%lu file(s)\r\n", (unsigned long)cnt);
}

static void cmd_rm(char *name)
{
    if (name == NULL)
    {
        printf("usage: rm <file>\r\n");
        return;
    }
    int e = lfs_remove(s_lfs, name);
    printf("rm %s: %s\r\n", name, (e == 0) ? "ok" : "failed");
}

static void cmd_df(void)
{
    if (s_lfs == NULL)
    {
        printf("fs not mounted\r\n");
        return;
    }
    lfs_ssize_t used = lfs_fs_size(s_lfs);
    if (used >= 0)
    {
        printf("fs: %ld/%lu blocks (%ld B free)\r\n", (long)used,
               (unsigned long)256, (long)(256 - used) * 4096L);
    }
    else
    {
        printf("fs stat failed (%ld)\r\n", (long)used);
    }
}

static void cmd_rec(void)
{
    if (WavRecorder_active())
    {
        (void)WavRecorder_stop();
    }
    else
    {
        if (WavRecorder_start() == 0)
        {
            AudioApp_ZplNotify(); /* 打开音频会话，启动采集流 */
        }
    }
}

static void cmd_info(void)
{
    printf("mcu: STM32WB55, pcm: %u Hz mono S16\r\n", 16000U);
    printf("flash: W25Q80 1MB, fs: littlefs 256x4KB\r\n");
    printf("kws: %s, key: PC12 toggle record, cli: %s\r\n",
           (AudioApp_GetKwsEnabled() != 0U) ? "on" : "off",
           (s_lfs != NULL) ? "fs ok" : "fs NOT mounted");
}

void Shell_CLI_Process(void)
{
    char *cmd, *arg;

    /* 睡眠唤醒或错误后 RX 中断可能未武装，自愈 */
    if (huart1.RxState == HAL_UART_STATE_READY && s_line_rdy == 0U)
    {
        arm_rx();
    }
    if (s_line_rdy == 0U)
    {
        return;
    }
    s_line_rdy = 0;

    /* 菜单项 3/4 的第二行：文件名 */
    if (s_menu_pending == '3' || s_menu_pending == '4')
    {
        uint8_t pend = s_menu_pending;
        s_menu_pending = 0U;
        printf("\r\n");
        if (s_line[0] != '\0' && s_lfs != NULL)
        {
            if (pend == '3')
            {
                (void)YmodemTx_Send(s_lfs, s_line);
            }
            else
            {
                cmd_rm(s_line);
            }
        }
        else if (s_lfs == NULL)
        {
            printf("fs not mounted\r\n");
        }
        print_menu();
        return;
    }

    cmd = strtok(s_line, " ");
    arg = strtok(NULL, " ");
    printf("\r\n");

    if (cmd == NULL)
    {
        /* 空行 */
    }
    else if (strlen(cmd) == 1U && cmd[0] >= '1' && cmd[0] <= '8')
    {
        /* 数字菜单项 */
        switch (cmd[0])
        {
        case '1':
            cmd_rec();
            break;
        case '2':
            cmd_ls();
            break;
        case '3': /* YMODEM：下一行输入文件名 */
        case '4': /* 删除：同上 */
            if (s_lfs == NULL)
            {
                printf("fs not mounted\r\n");
            }
            else
            {
                s_menu_pending = (uint8_t)cmd[0];
                printf("%s - enter file name: ",
                       (cmd[0] == '3') ? "ymodem" : "rm");
                return; /* 等文件名行，不重印菜单 */
            }
            break;
        case '5':
            cmd_df();
            break;
        case '6':
            AudioApp_SetKwsEnabled((AudioApp_GetKwsEnabled() == 0U) ? 1U : 0U);
            break;
        case '7':
            AudioApp_RequestSleep();
            break;
        case '8':
            cmd_info();
            break;
        default:
            break;
        }
    }
    else if (strcmp(cmd, "help") == 0)
    {
        cmd_help();
    }
    else if (strcmp(cmd, "menu") == 0)
    {
        print_menu();
        return;
    }
    else if (strcmp(cmd, "ls") == 0)
    {
        cmd_ls();
    }
    else if (strcmp(cmd, "rm") == 0)
    {
        if (s_lfs == NULL)
        {
            printf("fs not mounted\r\n");
        }
        else
        {
            cmd_rm(arg);
        }
    }
    else if (strcmp(cmd, "df") == 0)
    {
        cmd_df();
    }
    else if (strcmp(cmd, "rec") == 0)
    {
        cmd_rec();
    }
    else if (strcmp(cmd, "stop") == 0)
    {
        printf("%s\r\n", (WavRecorder_stop() >= 0) ? "stopped" : "not recording");
    }
    else if (strcmp(cmd, "ym") == 0 || strcmp(cmd, "ymodem") == 0)
    {
        if (s_lfs == NULL || arg == NULL)
        {
            printf("usage: ym <file>\r\n");
        }
        else
        {
            (void)YmodemTx_Send(s_lfs, arg);
        }
    }
    else if (strcmp(cmd, "kws") == 0)
    {
        if (arg != NULL && strcmp(arg, "on") == 0)
        {
            AudioApp_SetKwsEnabled(1U);
        }
        else if (arg != NULL && strcmp(arg, "off") == 0)
        {
            AudioApp_SetKwsEnabled(0U);
        }
        else
        {
            AudioApp_SetKwsEnabled((AudioApp_GetKwsEnabled() == 0U) ? 1U : 0U);
        }
    }
    else if (strcmp(cmd, "sleep") == 0)
    {
        AudioApp_RequestSleep();
    }
    else if (strcmp(cmd, "info") == 0)
    {
        cmd_info();
    }
    else
    {
        printf("unknown: %s (try 'help')\r\n", cmd);
    }
    print_menu();
}
