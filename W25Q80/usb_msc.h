/**
 ******************************************************************************
 * @file usb_msc.h
 * @brief USB MSC (U 盘) 生命周期与挂载状态管理。
 *
 * - 挂载检测：USBD configured 即 PC 已枚举 U 盘。
 * - 挂载期间固件端禁止写文件（互斥由业务层遵守，见 UsbMsc_Mounted）。
 * - 睡眠协作：进 STOP2 前断开 USB（USBD_Stop），唤醒后由 Poll 重启枚举。
 ******************************************************************************
 */
#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdint.h>

/* PC 已挂载 U 盘（USB configured）？挂载期间固件端不得写文件系统 */
uint8_t UsbMsc_Mounted(void);

/* 进 STOP2 前调用：断开 USB（PC 视为拔出），唤醒后由 UsbMsc_Poll 重启 */
void UsbMsc_NotifySleep(void);

/* 强制断开+重连（PC 重新枚举）。用于固件改写了盘内容（如开机 mkfs）
 * 后清掉 PC 端缓存的旧 BPB/容量。 */
void UsbMsc_Restart(void);

/* 只断开、不自动重连（录音独占 Flash 期间用；停止后调 Restart 重连）。 */
void UsbMsc_Disconnect(void);

/* 主循环调用：唤醒后重启 USB 枚举；检测挂载沿（自动停录音并提示） */
void UsbMsc_Poll(void);

#endif /* USB_MSC_H */
