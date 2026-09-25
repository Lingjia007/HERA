/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Services/svc_led.h
  * @brief   LED灯光控制服务 (家居智联-灯光模块)
  * @note    命令表驱动, 见svc_led.c的m_LedCmdTable
  *          二进制模式: [0x01][CMD][0];  文本模式: "led on/off/toggle"
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef SVC_LED_H
#define SVC_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Services.h"

/* LED命令码 (二进制帧CMD字段) */
#define LED_CMD_OFF     0x00u  /* 熄灭, 文本名"off" */
#define LED_CMD_ON      0x01u  /* 点亮, 文本名"on" */
#define LED_CMD_TOGGLE  0x02u  /* 翻转, 文本名"toggle" */

/* LED回复: 二进制帧payload=1字节状态(0=灭 1=亮); 文本模式回"led <cmd> ok" */

/* LED子服务注册实例 (Services.c注册表引用) */
extern const Ble_Svc_t Svc_LED;

#ifdef __cplusplus
}
#endif

#endif /* SVC_LED_H */
