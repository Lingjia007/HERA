/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Services/Services.c
  * @brief   BLE业务服务框架实现: 注册表分发 + TX Notify通道
  * @note    TX Notify特征由本模块在mySVC服务上动态注册, 不依赖
  *          CubeMX模板, 重新生成代码不影响本文件
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app_common.h"
#include "dbg_trace.h"
#include "ble.h"
#include "Services.h"
#include "svc_led.h"
#include <string.h>

/* Private define ------------------------------------------------------------*/
/* TX Notify特征UUID: 00000000-8e22-4541-9d4c-21edae82ed1a (与参考工程一致) */
#define COPY_UUID_128(uuid_struct, uuid_15, uuid_14, uuid_13, uuid_12, uuid_11, uuid_10, uuid_9, uuid_8, uuid_7, uuid_6, uuid_5, uuid_4, uuid_3, uuid_2, uuid_1, uuid_0) \
do {\
    uuid_struct[0] = uuid_0; uuid_struct[1] = uuid_1; uuid_struct[2] = uuid_2; uuid_struct[3] = uuid_3; \
    uuid_struct[4] = uuid_4; uuid_struct[5] = uuid_5; uuid_struct[6] = uuid_6; uuid_struct[7] = uuid_7; \
    uuid_struct[8] = uuid_8; uuid_struct[9] = uuid_9; uuid_struct[10] = uuid_10; uuid_struct[11] = uuid_11; \
    uuid_struct[12] = uuid_12; uuid_struct[13] = uuid_13; uuid_struct[14] = uuid_14; uuid_struct[15] = uuid_15; \
}while(0)

#define COPY_SVCTX_UUID(uuid_struct)   COPY_UUID_128(uuid_struct,0x00,0x00,0x00,0x00,0x8e,0x22,0x45,0x41,0x9d,0x4c,0x21,0xed,0xae,0x82,0xed,0x1a)

/* Private variables ---------------------------------------------------------*/
/**
 * 子服务注册表: 新增业务服务在此追加一行 (见Services.h扩展指南)
 */
static const Ble_Svc_t * const m_SvcTable[] =
{
  &Svc_LED,
};

#define SVC_NUM  (sizeof(m_SvcTable) / sizeof(m_SvcTable[0]))

typedef struct
{
  uint16_t SvcHandle;     /**< mySVC服务句柄 */
  uint16_t TxCharHandle;  /**< TX Notify特征句柄 */
} Services_Context_t;

static Services_Context_t m_Ctx;

/* extern custom_app.c的全局Notify缓冲(512B), 避免栈上大数组 */
extern uint8_t NotifyCharData[];

static void Services_OnTextData(const char *pText, uint16_t Len);

/* Functions Definition ------------------------------------------------------*/

void Services_BindGatt(uint16_t ServiceHandle)
{
  Char_UUID_t uuid;
  tBleStatus ret;
  uint32_t i;

  m_Ctx.SvcHandle = ServiceHandle;

  /* 子服务初始化 */
  for (i = 0; i < SVC_NUM; i++)
  {
    if (m_SvcTable[i]->Init != NULL)
    {
      m_SvcTable[i]->Init();
    }
  }

  /* mySVC服务上添加TX Notify特征(板→手机): 手机订阅CCCD后收到Notify */
  COPY_SVCTX_UUID(uuid.Char_UUID_128);
  ret = aci_gatt_add_char(ServiceHandle,
                          UUID_TYPE_128, &uuid,
                          SVC_MAX_PAYLOAD_LEN,
                          CHAR_PROP_READ | CHAR_PROP_NOTIFY,
                          ATTR_PERMISSION_NONE,
                          GATT_NOTIFY_ATTRIBUTE_WRITE,
                          0x10,
                          CHAR_VALUE_LEN_VARIABLE,
                          &(m_Ctx.TxCharHandle));
  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : Services TX char add, error code: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: Services TX char add, handle = 0x%04x \n\r", m_Ctx.TxCharHandle);
  }
}

