/**
 ******************************************************************************
 * @file wav_recorder.c
 * @brief WAV 录音落盘：PCM 16k/mono/S16 -> FAT 文件 RECxxxx.WAV。
 *        录音目标卷可配置（SD 卡优先，回退 W25Q80 "0:"），
 *        录音期间由 audio_app 的消费循环喂块（10 ms / 160 采样），
 *        FIL 自带 512B 扇区缓冲，周期 f_sync 落盘，close 前回填 WAV 头。
 *
 * 掉电安全：f_sync 周期提交（默认每 1s），掉电最多丢最后一段未提交数据；
 * USB U 盘挂载期间固件端禁写 flash 卷（start 自动断开，挂载沿自动停录音）；
 * 录音在 SD 卷时与 U 盘只读挂载互不影响。
 ******************************************************************************
 */
#include "wav_recorder.h"
#include "usb_msc.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

#define REC_SYNC_EVERY_SAMPLES (WAV_REC_SAMPLE_RATE * 1U) /* 每 1s f_sync */
#define REC_MAX_S_FLASH WAV_REC_MAX_SECONDS /* W25Q80 1MB 上限 28s */
#define REC_MAX_S_SD    600U                /* SD 卡上限 10 分钟 */

static FIL s_file;
static uint8_t s_fs_ok; /* FatFS 已挂载（main 自检置位） */
static char s_vol[4] = "0:";  /* 录音目标卷（"0:" 或 SDPath） */

static uint8_t s_active;
static uint32_t s_samples; /* 已写入的 PCM 采样数 */
static uint32_t s_max_samples; /* 本卷录音上限（feed 里判停） */
static uint32_t s_seq;     /* 文件序号（seq.dat 持久化，每卷独立） */
static uint8_t s_rec_usb_disc; /* 录音时主动断开了 U 盘（停止后重连） */

/* 确保录音目录存在（幂等：已存在/无卡均无害，失败留给 f_open 报） */
static void ensure_dir(void)
{
    char path[24];

    (void)snprintf(path, sizeof(path), "%s" WAV_REC_DIR, s_vol);
    (void)f_mkdir(path); /* FR_EXIST / FR_NOT_READY 直接忽略 */
}

/* 用小文件 seq.dat 持久化递增序号，掉电后不覆盖旧录音 */
static uint32_t load_seq(void)
{
    FIL f;
    uint32_t v = 0;
    UINT rd = 0;
    char path[24];

    (void)snprintf(path, sizeof(path), "%s" WAV_REC_DIR "/seq.dat", s_vol);
    if (f_open(&f, path, FA_READ) == FR_OK)
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
    char path[24];

    (void)snprintf(path, sizeof(path), "%s" WAV_REC_DIR "/seq.dat", s_vol);
    if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
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

uint8_t WavRecorder_OnFlash(void)
{
    return (uint8_t)(s_vol[0] == '0' && s_vol[1] == ':');
}

const char *WavRecorder_GetVolume(void)
{
    return s_vol;
}

int WavRecorder_SetVolume(const char *vol)
{
    if (s_active || vol == NULL || vol[0] == '\0' || vol[1] != ':')
    {
        return -1;
    }
    if (vol[0] == s_vol[0])
    {
        return 0; /* 同卷 */
    }
    s_vol[0] = vol[0];
    s_vol[1] = ':';
    s_vol[2] = '\0';
    if (!UsbMsc_Mounted())
    {
        ensure_dir(); /* U 盘挂载中不写 FAT，留待 start() 里补 */
    }
    s_seq = load_seq(); /* seq.dat 每卷独立 */
    printf("[REC] volume -> %s" WAV_REC_DIR "/ (seq %lu)\r\n",
           s_vol, (unsigned long)s_seq);
    return 0;
}

void WavRecorder_Init(const char *vol)
{
    s_active = 0;
    s_samples = 0;
    s_fs_ok = 1;
    if (vol != NULL && vol[0] != '\0' && vol[1] == ':')
    {
        s_vol[0] = vol[0];
        s_vol[1] = ':';
        s_vol[2] = '\0';
    }
    ensure_dir(); /* 开机即建好，插卡/上电 PC 直接可见 AUDIO_REC */
    s_seq = load_seq();
    printf("[REC] volume %s" WAV_REC_DIR "/ (seq %lu)\r\n",
           s_vol, (unsigned long)s_seq);
}

uint8_t WavRecorder_active(void)
{
    return s_active;
}

int WavRecorder_start(void)
{
    uint8_t hdr[44];
    char name[32];
    UINT wr = 0;

    if (!s_fs_ok)
        return -1;
    if (s_active)
        return 0;

    for (;;)
    {
        if (UsbMsc_Mounted())
        {
            /* U 盘挂载中（PC 对 flash/SD LUN 裸块访问）：断开（PC 视为
             * 拔出），录音独占目标卷直到停止；停止后才重连（立即重连会
             * 触发挂载沿"停录音"保护）。Disconnect 内部已刷新 FatFs 缓存。 */
            printf("[REC] usb mounted -> disconnect U-disk while recording\r\n");
            UsbMsc_Disconnect();
            s_rec_usb_disc = 1U;
        }
        ensure_dir(); /* 此刻必无 USB 会话，可安全建目录 */

        snprintf(name, sizeof(name), "%s" WAV_REC_DIR "/REC%04lu.WAV",
                 s_vol, (unsigned long)s_seq);

        if (f_open(&s_file, name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
        {
            s_max_samples = (WavRecorder_OnFlash() ? REC_MAX_S_FLASH
                                                   : REC_MAX_S_SD) *
                            WAV_REC_SAMPLE_RATE;
            break;
        }
        if (WavRecorder_OnFlash())
        {
            printf("[REC] open %s failed\r\n", name);
            return -2;
        }
        /* SD 卷打不开（满/拔卡/异常）：回退 W25Q80 重试一次 */
        printf("[REC] open on %s failed -> fallback flash 0:\r\n", s_vol);
        (void)WavRecorder_SetVolume("0:");
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
           name, (unsigned)(s_max_samples / WAV_REC_SAMPLE_RATE));
    return 0;
}

int WavRecorder_stop(void)
{
    uint8_t hdr[44];
    uint32_t data_bytes;
    char name[32];
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

    snprintf(name, sizeof(name), WAV_REC_DIR "/REC%04lu.WAV", (unsigned long)s_seq);
    if (!WavRecorder_OnFlash() || !UsbMsc_Mounted())
    {
        s_seq++;
        save_seq(s_seq); /* SD 卷随时可写；flash 卷须 USB 未挂载 */
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

    if (s_samples >= s_max_samples)
    {
        printf("[REC] max duration reached -> stop\r\n");
        (void)WavRecorder_stop();
    }
}
