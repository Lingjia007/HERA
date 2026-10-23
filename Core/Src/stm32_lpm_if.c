/* USER CODE BEGIN Header */
/**
  ***************************************************************************************
  * @file    stm32_lpm_if.c
  * @author  MCD Application Team
  * @brief   Low layer function to enter/exit low power modes (stop, sleep).
  ***************************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "stm32_lpm_if.h"
#include "stm32_lpm.h"
#include "app_conf.h"
/* USER CODE BEGIN include */
/* Application clock tree restore after a low-power wake (see
 * RestoreClocks / ExitLowPower). */
extern void SystemClock_Config(void);
extern void PeriphCommonClock_Config(void);
#include "usart.h"

static void Console_DrainTx(void); /* drain USART1 TX before clock switch */
static void RestoreClocks(void);    /* back to HSE -> PLL after a wake      */
/* USER CODE END include */

/* Exported variables --------------------------------------------------------*/
const struct UTIL_LPM_Driver_s UTIL_PowerDriver =
{
  PWR_EnterSleepMode,
  PWR_ExitSleepMode,

  PWR_EnterStopMode,
  PWR_ExitStopMode,

  PWR_EnterOffMode,
  PWR_ExitOffMode,
};

/* Private function prototypes -----------------------------------------------*/
static void Switch_On_HSI(void);
static void EnterLowPower(void);
static void ExitLowPower(void);
/* USER CODE BEGIN Private_Function_Prototypes */

/* USER CODE END Private_Function_Prototypes */
/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN Private_Typedef */

/* USER CODE END Private_Typedef */
/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Private_Define */

/* USER CODE END Private_Define */
/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Private_Macro */

/* USER CODE END Private_Macro */
/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Private_Variables */

/* USER CODE END Private_Variables */

/* Functions Definition ------------------------------------------------------*/
/**
  * @brief Enters Low Power Off Mode
  * @param none
  * @retval none
  */
void PWR_EnterOffMode(void)
{
/* USER CODE BEGIN PWR_EnterOffMode_1 */
  Console_DrainTx();
/* USER CODE END PWR_EnterOffMode_1 */
  /**
   * The systick should be disabled for the same reason than when the device enters stop mode because
   * at this time, the device may enter either OffMode or StopMode.
   */
  HAL_SuspendTick();

  EnterLowPower();

  /************************************************************************************
   * ENTER OFF MODE
   ***********************************************************************************/
  /*
   * There is no risk to clear all the WUF here because in the current implementation, this API is called
   * in critical section. If an interrupt occurs while in that critical section before that point,
   * the flag is set and will be cleared here but the system will not enter Off Mode
   * because an interrupt is pending in the NVIC. The ISR will be executed when moving out
   * of this critical section
   */
  LL_PWR_ClearFlag_WU();

  LL_PWR_SetPowerMode(LL_PWR_MODE_STANDBY);

  LL_LPM_EnableDeepSleep(); /**< Set SLEEPDEEP bit of Cortex System Control Register */

  /**
   * This option is used to ensure that store operations are completed
   */
#if defined (__CC_ARM) || defined (__ARMCC_VERSION)
  __force_stores();
#endif

  __WFI();

/* USER CODE BEGIN PWR_EnterOffMode_2 */

/* USER CODE END PWR_EnterOffMode_2 */
  return;
}

/**
  * @brief Exits Low Power Off Mode
  * @param none
  * @retval none
  */
void PWR_ExitOffMode(void)
{
/* USER CODE BEGIN PWR_ExitOffMode_1 */
  /* The OFF entry path also switches the system clock to HSI
   * (EnterLowPower), and this exit path never restores it: without this,
   * a blocked standby attempt (CPU2 still running) would leave the console
   * and timing at 16 MHz until the next full STOP2 cycle. */
  RestoreClocks();
/* USER CODE END PWR_ExitOffMode_1 */
  HAL_ResumeTick();
/* USER CODE BEGIN PWR_ExitOffMode_2 */

/* USER CODE END PWR_ExitOffMode_2 */
  return;
}

/**
  * @brief Enters Low Power Stop Mode
  * @note ARM exists the function when waking up
  * @param none
  * @retval none
  */
void PWR_EnterStopMode(void)
{
/* USER CODE BEGIN PWR_EnterStopMode_1 */
  /* Finish any in-flight console byte BEFORE EnterLowPower() switches the
   * system clock to HSI: the tail of a printf would otherwise be shifted
   * out at the wrong baud and garble the console. */
  Console_DrainTx();
/* USER CODE END PWR_EnterStopMode_1 */
  /**
   * When HAL_DBGMCU_EnableDBGStopMode() is called to keep the debugger active in Stop Mode,
   * the systick shall be disabled otherwise the cpu may crash when moving out from stop mode
   *
   * When in production, the HAL_DBGMCU_EnableDBGStopMode() is not called so that the device can reach best power consumption
   * However, the systick should be disabled anyway to avoid the case when it is about to expire at the same time the device enters
   * stop mode (this will abort the Stop Mode entry).
   */
  HAL_SuspendTick();

  /**
   * This function is called from CRITICAL SECTION
   */
  EnterLowPower();

  /************************************************************************************
   * ENTER STOP MODE
   ***********************************************************************************/
  LL_PWR_SetPowerMode(LL_PWR_MODE_STOP2);

  LL_LPM_EnableDeepSleep(); /**< Set SLEEPDEEP bit of Cortex System Control Register */

  /**
   * This option is used to ensure that store operations are completed
   */
#if defined (__CC_ARM) || defined (__ARMCC_VERSION)
  __force_stores();
#endif

  __WFI();

/* USER CODE BEGIN PWR_EnterStopMode_2 */

/* USER CODE END PWR_EnterStopMode_2 */
  return;
}

