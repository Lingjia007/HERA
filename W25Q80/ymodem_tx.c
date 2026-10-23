/**
 ******************************************************************************
 * @file ymodem_tx.c
 * @brief YMODEM(CRC) 发送端（设备 -> PC），1K 数据块，参考经典 ST IAP
 *        Ymodem_Transmit 实现，直接阻塞收发 huart1（115200）。
 *
 * 传输期间独占 USART1：调用方先 Shell_CLI_Pause() 挂起 CLI 的 RX 中断，
 * 结束后 Shell_CLI_Resume()。协议进行中禁止任何 printf（会污染字节流）。
 ******************************************************************************
 */
#include "ymodem_tx.h"
#include "shell_cli.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18

#define YM_BLK0_LEN 128U
#define YM_DATA_LEN 1024U
#define YM_PKT_LEN (YM_DATA_LEN + 5U)

static uint8_t s_pkt[YM_PKT_LEN]; /* STX + cnt + ~cnt + 1024 + crc16 */

static uint16_t crc16(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0U;
    uint32_t i;
    uint8_t b;

    while (len-- != 0U)
    {
        b = *buf++;
        crc ^= (uint16_t)((uint16_t)b << 8);
        for (i = 0U; i < 8U; i++)
        {
            crc = ((crc & 0x8000U) != 0U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                                          : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static int rx_byte(uint8_t *c, uint32_t timeout_ms)
{
    return (HAL_UART_Receive(&huart1, c, 1U, timeout_ms) == HAL_OK) ? 0 : -1;
}

static int tx_bytes(const uint8_t *b, uint32_t n)
{
    return (HAL_UART_Transmit(&huart1, (uint8_t *)b, n, 2000U) == HAL_OK) ? 0 : -1;
}

static void send_can(void)
{
    uint8_t can[2] = {CAN, CAN};
    (void)tx_bytes(can, 2U);
}

/* 组包并发送：soh 起始（128B）或 STX（1024B）*/
static int send_packet(uint8_t start, uint8_t num, const uint8_t *data, uint32_t len)
{
    uint16_t crc;

    s_pkt[0] = start;
    s_pkt[1] = num;
    s_pkt[2] = (uint8_t)(0xFFU - num);
    memcpy(s_pkt + 3, data, len);
    if (len < YM_DATA_LEN)
    {
        memset(s_pkt + 3 + len, 0x1A, YM_DATA_LEN - len); /* EOF 填充 */
    }
    crc = crc16(s_pkt + 3, YM_DATA_LEN);
    s_pkt[3 + YM_DATA_LEN] = (uint8_t)(crc >> 8);
    s_pkt[4 + YM_DATA_LEN] = (uint8_t)crc;

    return tx_bytes(s_pkt, YM_DATA_LEN + 5U);
}

/* 等待指定控制字符，忽略其他；返回 0 成功 */
static int wait_ctrl(uint8_t want, uint32_t timeout_ms, uint32_t retries)
{
    uint8_t c;

    while (retries-- != 0U)
    {
        if (rx_byte(&c, timeout_ms) == 0)
        {
            if (c == want)
            {
                return 0;
            }
            if (c == CAN)
            {
                uint8_t c2;
                if (rx_byte(&c2, 100U) == 0 && c2 == CAN)
                {
                    return -3; /* 接收端取消 */
                }
            }
        }
    }
    return -1; /* 超时 */
}

int YmodemTx_Send(lfs_t *lfs, const char *path)
{
    lfs_file_t f;
    struct lfs_file_config fcfg = {0};
    static uint8_t fcache[256];
    uint8_t blk0[YM_BLK0_LEN];
    uint8_t blkno = 1U;
    uint8_t hdr_buf[YM_DATA_LEN];
    lfs_ssize_t r;
    lfs_ssize_t total = 0;
    int err;
    uint32_t size;

    if (lfs == NULL || path == NULL)
    {
        return -4;
    }

    fcfg.buffer = fcache;
    if (lfs_file_opencfg(lfs, &f, path, LFS_O_RDONLY, &fcfg) != LFS_ERR_OK)
    {
        printf("[YM] open %s failed\r\n", path);
        return -2;
    }
    size = (uint32_t)lfs_file_size(lfs, &f);

    Shell_CLI_Pause();
    printf("[YM] sending %s (%lu B), waiting for receiver 'C'...\r\n",
           path, (unsigned long)size);

    /* 1. 等接收端 CRC 模式请求 */
    err = wait_ctrl('C', 1000U, 10U);
    if (err != 0)
    {
        printf("[YM] no receiver\r\n");
        goto exit_resume;
    }

    /* 2. 头块：文件名 + 长度 */
    memset(blk0, 0, sizeof(blk0));
    (void)snprintf((char *)blk0, YM_BLK0_LEN, "%s", path);
    (void)snprintf((char *)blk0 + strlen(path) + 1U,
                   YM_BLK0_LEN - strlen(path) - 1U, "%lu", (unsigned long)size);
    if (send_packet(SOH, 0U, blk0, YM_BLK0_LEN) != 0)
    {
        err = -5;
        goto exit_cancel;
    }

    err = wait_ctrl(ACK, 1000U, 10U);
    if (err != 0)
    {
        goto exit_cancel;
    }
    err = wait_ctrl('C', 1000U, 10U);
    if (err != 0)
    {
        goto exit_cancel;
    }

    /* 3. 数据块 1K */
    for (;;)
    {
        r = lfs_file_read(lfs, &f, hdr_buf, YM_DATA_LEN);
        if (r < 0)
        {
            err = -6;
            goto exit_cancel;
        }
        if (r == 0)
        {
            break;
        }
        if (send_packet(STX, blkno, hdr_buf, (uint32_t)r) != 0)
        {
            err = -5;
            goto exit_cancel;
        }
        err = wait_ctrl(ACK, 1000U, 10U);
        if (err != 0)
        {
            goto exit_cancel;
        }
        blkno++;
        total += r;
    }

    /* 4. EOT（接收端可能先 NAK：重发直到 ACK） */
    {
        uint8_t eot = EOT;
        uint32_t tries = 10U;
        uint8_t c;

        while (tries-- != 0U)
        {
            (void)tx_bytes(&eot, 1U);
            if (rx_byte(&c, 1000U) == 0)
            {
                if (c == ACK)
                {
                    break;
                }
                if (c == CAN)
                {
                    err = -3;
                    goto exit_resume;
                }
            }
        }
    }

    /* 5. 结束空头块 */
    err = wait_ctrl('C', 1000U, 10U);
    if (err != 0)
    {
        goto exit_cancel;
    }
    memset(blk0, 0, sizeof(blk0));
    (void)send_packet(SOH, 0U, blk0, YM_BLK0_LEN);
    (void)wait_ctrl(ACK, 1000U, 10U);

    printf("[YM] done: %ld B sent\r\n", (long)total);
    err = 0;
    goto exit_resume;

exit_cancel:
    send_can();
    printf("[YM] aborted (err=%d, %ld B)\r\n", err, (long)total);

exit_resume:
    (void)lfs_file_close(lfs, &f);
    Shell_CLI_Resume();
    return err;
}
