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

/* U 盘功能开关当前状态（默认 0：开机即 KWS 唤醒识别，不枚举 U 盘） */
uint8_t UsbMsc_Enabled(void);

/* U 盘功能开关：1 = 枚举挂载 U 盘；0 = 断开并禁用自动重连（CLI `usb on/off`） */
void UsbMsc_SetEnabled(uint8_t en);

/* 进 STOP2 前调用：断开 USB（PC 视为拔出），唤醒后由 UsbMsc_Poll 重启。
 * 仅在功能开启时有动作。 */
void UsbMsc_NotifySleep(void);

/* 断开+重连（PC 重新枚举）。受功能开关约束：关着时为空操作。
 * 用于固件改写了盘内容（如 rmall）后清掉 PC 端缓存的旧 BPB/容量。 */
void UsbMsc_Restart(void);

/* 只断开、不自动重连（录音独占 Flash 期间用；停止后调 Restart 重连）。 */
void UsbMsc_Disconnect(void);

/* 主循环调用：唤醒后重启 USB 枚举；检测挂载沿（自动停录音并提示） */
void UsbMsc_Poll(void);

#endif /* USB_MSC_H */
