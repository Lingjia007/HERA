/**
 ******************************************************************************
 * @file wav_recorder.c
 * @brief WAV 录音落盘：PCM 16k/mono/S16 -> FAT 文件 RECxxxx.WAV（W25Q80）。
 *        录音期间由 audio_app 的消费循环喂块（10 ms / 160 采样），
 *        FIL 自带 512B 扇区缓冲，周期 f_sync 落盘，close 前回填 WAV 头。
 *
 * 掉电安全：f_sync 周期提交（默认每 1s），掉电最多丢最后一段未提交数据；
 * USB U 盘挂载期间固件端禁写（start 返回 -3，挂载沿自动停录音）。
 ******************************************************************************
 */
#include "wav_recorder.h"
#include "usb_msc.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

#define REC_SYNC_EVERY_SAMPLES (WAV_REC_SAMPLE_RATE * 1U) /* 每 1s f_sync */

static FIL s_file;
static uint8_t s_fs_ok; /* FatFS 已挂载（main 自检置位） */

static uint8_t s_active;
static uint32_t s_samples; /* 已写入的 PCM 采样数 */
static uint32_t s_seq;     /* 文件序号（seq.dat 持久化） */
static uint8_t s_rec_usb_disc; /* 录音时主动断开了 U 盘（停止后重连） */

/* 用小文件 seq.dat 持久化递增序号，掉电后不覆盖旧录音 */
static uint32_t load_seq(void)
{
    FIL f;
    uint32_t v = 0;
    UINT rd = 0;

    if (f_open(&f, "0:seq.dat", FA_READ) == FR_OK)
    {
        if (f_read(&f, &v, 4, &rd) != FR_OK || rd != 4U)
        {
            v = 0;
        }
        (void)f_close(&f);
    }
    return v;
}

static void save_seq(uint32_t v)
{
    FIL f;
    UINT wr = 0;

    if (f_open(&f, "0:seq.dat", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
    {
        (void)f_write(&f, &v, 4, &wr);
        (void)f_close(&f);
    }
}

static void build_header(uint8_t *h, uint32_t data_bytes)
{
    uint32_t rate = WAV_REC_SAMPLE_RATE;
    uint32_t byte_rate = rate * WAV_REC_CHANNELS * (WAV_REC_BITS / 8U);
    uint32_t riff = 36U + data_bytes;

    memcpy(h + 0, "RIFF", 4);
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    h[16] = 16; /* fmt chunk size */
    h[17] = 0; h[18] = 0; h[19] = 0;
    h[20] = 1;  /* PCM */
    h[21] = 0;
    h[22] = (uint8_t)WAV_REC_CHANNELS;
    h[23] = 0;
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byte_rate, 4);
    h[32] = (uint8_t)(WAV_REC_CHANNELS * (WAV_REC_BITS / 8U)); /* block align */
    h[33] = 0;
    h[34] = (uint8_t)WAV_REC_BITS;
    h[35] = 0;
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4);
}

void WavRecorder_Init(void)
{
    s_active = 0;
    s_samples = 0;
    s_fs_ok = 1;
    s_seq = load_seq();
}

uint8_t WavRecorder_active(void)
{
    return s_active;
}

int WavRecorder_start(void)
{
    uint8_t hdr[44];
    char name[20];
    UINT wr = 0;

    if (!s_fs_ok)
        return -1;
    if (s_active)
        return 0;
    if (UsbMsc_Mounted())
    {
        /* U 盘挂载中：断开（PC 视为拔出），录音独占 Flash 直到停止；
         * 停止后才重连（若立即重连，PC 挂载沿会触发"停录音"保护）。 */
        printf("[REC] usb mounted -> disconnect U-disk while recording\r\n");
        UsbMsc_Disconnect();
        s_rec_usb_disc = 1U;
    }

    snprintf(name, sizeof(name), "0:REC%04lu.WAV", (unsigned long)s_seq);

    if (f_open(&s_file, name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        printf("[REC] open %s failed\r\n", name);
        return -2;
    }

    /* 占位头：停止时回填真实长度 */
    memset(hdr, 0, sizeof(hdr));
    build_header(hdr, 0);
    if (f_write(&s_file, hdr, sizeof(hdr), &wr) != FR_OK || wr != sizeof(hdr))
    {
        (void)f_close(&s_file);
        printf("[REC] header write failed\r\n");
        return -3;
    }

    s_samples = 0;
    s_active = 1;
    printf("[REC] %s -> recording (max %us, KEY or 'stop' to finish)\r\n",
           name, (unsigned)WAV_REC_MAX_SECONDS);
    return 0;
}

int WavRecorder_stop(void)
{
    uint8_t hdr[44];
    uint32_t data_bytes;
    char name[20];
    UINT wr = 0;

    if (!s_active)
        return 0;
    s_active = 0;

    data_bytes = s_samples * 2U;
    build_header(hdr, data_bytes);

    /* 回到文件头回填长度，close 提交 */
    if (f_lseek(&s_file, 0) != FR_OK ||
        f_write(&s_file, hdr, sizeof(hdr), &wr) != FR_OK || wr != sizeof(hdr))
    {
        (void)f_close(&s_file);
        printf("[REC] finalize failed (data may be incomplete)\r\n");
        return -1;
    }
    if (f_close(&s_file) != FR_OK)
    {
        printf("[REC] close failed\r\n");
        return -2;
    }

    snprintf(name, sizeof(name), "REC%04lu.WAV", (unsigned long)s_seq);
    if (!UsbMsc_Mounted())
    {
        s_seq++;
        save_seq(s_seq);
    }

    /* 录音时断过 U 盘：重连，PC 重新枚举即可看到新文件 */
    if (s_rec_usb_disc != 0U)
    {
        s_rec_usb_disc = 0U;
        printf("[REC] usb U-disk re-connected\r\n");
        UsbMsc_Restart();
    }

    printf("[REC] saved %s: %lu.%02lus, %lu B\r\n", name,
           (unsigned long)(s_samples / WAV_REC_SAMPLE_RATE),
           (unsigned long)((s_samples % WAV_REC_SAMPLE_RATE) * 100U / WAV_REC_SAMPLE_RATE),
           (unsigned long)data_bytes);
    return (int)data_bytes;
}

void WavRecorder_feed(const int16_t *pcm, uint32_t n)
{
    UINT wr = 0;

    if (!s_active || pcm == NULL || n == 0U)
        return;

    if (f_write(&s_file, pcm, n * 2U, &wr) != FR_OK || wr != n * 2U)
    {
        printf("[REC] write failed -> stop\r\n");
        (void)WavRecorder_stop();
        return;
    }
    s_samples += n;

    /* 周期落盘：掉电最多丢最后 <1s 数据；每秒一次 4KB 擦除尖峰
     * 由 800ms 环形缓冲吸收 */
    if ((s_samples % REC_SYNC_EVERY_SAMPLES) < n)
    {
        (void)f_sync(&s_file);
    }

    if (s_samples >= WAV_REC_MAX_SECONDS * WAV_REC_SAMPLE_RATE)
    {
        printf("[REC] max duration reached -> stop\r\n");
        (void)WavRecorder_stop();
    }
}
