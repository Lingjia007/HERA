/**
 ******************************************************************************
 * @file wav_recorder.c
 * @brief WAV 录音落盘：PCM 16k/mono/S16 -> littlefs 文件 RECxxxx.WAV。
 *        录音期间由 audio_app 的消费循环喂块（10 ms / 160 采样），
 *        lfs 的 256B 程序缓存吸收小写入，close 前回填最终 WAV 头。
 *
 * 掉电安全：littlefs 幂等提交，已 close 的文件不会因掉电损坏；
 * 录音中途掉电最多丢最后一段未提交数据（文件不可见，不留垃圾）。
 ******************************************************************************
 */
#include "wav_recorder.h"
#include <stdio.h>
#include <string.h>

static lfs_t *s_lfs;
static lfs_file_t s_file;
static uint8_t s_file_cache[256]; /* lfs 静态文件缓存（LFS_NO_MALLOC） */

static uint8_t s_active;
static uint32_t s_samples; /* 已写入的 PCM 采样数 */
static uint32_t s_seq;     /* 文件序号（seq.dat 持久化） */

/* 用小文件 seq.dat 持久化递增序号，掉电后不覆盖旧录音 */
static uint32_t load_seq(void)
{
    lfs_file_t f;
    uint32_t v = 0;
    struct lfs_file_config fcfg = {0};

    fcfg.buffer = s_file_cache;
    if (lfs_file_opencfg(s_lfs, &f, "seq.dat",
                         LFS_O_RDONLY, &fcfg) == LFS_ERR_OK)
    {
        (void)lfs_file_read(s_lfs, &f, &v, 4);
        (void)lfs_file_close(s_lfs, &f);
    }
    return v;
}

static void save_seq(uint32_t v)
{
    lfs_file_t f;
    struct lfs_file_config fcfg = {0};

    fcfg.buffer = s_file_cache;
    if (lfs_file_opencfg(s_lfs, &f, "seq.dat",
                         LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC,
                         &fcfg) == LFS_ERR_OK)
    {
        (void)lfs_file_write(s_lfs, &f, &v, 4);
        (void)lfs_file_close(s_lfs, &f);
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

void WavRecorder_Init(lfs_t *lfs)
{
    s_lfs = lfs;
    s_active = 0;
    s_samples = 0;
    s_seq = (lfs != NULL) ? load_seq() : 0U;
}

uint8_t WavRecorder_active(void)
{
    return s_active;
}

int WavRecorder_start(void)
{
    uint8_t hdr[44];

    if (s_lfs == NULL)
        return -1;
    if (s_active)
        return 0;

    char name[16];
    snprintf(name, sizeof(name), "REC%04lu.WAV", (unsigned long)s_seq);

    static struct lfs_file_config fcfg;
    fcfg.buffer = s_file_cache;

    if (lfs_file_opencfg(s_lfs, &s_file, name,
                         LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC,
                         &fcfg) != LFS_ERR_OK)
    {
        printf("[REC] open %s failed\r\n", name);
        return -2;
    }

    /* 占位头：停止时回填真实长度 */
    memset(hdr, 0, sizeof(hdr));
    build_header(hdr, 0);
    if (lfs_file_write(s_lfs, &s_file, hdr, sizeof(hdr)) != (lfs_ssize_t)sizeof(hdr))
    {
        (void)lfs_file_close(s_lfs, &s_file);
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
    char name[16];

    if (!s_active)
        return 0;
    s_active = 0;

    data_bytes = s_samples * 2U;
    build_header(hdr, data_bytes);

    /* 回到文件头回填长度，close 提交 */
    if (lfs_file_seek(s_lfs, &s_file, 0, LFS_SEEK_SET) < 0 ||
        lfs_file_write(s_lfs, &s_file, hdr, sizeof(hdr)) != (lfs_ssize_t)sizeof(hdr))
    {
        (void)lfs_file_close(s_lfs, &s_file);
        printf("[REC] finalize failed (data may be incomplete)\r\n");
        return -1;
    }
    if (lfs_file_close(s_lfs, &s_file) != LFS_ERR_OK)
    {
        printf("[REC] close failed\r\n");
        return -2;
    }

    snprintf(name, sizeof(name), "REC%04lu.WAV", (unsigned long)s_seq);
    s_seq++;
    save_seq(s_seq);

    printf("[REC] saved %s: %lu.%02lus, %lu B\r\n", name,
           (unsigned long)(s_samples / WAV_REC_SAMPLE_RATE),
           (unsigned long)((s_samples % WAV_REC_SAMPLE_RATE) * 100U / WAV_REC_SAMPLE_RATE),
           (unsigned long)data_bytes);
    return (int)data_bytes;
}

void WavRecorder_feed(const int16_t *pcm, uint32_t n)
{
    if (!s_active || pcm == NULL || n == 0U)
        return;

    lfs_ssize_t w = lfs_file_write(s_lfs, &s_file, pcm,
                                   (lfs_size_t)n * 2U);
    if (w != (lfs_ssize_t)(n * 2U))
    {
        printf("[REC] write failed (%ld) -> stop\r\n", (long)w);
        (void)WavRecorder_stop();
        return;
    }
    s_samples += n;

    if (s_samples >= WAV_REC_MAX_SECONDS * WAV_REC_SAMPLE_RATE)
    {
        printf("[REC] max duration reached -> stop\r\n");
        (void)WavRecorder_stop();
    }
}
