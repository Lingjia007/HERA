/**
 ******************************************************************************
 * @file shell_cli.c
 * @brief USART1 命令行（cmd 风格）：回显、退格、↑/↓ 历史、Ctrl+C 放弃行、
 *        Ctrl+U 清行；命令执行后仅打印 "> " 提示符（menu 才刷菜单）。
 *        文件命令：ls/rmall/df 同时覆盖 W25Q80("0:") 与 SD 卡（挂载时），
 *        录音文件统一在各卷 AUDIO_REC/ 子目录；rm/ym 裸文件名默认指向
 *        录音卷，也可带 "1:" 前缀显式指定。
 *        flactest <file>：miniflac 解码速度基准（读 SD/flash 上的 FLAC
 *        全速解码并统计，验证实时性；blocksize 上限 4608/2ch）。
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
#include "miniflac.h"
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
    printf("digit+Enter, cmd: help ls rm rmall df flactest rec stop ym kws sleep usb\r\n");
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
           "flactest <file>  flac decode speed benchmark\r\n"
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

/* ---- flactest: FLAC 解码速度基准（miniflac） -----------------------------
 * 读 SD/flash 上的 .flac 全速解码（只统计不落盘），验证 MCU 能否实时解码。
 * miniflac 无内部大缓冲：残差直接解码进调用方输出缓冲（平面 PCM，逐声道
 * 一条 blocksize*4B），故 RAM 上限由这里的静态缓冲决定；超过 blocksize
 * 上限的文件在帧头解析后拒绝，防缓冲溢出。 */
#define FLAC_IN_BUF  16384U /* SD -> miniflac 读入缓冲 */
#define FLAC_MAX_BS  4608U  /* blocksize 上限（flac 默认编码 <=4608） */
#define FLAC_MAX_CH  2U     /* 立体声 */

static miniflac_t s_flac;                              /* 解码器状态机 ~0.5KB */
static int32_t s_flac_pcm[FLAC_MAX_CH][FLAC_MAX_BS];   /* 平面 PCM 输出 36KB */
static uint8_t s_flac_in[FLAC_IN_BUF];                 /* 输入 16KB */

/* SD(SPI2) 与 USB MSC LUN1 共享总线：主循环读卡期间禁 USB_LP 防竞态，
 * USB 硬件对主机 NAK 挂起、使能后无损续传（同 w25_disk.c 的 spi_guard） */
static uint8_t flac_sd_lock(void)
{
    if (__get_IPSR() == 0U) /* thread mode = 主循环 */
    {
        HAL_NVIC_DisableIRQ(USB_LP_IRQn);
        return 1U;
    }
    return 0U;
}

static void flac_sd_unlock(uint8_t lock)
{
    if (lock != 0U)
    {
        HAL_NVIC_EnableIRQ(USB_LP_IRQn);
    }
}

/* DWT 周期计数器：Cortex-M4 自带，64MHz 下分辨率 15.6ns；
 * 每次调用前后取差值累计（单次差值 < 67s 回绕周期，安全） */