void Services_OnRxData(const uint8_t *pData, uint16_t Len)
{
  const Ble_Svc_t *pSvc = NULL;
  uint8_t SvcId, Cmd, PayloadLen;
  const uint8_t *pPayload;
  uint32_t i;

  if ((pData == NULL) || (Len == 0u))
  {
    return;
  }

  /* 模式识别: 首字节为可打印ASCII → 文本命令(如"led on"), 否则二进制帧 */
  if ((pData[0] >= 0x21u) && (pData[0] <= 0x7Eu))
  {
    Services_OnTextData((const char *)pData, Len);
    return;
  }

  if (Len < 3u)
  {
    return; /* 二进制帧不完整, 丢弃 */
  }

  SvcId      = pData[0];
  Cmd        = pData[1];
  PayloadLen = pData[2];

  /* LEN字段与实际数据长度校验 */
  if (Len < (uint16_t)(3u + PayloadLen))
  {
    return;
  }
  pPayload = (PayloadLen > 0u) ? &pData[3] : NULL;

  /* 按SVC_ID查注册表 */
  for (i = 0; i < SVC_NUM; i++)
  {
    if (m_SvcTable[i]->SvcId == SvcId)
    {
      pSvc = m_SvcTable[i];
      break;
    }
  }

  if (pSvc == NULL)
  {
    /* 未知服务ID: 回错误帧 */
    (void)Services_Send(SvcId, SVC_CMD_ERR, NULL, 0);
    return;
  }

  pSvc->OnRx(Cmd, pPayload, PayloadLen);
}

tBleStatus Services_Send(uint8_t SvcId, uint8_t Cmd, const uint8_t *pPayload, uint16_t Len)
{
  tBleStatus ret;

  if (m_Ctx.TxCharHandle == 0u)
  {
    return BLE_STATUS_INVALID_PARAMS; /* TX通道未就绪 */
  }

  if (Len > SVC_MAX_PAYLOAD_LEN)
  {
    Len = SVC_MAX_PAYLOAD_LEN;
  }

  /* 组帧: [SvcId][Cmd][Len][payload] */
  NotifyCharData[0] = SvcId;
  NotifyCharData[1] = Cmd;
  NotifyCharData[2] = (uint8_t)Len;
  if ((pPayload != NULL) && (Len > 0u))
  {
    memcpy(&NotifyCharData[3], pPayload, Len);
  }

  ret = aci_gatt_update_char_value(m_Ctx.SvcHandle,
                                   m_Ctx.TxCharHandle,
                                   0,                    /* charValOffset */
                                   (uint8_t)(Len + 3u),  /* charValueLen  */
                                   NotifyCharData);
  if (ret != BLE_STATUS_SUCCESS)
  {
    /* 未连接/未订阅CCCD时返回错误属正常现象 */
    APP_DBG_MSG("  Fail   : Services_Notify 0x%02x (未连接或未订阅时正常) \n\r", ret);
  }
  return ret;
}

tBleStatus Services_SendText(const char *pText)
{
  uint16_t len;

  if ((pText == NULL) || (m_Ctx.TxCharHandle == 0u))
  {
    return BLE_STATUS_INVALID_PARAMS;
  }

  len = strlen(pText);
  if (len > SVC_MAX_PAYLOAD_LEN)
  {
    len = SVC_MAX_PAYLOAD_LEN;
  }

  return aci_gatt_update_char_value(m_Ctx.SvcHandle,
                                    m_Ctx.TxCharHandle,
                                    0, (uint8_t)len, (const uint8_t *)pText);
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  文本命令处理: 解析"服务名 命令名 [参数]", 小写化后查注册表分发
 * @note   文本来自myCharWrite, 最长受SizeMycharwrite(20)限制, 缓冲24字节足够
 */
static void Services_OnTextData(const char *pText, uint16_t Len)
{
  const Ble_Svc_t *pSvc = NULL;
  char buf[24];
  char *pArgs = NULL;
  uint16_t i, nameLen;

  if (Len > (sizeof(buf) - 1u))
  {
    Len = sizeof(buf) - 1u;
  }

  /* 小写化 + '\0'结尾 */
  for (i = 0; i < Len; i++)
  {
    char c = pText[i];
    buf[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
  }
  buf[Len] = '\0';

  /* 按第一个空格拆分: 服务名 / 参数 */
  nameLen = 0;
  while ((buf[nameLen] != '\0') && (buf[nameLen] != ' '))
  {
    nameLen++;
  }
  if (buf[nameLen] == ' ')
  {
    buf[nameLen] = '\0';
    pArgs = &buf[nameLen + 1u];
  }

  /* 按服务名查注册表 */
  for (i = 0; i < SVC_NUM; i++)
  {
    if ((m_SvcTable[i]->Name != NULL) && (strcmp(m_SvcTable[i]->Name, buf) == 0))
    {
      pSvc = m_SvcTable[i];
      break;
    }
  }

  if ((pSvc == NULL) || (pSvc->OnText == NULL))
  {
    (void)Services_SendText("error: unknown service");
    return;
  }

  pSvc->OnText((pArgs != NULL) ? pArgs : "", pArgs);
}
