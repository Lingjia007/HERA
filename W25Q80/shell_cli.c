/**
 ******************************************************************************
 * @file shell_cli.c
 * @brief USART1 命令行（cmd 风格）：回显、退格、↑/↓ 历史、Ctrl+C 放弃行、
 *        Ctrl+U 清行；命令执行后仅打印 "> " 提示符（menu 才刷菜单）。
 *        文件命令：ls/rmall/df 同时覆盖 W25Q80("0:") 与 SD 卡（挂载时），
 *        录音文件统一在各卷 AUDIO_REC/ 子目录；rm/ym 裸文件名默认指向
 *        录音卷，也可带 "1:" 前缀显式指定。
 *        KEY(PC12) 与 `rec` 命令等效（按键翻转录音）。
 *        USB U 盘挂载期间 flash 卷禁写（rm flash 文件拒绝），读命令照常。
 ******************************************************************************
 */
#include "shell_cli.h"
#include "wav_recorder.h"
#include "ymodem_tx.h"
#include "usb_msc.h"
#include "audio_app.h"
#include "sd_diskio.h"
#include "usart.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CLI_LINE_MAX 64
#define CLI_HIST_MAX 8

static uint8_t s_rx;
static char s_line[CLI_LINE_MAX];
static volatile uint16_t s_len;
static volatile uint8_t s_line_rdy;
static uint8_t s_menu_pending; /* '3'/'4' = 下一行输入是文件名 */
static uint8_t s_esc;          /* ANSI 转义序列状态机: 0 无 / 1 ESC / 2 ESC[ */
static char s_hist[CLI_HIST_MAX][CLI_LINE_MAX];
static uint8_t s_hist_cnt;
static uint8_t s_hist_pos;     /* 浏览位置，= cnt 表示回到空行 */

static void arm_rx(void)
{
    (void)HAL_UART_Receive_IT(&huart1, &s_rx, 1U);
}

static void tx_str(const char *s, uint16_t n)
{
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)s, n, 10U);
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
    printf(" 9 rm ALL files\r\n");
    if (UsbMsc_Mounted())
    {
        printf("usb U-DISK mounted, luns: flash + sd (MCU readonly)\r\n");
    }
    else if (UsbMsc_Enabled())
    {
        printf("usb U-DISK on (waiting pc)\r\n");
    }
    else
    {
        printf("usb U-DISK off (cmd: usb on)\r\n");
    }
    if (g_sd_mounted != 0U)
    {
        printf("sd %s mounted, record -> %s\r\n", SDPath, WavRecorder_GetVolume());
    }
    else
    {
        printf("sd not mounted, record -> %s (flash)\r\n", WavRecorder_GetVolume());
    }
    printf("digit+Enter, cmd: help ls rm rmall df rec stop ym kws sleep usb\r\n");
    printf("up/down history, backspace edit, Ctrl+U clear, Ctrl+C cancel\r\n");
    printf("> ");
}

