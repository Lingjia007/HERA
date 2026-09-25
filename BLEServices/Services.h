/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Services/Services.h
  * @brief   BLE业务服务框架: 多服务注册/命令分发/TX Notify通道管理
  * @note    本文件夹不属于CubeMX生成体系, 代码重新生成不会覆盖本目录
  *          应用场景: 家居智联BLE低功耗 + 边缘AI语音识别, 业务服务按
  *          模块化扩展 (灯光/语音/场景...), 见文末"扩展指南"
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef SERVICES_H
#define SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ble.h"

/* ================================ 协议 ================================
 * myCharWrite(RX)支持两种控制模式, 由首字节自动识别:
 * 1. 二进制帧模式(程序/App用, 首字节为非可打印ASCII):
 *      Byte0  SVC_ID  : 目标服务ID, 见 SVC_ID_xxx
 *      Byte1  CMD     : 服务内命令码, 各服务头文件定义
 *      Byte2  LEN     : payload字节数
 *      Byte3~ payload : 数据, 可为空(LEN=0)
 *    回复帧(Notify)同格式: [SVC_ID][CMD][LEN][payload]
 * 2. 文本命令模式(调试用, 首字节为可打印ASCII):
 *      格式: "服务名 命令名 [参数]", 如 "led on", 大小写不敏感
 *    回复(Notify)为文本, 如 "led on ok"
 * 未知服务/命令: 二进制模式回错误帧[SVC_ID][0xFF][0], 文本模式回
 * "error: unknown service"/"error: unknown cmd"
 * ====================================================================== */

/* 服务ID与文本服务名分配表 (新增服务时在此追加) */
#define SVC_ID_LED    0x01u  /* 灯光控制, 见svc_led.h, 文本名"led" */
/* #define SVC_ID_VOICE  0x02u */ /* 预留: 语音识别结果/配置 */
/* #define SVC_ID_SCENE  0x03u */ /* 预留: 场景联动 */

/* 通用命令码 (二进制帧CMD字段) */
#define SVC_CMD_ERR   0xFFu  /* 未知服务ID/未知命令/参数错误 */

/* 单包payload上限: 受CFG_BLE_MAX_ATT_MTU(156)限制, 帧头3字节+数据 */
#define SVC_MAX_PAYLOAD_LEN   153u

/**
 * @brief  子服务注册结构: 每个业务服务(svc_xxx.c)实现一个常量实例
 */
typedef struct
{
  uint8_t SvcId;          /**< 服务ID, 见 SVC_ID_xxx */
  const char *Name;       /**< 文本命令模式的服务名, NULL=不支持文本命令 */
  void (*Init)(void);     /**< 初始化回调, 无需初始化则填NULL */
  void (*OnRx)(uint8_t Cmd, const uint8_t *pPayload, uint16_t Len); /**< 二进制帧命令处理 */
  void (*OnText)(const char *pCmd, const char *pArgs);              /**< 文本命令处理, pCmd小写无空格 */
} Ble_Svc_t;

/**
 * @brief  绑定GATT载体: 在mySVC服务上添加TX Notify特征(板→手机),
 *         并调用注册表内所有子服务的Init()
 * @note   由custom_stm.c的USER CODE区调用(传入服务句柄), 时序在
 *         aci_gatt_add_service成功之后
 */
void Services_BindGatt(uint16_t ServiceHandle);

/**
 * @brief  RX数据入口: 按帧头SVC_ID分发到子服务
 * @note   由custom_app.c的USER CODE区调用(myCharWrite写入事件)
 */
void Services_OnRxData(const uint8_t *pData, uint16_t Len);

/**
 * @brief  TX发送: 自动组帧[SvcId][Cmd][Len][payload]并Notify给手机
 * @note   未连接或手机未订阅CCCD时返回错误, 不阻塞
 */
tBleStatus Services_Send(uint8_t SvcId, uint8_t Cmd, const uint8_t *pPayload, uint16_t Len);

/**
 * @brief  TX发送文本(Notify): 不加帧头, 原样发送字符串
 */
tBleStatus Services_SendText(const char *pText);

/* ============================== 扩展指南 ==============================
 * 新增一个业务服务(例: 语音识别svc_voice)只需3步:
 *   1. 本头文件: 分配 SVC_ID_VOICE, 打开注释
 *   2. 新建 svc_voice.h/.c: 实现命令处理函数和 Ble_Svc_t 实例
 *   3. Services.c 的 m_SvcTable 注册表追加一行 &Svc_VOICE
 * 生成文件(custom_stm.c/custom_app.c)无需任何改动
 * ====================================================================== */

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_H */