static void flac_dwt_start(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* 失步/解码失败取证：失败偏移 = 已读总量 - 缓冲内未消费量。打印现场
 * 16 字节后回卷重读同位置对比——两次内容不同 = 读路径瞬时损坏；
 * 相同且非法 = 卡上数据就是坏的（或当初拷贝就坏了）。 */
static void flac_dump_evidence(FIL *f, const uint8_t *buf, uint32_t in_pos,
                               uint32_t in_len, uint64_t bytes_in)
{
    uint32_t off = (uint32_t)(bytes_in - (uint64_t)(in_len - in_pos));
    uint8_t again[16];
    UINT rd = 0U, i;
    FRESULT fr;

    printf("[FLAC] fail offset %lu (0x%lX)\r\n",
           (unsigned long)off, (unsigned long)off);
    printf("[FLAC] in-buffer:");
    for (i = 0U; (i < 16U) && ((in_pos + i) < in_len); i++)
    {
        printf(" %02X", buf[in_pos + i]);
    }
    printf("\r\n");
    {
        uint8_t lock = flac_sd_lock();
        fr = f_lseek(f, off);
        if (fr == FR_OK)
        {
            fr = f_read(f, again, sizeof(again), &rd);
        }
        flac_sd_unlock(lock);
    }
    if ((fr == FR_OK) && (rd > 0U))
    {
        printf("[FLAC] re-read    :");
        for (i = 0U; i < rd; i++)
        {
            printf(" %02X", again[i]);
        }
        printf("\r\n");
    }
    else
    {
        printf("[FLAC] re-read FAILED (%d)\r\n", (int)fr);
    }
}

/* 续喂输入：把未消费数据搬到缓冲头并续读 SD。返回 FR_OK / 读错误；
 * 文件读完时 *rd_eof 置 1（缓冲内剩余仍可用）。读卡耗时计入 *cyc_rd。 */
static FRESULT flac_feed(FIL *f, uint32_t *in_pos, uint32_t *in_len,
                         uint64_t *bytes_in, uint32_t *rd_eof,
                         uint64_t *cyc_rd)
{
    FRESULT fr;
    UINT rd = 0U;
    uint32_t c0, c1;

    if (*in_pos > 0U)
    {
        if (*in_pos < *in_len)
        {
            memmove(s_flac_in, &s_flac_in[*in_pos], *in_len - *in_pos);
        }
        *in_len -= *in_pos;
        *in_pos = 0U;
    }
    if (*in_len < FLAC_IN_BUF)
    {
        uint8_t lock = flac_sd_lock();
        c0 = DWT->CYCCNT;
        fr = f_read(f, &s_flac_in[*in_len], (UINT)(FLAC_IN_BUF - *in_len), &rd);
        c1 = DWT->CYCCNT;
        flac_sd_unlock(lock);
        *cyc_rd += c1 - c0;
        if (fr != FR_OK)
        {
            return fr;
        }
        if (rd == 0U)
        {
            *rd_eof = 1U;
        }
        else
        {
            *in_len += rd;
            *bytes_in += rd;
        }
    }
    return FR_OK;
}

static void cmd_flactest(char *arg)
{
    char path[96];
    FIL f;
    FRESULT fr;
    MINIFLAC_RESULT r;
    uint32_t used, c0, c1;
    uint32_t in_len = 0U, in_pos = 0U, eof = 0U, need_feed = 1U, trunc = 0U;
    uint32_t frames = 0U, bs_max = 0U;
    uint64_t bytes_in = 0, samples_total = 0;
    uint64_t cyc_dec = 0, cyc_rd = 0;
    uint32_t sample_rate = 0U, t0_ms, t1_ms, clk_khz;
    uint8_t channels = 0U, bps = 0U;
    int32_t *pcm[FLAC_MAX_CH];
    uint64_t dec_ms, rd_ms, audio_ms = 0, rt100 = 0, kbps = 0;

    if (arg == NULL)
    {
        printf("usage: flactest <file>   (e.g. 1:/music/001.flac)\r\n");
        return;
    }
    if (arg[1] == ':')
    {
        (void)snprintf(path, sizeof(path), "%s", arg);
    }
    else if (g_sd_mounted != 0U) /* 裸名默认 SD 卷根 */
    {
        (void)snprintf(path, sizeof(path), "%s%s", SDPath, arg);
    }
    else
    {
        (void)snprintf(path, sizeof(path), "0:%s", arg);
    }
    if (WavRecorder_active()) /* FatFs 非重入，先停录音 */
    {
        printf("[FLAC] recording -> stop first\r\n");
        (void)WavRecorder_stop();
    }

    printf("[FLAC] miniflac %s, open %s\r\n", miniflac_version_string(), path);
    {
        uint8_t lock = flac_sd_lock();
        fr = f_open(&f, path, FA_READ);
        flac_sd_unlock(lock);
    }
    if (fr != FR_OK)
    {
        printf("[FLAC] f_open FAILED (%d)\r\n", (int)fr);
        return;
    }

    pcm[0] = s_flac_pcm[0];
    pcm[1] = s_flac_pcm[1];
    miniflac_init(&s_flac, MINIFLAC_CONTAINER_UNKNOWN);
    flac_dwt_start();
    t0_ms = HAL_GetTick();

    for (;;)
    {
        if (need_feed != 0U)
        {
            fr = flac_feed(&f, &in_pos, &in_len, &bytes_in, &eof, &cyc_rd);
            if (fr != FR_OK)
            {
                printf("[FLAC] f_read FAILED (%d)\r\n", (int)fr);
                goto done;
            }
            need_feed = 0U;
        }
        if (in_pos >= in_len && eof != 0U)
        {
            break; /* 输入耗尽 */
        }

        used = 0U; /* 空数据路径不写 out_length，必须预置 */
        c0 = DWT->CYCCNT;
        r = miniflac_sync(&s_flac, &s_flac_in[in_pos], in_len - in_pos, &used);
        c1 = DWT->CYCCNT;
        cyc_dec += c1 - c0;
        in_pos += used;

        if (r == MINIFLAC_CONTINUE)
        {
            if (eof != 0U)
            {
                break;
            }
            need_feed = 1U;
            continue;
        }
        if (r < 0)
        {
            flac_dump_evidence(&f, s_flac_in, in_pos, in_len, bytes_in);
            printf("[FLAC] sync FAILED r=%d at frame %lu (not a flac/ogg stream?)\r\n",
                   (int)r, (unsigned long)frames);
            goto done;
        }
        if (miniflac_is_frame(&s_flac) != 0U)
        {
            uint16_t bs = miniflac_frame_block_size(&s_flac);
            uint8_t ch = miniflac_frame_channels(&s_flac);

            /* 帧头已解析：解码前校验输出缓冲容量，防溢出 */
            if ((uint32_t)bs > FLAC_MAX_BS)
            {
                printf("[FLAC] blocksize %lu > %lu cap"
                       " (re-encode: flac --blocksize=4608)\r\n",
                       (unsigned long)bs, (unsigned long)FLAC_MAX_BS);
                goto done;
            }
            if ((uint32_t)ch > FLAC_MAX_CH)
            {
                printf("[FLAC] %u channels > %u unsupported\r\n",
                       (unsigned int)ch, (unsigned int)FLAC_MAX_CH);
                goto done;
            }
            if ((uint32_t)bs > bs_max)
            {
                bs_max = bs;
            }

            /* 续帧内循环：decode 返回 CONTINUE 后必须续喂并再调 decode。
             * 不能回外层走 sync —— sync 对"帧内未完成"状态会以
             * samples=NULL 解码后丢弃整帧（不计数），导致帧数被低估。 */
            for (;;)
            {
                used = 0U;
                c0 = DWT->CYCCNT;
                r = miniflac_decode(&s_flac, &s_flac_in[in_pos], in_len - in_pos,
                                    &used, pcm);
                c1 = DWT->CYCCNT;
                cyc_dec += c1 - c0;
                in_pos += used;

                if (r == MINIFLAC_OK)
                {
                    break;
                }
                if (r < 0)
                {
                    flac_dump_evidence(&f, s_flac_in, in_pos, in_len, bytes_in);
                    printf("[FLAC] decode FAILED r=%d at frame %lu"
                           " (corrupt stream?)\r\n", (int)r,
                           (unsigned long)frames);
                    goto done;
                }
                /* CONTINUE：输入耗尽即文件在帧中间结束 -> 截断 */
                if ((in_pos >= in_len) && (eof != 0U))
                {
                    printf("[FLAC] EOF mid-frame: truncated tail dropped\r\n");
                    trunc = 1U;
                    break;
                }
                fr = flac_feed(&f, &in_pos, &in_len, &bytes_in, &eof, &cyc_rd);
                if (fr != FR_OK)
                {
                    printf("[FLAC] f_read FAILED (%d)\r\n", (int)fr);
                    goto done;
                }
            }
            if (trunc != 0U)
            {
                break; /* 跳出外层，保留已有统计 */
            }

            frames++;
            samples_total += bs;
            sample_rate = miniflac_frame_sample_rate(&s_flac);
            channels = ch;
            bps = miniflac_frame_bps(&s_flac);
            if (frames == 1U)
            {
                printf("[FLAC] stream: %lu Hz, %u ch, %u bit\r\n",
                       (unsigned long)sample_rate, (unsigned int)channels,
                       (unsigned int)bps);
            }
            else if ((frames % 200U) == 0U)
            {
                printf("[FLAC] %lu frames...\r\n", (unsigned long)frames);
            }
        }
        /* metadata 块：sync 已流过，直接进入下一轮 */
    }

    t1_ms = HAL_GetTick();
    clk_khz = SystemCoreClock / 1000U;
    dec_ms = cyc_dec / clk_khz;
    rd_ms = cyc_rd / clk_khz;

    printf("[FLAC] ---- done ----\r\n");
    printf("[FLAC] input %lu B, %lu frames, blocksize max %lu\r\n",
           (unsigned long)bytes_in, (unsigned long)frames,
           (unsigned long)bs_max);
    if (sample_rate != 0U)
    {
        audio_ms = samples_total * 1000U / sample_rate;
    }
    if (sample_rate != 0U && dec_ms != 0U)
    {
        rt100 = audio_ms * 100U / dec_ms;
        printf("[FLAC] audio %lu.%02lu s @ %lu Hz %uch%ubit\r\n",
               (unsigned long)(audio_ms / 1000U),
               (unsigned long)((audio_ms % 1000U) / 10U),
               (unsigned long)sample_rate, (unsigned int)channels,
               (unsigned int)bps);
        printf("[FLAC] decode %lu.%03lu s -> realtime x%lu.%02lu (%s)\r\n",
               (unsigned long)(dec_ms / 1000U),
               (unsigned long)(dec_ms % 1000U),
               (unsigned long)(rt100 / 100U), (unsigned long)(rt100 % 100U),
               (rt100 >= 100U) ? "OK" : "TOO SLOW");
    }
    else
    {
        printf("[FLAC] no audio decoded (empty/metadata-only file?)\r\n");
    }
    if (rd_ms != 0U)
    {
        kbps = bytes_in * 1000U / 1024U / rd_ms;
        printf("[FLAC] sd read %lu.%03lu s, %lu KB/s\r\n",
               (unsigned long)(rd_ms / 1000U), (unsigned long)(rd_ms % 1000U),
               (unsigned long)kbps);
    }
    printf("[FLAC] wall %lu ms (tick)\r\n", (unsigned long)(t1_ms - t0_ms));
    printf("[FLAC] mem: miniflac %lu B + pcm %lu B + in %lu B = %lu B\r\n",
           (unsigned long)miniflac_size(),
           (unsigned long)sizeof(s_flac_pcm),
           (unsigned long)sizeof(s_flac_in),
           (unsigned long)(miniflac_size() + sizeof(s_flac_pcm) + sizeof(s_flac_in)));

done:
    {
        uint8_t lock = flac_sd_lock();
        (void)f_close(&f);
        flac_sd_unlock(lock);
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
    else if (strcmp(cmd, "flactest") == 0)
    {
        cmd_flactest(arg);
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
