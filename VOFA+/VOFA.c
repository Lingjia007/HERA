/**
 ******************************************************************************
 * @file    VOFA.c
 * @brief   VOFA+ fdata protocol over USART1.
 *          Frame layout: VOFA_CH_COUNT floats + 4 tail bytes {00 00 80 7f}.
 ******************************************************************************
 */
#include "VOFA.h"
#include "usart.h"
#include <stdlib.h>
#include <string.h>

void Vofa_Init(void)
{
}

void Vofa_Data_deal(uint8_t *buf)
{
    uint8_t ch[4] = {0};
    uint8_t data[20] = {0};

    uint8_t ch_n = 0;

    memcpy(ch, buf, 2);
    memcpy(data, &buf[2], 20);

    ch_n = atoi((char *)ch);

    if (ch_n < VOFA_RECV_CH_COUNT)
    {
        VOFA_Recv_Handle.fdata[ch_n] = atof((char *)data);
    }
}

VOFA_Send_Handle_t VOFA_Handle = {
    .tail = {0x00, 0x00, 0x80, 0x7f},
};

VOFA_Recv_Handle_t VOFA_Recv_Handle;

void Vofa_Send(void)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&VOFA_Handle, sizeof(VOFA_Handle), HAL_MAX_DELAY);
}
