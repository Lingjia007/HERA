/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Services/svc_led.c
  * @brief   LED灯光控制服务实现 (命令表驱动)
  * @note    板载LED: PC13, 低电平点亮 (引脚宏LED_Pin/LED_GPIO_Port在main.h,
  *          由CubeMX配置GPIO, 本模块只做电平控制)
  *          新增命令: 在m_LedCmdTable加一行即可, 无需改分发逻辑
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dbg_trace.h"
#include "Services.h"
#include "svc_led.h"
#include <string.h>

/* Private typedef -----------------------------------------------------------*/
/**
 * LED命令表项: 二进制命令码 + 文本命令名 + 执行函数
 */
typedef struct
{
  uint8_t      Cmd;    /* 二进制命令码(帧CMD字段) */
  const char  *Name;   /* 文本命令名(小写) */
  void       (*Exec)(void);
} Led_Cmd_t;

/* Private function prototypes -----------------------------------------------*/
static void Led_On(void);
static void Led_Off(void);
static void Led_Toggle(void);

/* Private constants ---------------------------------------------------------*/
/**
 * LED命令表 (当前: on/off/toggle, 新增命令在此追加一行)
 */
static const Led_Cmd_t m_LedCmdTable[] =
{
  { LED_CMD_ON,     "on",     Led_On     },
  { LED_CMD_OFF,    "off",    Led_Off    },
  { LED_CMD_TOGGLE, "toggle", Led_Toggle },
};

#define LED_CMD_NUM  (sizeof(m_LedCmdTable) / sizeof(m_LedCmdTable[0]))

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  读取LED当前状态
 * @retval 1=亮 0=灭
 */
static uint8_t Led_GetState(void)
{
  return (HAL_GPIO_ReadPin(LED_GPIO_Port, LED_Pin) == GPIO_PIN_RESET) ? 1u : 0u;
}

/**
 * @brief  设置LED状态
 * @param  On: 1=亮 0=灭
 */
static void Led_Apply(uint8_t On)
{
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
                    (On != 0u) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void Led_On(void)
{
  Led_Apply(1u);
}

static void Led_Off(void)
{
  Led_Apply(0u);
}

static void Led_Toggle(void)
{
  Led_Apply(Led_GetState() ? 0u : 1u);
}

/**
 * @brief  按二进制命令码查命令表
 */
static const Led_Cmd_t *Led_FindByCode(uint8_t Cmd)
{
  uint32_t i;
  for (i = 0; i < LED_CMD_NUM; i++)
  {
    if (m_LedCmdTable[i].Cmd == Cmd)
    {
      return &m_LedCmdTable[i];
    }
  }
  return NULL;
}

/**
 * @brief  按文本命令名查命令表(忽略大小写)
 */
static const Led_Cmd_t *Led_FindByName(const char *pName)
{
  uint32_t i;
  const char *p, *q;
  for (i = 0; i < LED_CMD_NUM; i++)
  {
    p = pName;
    q = m_LedCmdTable[i].Name;
    while ((*p != '\0') && (*q != '\0'))
    {
      char c = (*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p;
      if (c != *q)
      {
        break;
      }
      p++;
      q++;
    }
    if ((*p == '\0') && (*q == '\0'))
    {
      return &m_LedCmdTable[i];
    }
  }
  return NULL;
}

/**
 * @brief  执行命令并回复状态(两种模式共用)
 */
static void Led_ExecAndReply(const Led_Cmd_t *pCmd, uint8_t IsTextMode)
{
  uint8_t state;

  pCmd->Exec();
  state = Led_GetState();
  APP_DBG_MSG("Services: LED %s \n\r", (state != 0u) ? "ON" : "OFF");

  if (IsTextMode)
  {
    /* 文本回复: "led <name> ok" */
    char resp[24];
    uint16_t pos = 0, nlen = strlen(pCmd->Name);

    memcpy(&resp[pos], "led ", 4u);
    pos += 4u;
    memcpy(&resp[pos], pCmd->Name, nlen);
    pos += nlen;
    memcpy(&resp[pos], " ok", 3u);
    pos += 3u;
    resp[pos] = '\0';
    (void)Services_SendText(resp);
  }
  else
  {
    /* 二进制回复: [LED][Cmd][1][state] */
    (void)Services_Send(SVC_ID_LED, pCmd->Cmd, &state, 1u);
  }
}

/* 子服务接口 ----------------------------------------------------------------*/

/**
 * @brief  二进制帧命令入口: [0x01][Cmd][Len]
 */
static void SvcLed_OnRx(uint8_t Cmd, const uint8_t *pPayload, uint16_t Len)
{
  const Led_Cmd_t *pCmd = Led_FindByCode(Cmd);

  (void)pPayload; /* LED命令无参数 */
  (void)Len;

  if (pCmd == NULL)
  {
    (void)Services_Send(SVC_ID_LED, SVC_CMD_ERR, NULL, 0);
    return;
  }
  Led_ExecAndReply(pCmd, 0u);
}

/**
 * @brief  文本命令入口: "led <name>"→本函数收到"<name>"
 */
static void SvcLed_OnText(const char *pCmd, const char *pArgs)
{
  const Led_Cmd_t *p = Led_FindByName(pCmd);

  (void)pArgs; /* LED命令无参数 */

  if (p == NULL)
  {
    (void)Services_SendText("error: unknown cmd");
    return;
  }
  Led_ExecAndReply(p, 1u);
}

/* 注册实例 ------------------------------------------------------------------*/
const Ble_Svc_t Svc_LED =
{
  .SvcId  = SVC_ID_LED,
  .Name   = "led",
  .Init   = NULL,  /* GPIO已由MX_GPIO_Init完成配置 */
  .OnRx   = SvcLed_OnRx,
  .OnText = SvcLed_OnText,
};