void Shell_CLI_Init(void)
{
    s_len = 0;
    s_line_rdy = 0;
    s_menu_pending = 0;
    s_esc = 0;
    s_hist_cnt = 0;
    s_hist_pos = 0;
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

/* ---- 行编辑辅助（RX 中断上下文） ---------------------------------------- */

/* 擦掉终端上已回显的当前行 */
static void line_erase(void)
{
    while (s_len != 0U)
    {
        s_len--;
        tx_str("\b \b", 3U);
    }
}

/* 历史浏览后回显新行内容（line_erase 已清空 s_len） */
static void hist_show(void)
{
    uint16_t n;

    if (s_hist_pos < s_hist_cnt)
    {
        n = (uint16_t)strlen(s_hist[s_hist_pos]);
        if (n > CLI_LINE_MAX - 1U)
        {
            n = CLI_LINE_MAX - 1U;
        }
        memcpy(s_line, s_hist[s_hist_pos], n);
        s_len = n;
        tx_str(s_line, n);
    }
}

static void hist_nav(int8_t dir) /* -1 = up, +1 = down */
{
    if (dir < 0)
    {
        if (s_hist_pos != 0U)
        {
            s_hist_pos--;
            line_erase();
            hist_show();
        }
    }
    else
    {
        if (s_hist_pos < s_hist_cnt)
        {
            s_hist_pos++;
            line_erase();
            hist_show();
        }
    }
}

/* 非空行入历史（去重、滚动），浏览位置复位 */
static void hist_commit(void)
{
    s_line[s_len] = '\0';
    if (s_len != 0U)
    {
        if (s_hist_cnt == 0U || strcmp(s_hist[s_hist_cnt - 1U], s_line) != 0)
        {
            if (s_hist_cnt < CLI_HIST_MAX)
            {
                (void)strcpy(s_hist[s_hist_cnt], s_line);
                s_hist_cnt++;
            }
            else
            {
                memmove(s_hist[0], s_hist[1], (CLI_HIST_MAX - 1U) * CLI_LINE_MAX);
                (void)strcpy(s_hist[CLI_HIST_MAX - 1U], s_line);
            }
        }
    }
    s_hist_pos = s_hist_cnt;
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

    if (s_line_rdy)
    {
        return; /* 上一行未处理，丢弃溢出输入 */
    }

    /* ANSI 转义序列（方向键 Up/Down）: ESC [ A / ESC [ B */
    if (s_esc == 2U)
    {
        s_esc = 0U;
        if (s_rx == (uint8_t)'A')
        {
            hist_nav(-1);
        }
        else if (s_rx == (uint8_t)'B')
        {
            hist_nav(1);
        }
        return;
    }
    if (s_esc == 1U)
    {
        s_esc = (s_rx == (uint8_t)'[') ? 2U : 0U;
        return;
    }
    if (s_rx == 0x1BU)
    {
        s_esc = 1U;
        return;
    }

    if (s_rx == (uint8_t)'\r' || s_rx == (uint8_t)'\n')
    {
        hist_commit();
        s_line[s_len] = '\0';
        s_len = 0; /* 行已提交：清空编辑缓冲，下一行从 0 开始收集 */
        s_line_rdy = 1U; /* 空行也提交：Process 只回显 CRLF + 新提示符 */
        return;
    }
    if (s_rx == 0x03U) /* Ctrl+C：放弃当前行 */
    {
        s_len = 0;
        s_esc = 0U;
        s_line[0] = '\0';
        tx_str("^C", 2U);
        s_line_rdy = 1U; /* 提交空行 -> Process 刷新提示符 */
        return;
    }
    if (s_rx == 0x15U) /* Ctrl+U：清空当前行 */
    {
        line_erase();
        return;
    }
    if (s_rx == 0x08U || s_rx == 0x7FU) /* 退格 */
    {
        if (s_len != 0U)
        {
            s_len--;
            tx_str("\b \b", 3U);
        }
        return;
    }
    if (s_rx >= 0x20U && s_rx < 0x7FU) /* 可打印字符才入行并回显 */
    {
        if (s_len < CLI_LINE_MAX - 1U)
        {
            s_line[s_len++] = (char)s_rx;
            tx_str((const char *)&s_rx, 1U);
        }
        else
        {
            tx_str("\a", 1U); /* 行满响铃 */
        }
    }
}

/* ---- 命令实现 ------------------------------------------------------------- */

static void cmd_help(void)
{
    printf("menu             show the menu\r\n"
           "ls               list files (flash + sd)\r\n"
           "rm <file>        delete file (bare name = record volume)\r\n"
           "rmall            delete ALL files on flash + sd\r\n"
           "df               fs usage (flash + sd)\r\n"
           "rec              toggle recording (KEY/PC12 same)\r\n"
           "stop             stop recording\r\n"
           "ym <file>        send file via YMODEM (backup of usb)\r\n"
           "kws on|off       toggle speech recognition\r\n"
           "sleep            enter STOP2 now (KEY/sound wakes)\r\n"
           "usb on|off       mount/unmount u-disk\r\n"
           "info             system info\r\n");
}

/* 录音文件统一在 "<卷>:AUDIO_REC/" 下：裸名 -> 录音卷，
 * 'N:NAME' -> N 卷。返回 buf 便于直接传给 f_unlink/YmodemTx_Send。 */
static const char *rec_path(const char *name, char *buf, size_t n)
{
    if (name[1] == ':')
    {
        (void)snprintf(buf, n, "%c:%s/%s", name[0], WAV_REC_DIR, name + 2);
    }
    else
    {
        (void)snprintf(buf, n, "%s%s/%s", WavRecorder_GetVolume(), WAV_REC_DIR, name);
    }
    return buf;
}

static void list_volume(const char *vol, const char *tag)
{
    DIR d;
    FILINFO info;
    char path[24];
    uint32_t cnt = 0;

    (void)snprintf(path, sizeof(path), "%s" WAV_REC_DIR, vol);
    printf("---- %s (%s) ----\r\n", path, tag);
    if (f_opendir(&d, path) != FR_OK)
    {
        printf("no " WAV_REC_DIR " dir (no recordings yet)\r\n");
        return;
    }
    while (f_readdir(&d, &info) == FR_OK && info.fname[0] != '\0')
    {
        if (!(info.fattrib & AM_DIR))
        {
            printf("%-14s %6lu B\r\n", info.fname, (unsigned long)info.fsize);
            cnt++;
        }
    }
    (void)f_closedir(&d);
    printf("%lu file(s)\r\n", (unsigned long)cnt);
}

static void cmd_ls(void)
{
    list_volume("0:", "flash/usb");
    if (g_sd_mounted != 0U)
    {
        list_volume(SDPath, "sd card");
    }
}

static void cmd_rm(char *name)
{
    char path[96];

    if (name == NULL)
    {
        printf("usage: rm <file>   (in " WAV_REC_DIR ", bare = record vol, or '1:NAME')\r\n");
        return;
    }
    if (UsbMsc_Mounted())
    {
        printf("usb mounted (readonly, both luns) - eject disk first\r\n");
        return;
    }
    (void)rec_path(name, path, sizeof(path));
    FRESULT e = f_unlink(path);
    printf("rm %s: %s\r\n", path, (e == FR_OK) ? "ok" : "failed");
}

static uint32_t purge_volume(const char *vol)
{
    DIR d;
    FILINFO info;
    char dir[24];
    char path[96];
    uint32_t cnt = 0;

    (void)snprintf(dir, sizeof(dir), "%s" WAV_REC_DIR, vol);
    if (f_opendir(&d, dir) != FR_OK)
    {
        return 0U;
    }
    while (f_readdir(&d, &info) == FR_OK && info.fname[0] != '\0')
    {
        if (!(info.fattrib & AM_DIR))
        {
            (void)snprintf(path, sizeof(path), "%s/%s", dir, info.fname);
            if (f_unlink(path) == FR_OK)
            {
                cnt++;
            }
            else
            {
                printf("rm %s: failed\r\n", path);
            }
        }
    }
    (void)f_closedir(&d);
    return cnt;
}

static void cmd_rmall(void)
{
    uint8_t usb_was = 0;
    uint32_t cnt;

    if (WavRecorder_active())
    {
        printf("recording -> stop first\r\n");
        (void)WavRecorder_stop();
    }
    if (UsbMsc_Mounted())
    {
        /* PC 弹出不会清 configured 状态，只能固件主动断开拿回写权限；
         * PC 端未同步的写入本来就在删除范围内，直接断开是安全的。 */
        printf("usb mounted -> disconnect for write access\r\n");
        UsbMsc_Disconnect();
        usb_was = 1U;
    }
    printf("purge flash 0:" WAV_REC_DIR "\r\n");
    cnt = purge_volume("0:");
    if (g_sd_mounted != 0U)
    {
        printf("purge sd %s" WAV_REC_DIR "\r\n", SDPath);
        cnt += purge_volume(SDPath);
    }
    printf("rm all: %lu removed\r\n", (unsigned long)cnt);
    if (usb_was != 0U)
    {
        UsbMsc_Restart(); /* PC 重新枚举，看到清空后的盘 */
    }
}

static void df_volume(const char *vol, const char *tag)
{
    FATFS *fs = NULL;
    DWORD fre = 0;

    if (f_getfree(vol, &fre, &fs) != FR_OK || fs == NULL)
    {
        printf("%s %s: not mounted\r\n", tag, vol);
        return;
    }
    printf("%s %s: %lu/%lu KB free\r\n", tag, vol,
           (unsigned long)(fre * (fs->csize / 2U)),
           (unsigned long)((fs->n_fatent - 2U) * (fs->csize / 2U)));
}

static void cmd_df(void)
{
    df_volume("0:", "flash");
    if (g_sd_mounted != 0U)
    {
        df_volume(SDPath, "sd");
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

static void cmd_usb(char *arg)
{
    if (arg != NULL && strcmp(arg, "on") == 0)
    {
        UsbMsc_SetEnabled(1U);
    }
    else if (arg != NULL && strcmp(arg, "off") == 0)
    {
        UsbMsc_SetEnabled(0U);
    }
    printf("usb %s\r\n",
           UsbMsc_Mounted() ? "mounted (MCU readonly)"
                            : (UsbMsc_Enabled() ? "on (waiting pc)" : "off"));
}

static void cmd_info(void)
{
    printf("mcu: STM32WB55, pcm: %u Hz mono S16\r\n", 16000U);
    printf("flash: W25Q80 1MB, fs: FAT12 512B sector\r\n");
    printf("record -> %s" WAV_REC_DIR "/ (%s)\r\n", WavRecorder_GetVolume(),
           WavRecorder_OnFlash() ? "flash" : "sd card");
    if (g_sd_mounted != 0U)
    {
        printf("sd: mounted '%s'\r\n", SDPath);
    }
    else
    {
        printf("sd: not mounted\r\n");
    }
    printf("kws: %s, key: PC12 toggle record\r\n",
           (AudioApp_GetKwsEnabled() != 0U) ? "on" : "off");
    if (UsbMsc_Mounted())
    {
        printf("usb: U-disk mounted, luns: flash + sd (MCU readonly)\r\n");
    }
    else
    {
        printf("usb: %s\r\n", UsbMsc_Enabled() ? "on (waiting pc)" : "off");
    }
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
        if (s_line[0] != '\0')
        {
            if (pend == '3')
            {
                char path[96];
                (void)YmodemTx_Send(rec_path(s_line, path, sizeof(path)));
            }
            else
            {
                cmd_rm(s_line);
            }
        }
        printf("> ");
        return;
    }

    cmd = strtok(s_line, " ");
    arg = strtok(NULL, " ");
    printf("\r\n");

    if (cmd == NULL)
    {
        /* 空行：仅刷新提示符 */
    }
    else if (strlen(cmd) == 1U && cmd[0] >= '1' && cmd[0] <= '9')
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
            s_menu_pending = (uint8_t)cmd[0];
            printf("%s - enter file name: ",
                   (cmd[0] == '3') ? "ymodem" : "rm");
            return; /* 等文件名行，不刷提示符 */
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
        case '9':
            cmd_rmall();
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
        cmd_rm(arg);
    }
    else if (strcmp(cmd, "rmall") == 0)
    {
        cmd_rmall();
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
        if (arg == NULL)
        {
            printf("usage: ym <file>   (in " WAV_REC_DIR ", bare = record vol, or '1:NAME')\r\n");
        }
        else
        {
            char path[96];
            (void)YmodemTx_Send(rec_path(arg, path, sizeof(path)));
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
    else if (strcmp(cmd, "usb") == 0)
    {
        cmd_usb(arg);
    }
    else if (strcmp(cmd, "info") == 0)
    {
        cmd_info();
    }
    else
    {
        printf("unknown: %s (try 'help')\r\n", cmd);
    }
    printf("> ");
}
