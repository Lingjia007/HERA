/**
 ******************************************************************************
 * @file usb_msc.c
 * @brief USB MSC 生命周期管理（详见 usb_msc.h）。
 ******************************************************************************
 */
#include "usb_msc.h"
#include "usb_device.h"
#include "usbd_core.h"
#include "wav_recorder.h"
#include <stdio.h>

static volatile uint8_t s_need_start; /* STOP2 唤醒后需重启 USB 枚举 */
static uint8_t s_prev_mounted;
static uint8_t s_prev_state;  /* dev_state 诊断 */
static uint8_t s_stopped;     /* 固件主动断开中（Restart 后、Start 前） */
static uint8_t s_enabled;     /* U 盘功能开关（默认关：开机即 KWS 唤醒识别，
                               * CLI `usb on` 后才允许枚举/自动重连） */

extern USBD_HandleTypeDef hUsbDeviceFS; /* usb_device.c */

uint8_t UsbMsc_Mounted(void)
{
    return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && s_stopped == 0U)
               ? 1U
               : 0U;
}

void UsbMsc_NotifySleep(void)
{
    /* 断开 DP 上拉：PC 视为拔出；STOP2 后 USB IP 无时钟，必须先断开。
     * 功能关着时从未拉过上拉，无需动作。 */
    if (s_enabled != 0U)
    {
        UsbMsc_Restart();
    }
}

uint8_t UsbMsc_Enabled(void)
{
    return s_enabled;
}

void UsbMsc_SetEnabled(uint8_t en)
{
    if (en != 0U)
    {
        if (s_enabled == 0U)
        {
            s_enabled = 1U;
            UsbMsc_Restart(); /* 主循环 Poll 里 USBD_Start -> PC 枚举挂载 */
        }
    }
    else
    {
        s_enabled = 0U;
        s_need_start = 0U;
        UsbMsc_Disconnect(); /* PC 视为拔出，回到纯 KWS 模式 */
    }
}

void UsbMsc_Restart(void)
{
    if (s_enabled == 0U)
    {
        return; /* 用户关掉 U 盘功能后，录音停/rmall 不自动重新枚举 */
    }
    (void)USBD_Stop(&hUsbDeviceFS);
    s_stopped = 1U;
    s_need_start = 1U;
}

void UsbMsc_Disconnect(void)
{
    (void)USBD_Stop(&hUsbDeviceFS);
    s_stopped = 1U;
    /* 不置 s_need_start：录音独占期间保持断开，停止后由 Restart 重连 */
}

void UsbMsc_Poll(void)
{
    uint8_t mounted;
    uint8_t st;

    if (s_need_start != 0U)
    {
        s_need_start = 0;
        s_stopped = 0;
        s_prev_mounted = 0;
        (void)USBD_Start(&hUsbDeviceFS); /* 重新拉 DP 上拉 -> PC 重新枚举 */
    }

    /* 枚举过程诊断：1=DEFAULT 2=ADDRESSED 3=CONFIGURED 4=SUSPENDED */
    st = (uint8_t)hUsbDeviceFS.dev_state;
    if (st != s_prev_state)
    {
        printf("[USB] state %d -> %d\r\n", (int)s_prev_state, (int)st);
        s_prev_state = st;
    }

    mounted = UsbMsc_Mounted();
    if (mounted != 0U && s_prev_mounted == 0U)
    {
        /* 挂载沿：录音中则强制停止（挂载期间固件端不得写 FS） */
        if (WavRecorder_active())
        {
            printf("[USB] mounted -> stop recording (readonly for MCU)\r\n");
            (void)WavRecorder_stop();
        }
        else
        {
            printf("[USB] mounted\r\n");
        }
    }
    s_prev_mounted = mounted;
}