/**
  * @brief Exits Low Power Stop Mode
  * @note Enable the pll at 32MHz
  * @param none
  * @retval none
  */
void PWR_ExitStopMode(void)
{
/* USER CODE BEGIN PWR_ExitStopMode_1 */

/* USER CODE END PWR_ExitStopMode_1 */
  /**
   * This function is called from CRITICAL SECTION
   */
  ExitLowPower();

  HAL_ResumeTick();
/* USER CODE BEGIN PWR_ExitStopMode_2 */

/* USER CODE END PWR_ExitStopMode_2 */
  return;
}

/**
  * @brief Enters Low Power Sleep Mode
  * @note ARM exits the function when waking up
  * @param none
  * @retval none
  */
void PWR_EnterSleepMode(void)
{
/* USER CODE BEGIN PWR_EnterSleepMode_1 */

/* USER CODE END PWR_EnterSleepMode_1 */

  HAL_SuspendTick();

  /************************************************************************************
   * ENTER SLEEP MODE
   ***********************************************************************************/
  LL_LPM_EnableSleep(); /**< Clear SLEEPDEEP bit of Cortex System Control Register */

  /**
   * This option is used to ensure that store operations are completed
   */
#if defined (__CC_ARM) || defined (__ARMCC_VERSION)
  __force_stores();
#endif

  __WFI();
/* USER CODE BEGIN PWR_EnterSleepMode_2 */

/* USER CODE END PWR_EnterSleepMode_2 */
  return;
}

/**
  * @brief Exits Low Power Sleep Mode
  * @note ARM exits the function when waking up
  * @param none
  * @retval none
  */
void PWR_ExitSleepMode(void)
{
/* USER CODE BEGIN PWR_ExitSleepMode_1 */

/* USER CODE END PWR_ExitSleepMode_1 */
  HAL_ResumeTick();
/* USER CODE BEGIN PWR_ExitSleepMode_2 */

/* USER CODE END PWR_ExitSleepMode_2 */
  return;
}

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
/**
  * @brief Setup the system to enter either stop or off mode
  * @param none
  * @retval none
  */
static void EnterLowPower(void)
{
  /**
   * This function is called from CRITICAL SECTION
   */

  while(LL_HSEM_1StepLock(HSEM, CFG_HW_RCC_SEMID));

  if (! LL_HSEM_1StepLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID))
  {
    if(LL_PWR_IsActiveFlag_C2DS() || LL_PWR_IsActiveFlag_C2SB())
    {
      /* Release ENTRY_STOP_MODE semaphore */
      LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);

      Switch_On_HSI();
      __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_0);
    }
  }
  else
  {
    Switch_On_HSI();
    __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_0);
  }

  /* Release RCC semaphore */
  LL_HSEM_ReleaseLock(HSEM, CFG_HW_RCC_SEMID, 0);

  return;
}

/**
  * @brief Restore the system to exit stop mode
  * @param none
  * @retval none
  */
static void ExitLowPower(void)
{
  /* Release ENTRY_STOP_MODE semaphore */
  LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);

  while(LL_HSEM_1StepLock(HSEM, CFG_HW_RCC_SEMID));

  if(LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSI)
  {
/* Restore the clock configuration of the application in this user section */
/* USER CODE BEGIN ExitLowPower_1 */
  /* STOP2 wake: the system is back on HSI (EnterLowPower switched to it
   * before the stop) and the main PLL is down - restore the application
   * tree (HSE -> PLL, 64 MHz) or the CPU timing and the USART1 baud are
   * off. PeriphCommonClock_Config is also needed: Switch_On_HSI moved the
   * SMPS to HSI and the SMPS/RFWKP sources must go back to HSE.
   * Safe: the RCC semaphore is held while this section runs. */
  SystemClock_Config();
  PeriphCommonClock_Config();
/* USER CODE END ExitLowPower_1 */
  }
  else
  {
/* If the application is not running on HSE restore the clock configuration in this user section */
/* USER CODE BEGIN ExitLowPower_2 */

/* USER CODE END ExitLowPower_2 */
  }

  /* Release RCC semaphore */
  LL_HSEM_ReleaseLock(HSEM, CFG_HW_RCC_SEMID, 0);

  return;
}

/**
  * @brief Switch the system clock on HSI
  * @param none
  * @retval none
  */
static void Switch_On_HSI(void)
{
  LL_RCC_HSI_Enable();
  while(!LL_RCC_HSI_IsReady());
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
  LL_RCC_SetSMPSClockSource(LL_RCC_SMPS_CLKSOURCE_HSI);
  while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI);
  return;
}

/* USER CODE BEGIN Private_Functions */

/* Wait for any in-flight USART1 frame to complete. The low-power entry
 * paths switch the system clock to HSI (EnterLowPower), and a byte still
 * sitting in the TX shift register would be clocked out at the wrong baud
 * and garble the console. */
static void Console_DrainTx(void)
{
  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) == RESET)
  {
  }
  if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET)
  {
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET)
    {
    }
  }
}

/* Restore the application clock tree (HSE -> PLL 64 MHz, SMPS + RFWKP) when
 * a low-power exit left the system running on HSI. Locks the RCC semaphore
 * for CPU2 arbitration, so it must NOT be called while the semaphore is
 * already held (ExitLowPower calls SystemClock_Config directly instead). */
static void RestoreClocks(void)
{
  if (LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSI)
  {
    while (LL_HSEM_1StepLock(HSEM, CFG_HW_RCC_SEMID))
    {
    }
    SystemClock_Config();
    PeriphCommonClock_Config();
    LL_HSEM_ReleaseLock(HSEM, CFG_HW_RCC_SEMID, 0);
  }
}

/* USER CODE END Private_Functions */

