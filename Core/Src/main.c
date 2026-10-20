/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
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
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "ipcc.h"
#include "rf.h"
#include "rtc.h"
#include "sai.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "audio_app.h"
#include "vm3011.h"
#include "stm32_lpm_if.h" /* PWR_EnterStopMode/PWR_ExitStopMode (same STOP2 sequence as BLE-TEST) */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ZPL_LP_ENTER_DELAY_MS 3000U /* park in STOP2 this long after boot (ms) */
#define ZPL_LP_REARM_DELAY_MS 3000U /* re-enter STOP2 this long after a ZPL wake (ms) */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static VM3011_Dev_t s_vm;
static volatile uint8_t s_zpl_evt; /* 1 = ZPL (PA0) edge pending; also the STOP2 wake flag */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* RTC time converted to real milliseconds. The RTC (LSE) keeps running in
 * STOP2, unlike HAL_GetTick whose systick is suspended -> use it to time the
 * park cycle.
 * CAUTION: the WPAN timer server configures a non-1Hz calendar
 * (app_conf.h: ASYNCH=0x0F, SYNCH=0x7FFF, "does not support 1Hz calendar"):
 * 1 calendar "second" = (16*32768)/32768 = 16 real seconds. Scale the raw
 * reading by that factor to get REAL ms. */
static uint32_t LP_rtc_ms(void)
{
  RTC_TimeTypeDef t = {0};
  RTC_DateTypeDef d = {0};
  uint32_t pseudo_ms;
  const uint32_t sec_per_cal_sec =
      ((CFG_RTC_ASYNCH_PRESCALER + 1UL) * (CFG_RTC_SYNCH_PRESCALER + 1UL)) / 32768UL;

  /* GetTime must be followed by GetDate to unlock the shadow registers */
  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

  pseudo_ms = (uint32_t)t.Hours * 3600000UL + (uint32_t)t.Minutes * 60000UL +
              (uint32_t)t.Seconds * 1000UL +
              ((uint32_t)(t.SecondFraction - t.SubSeconds) * 1000UL) / (t.SecondFraction + 1UL);
  return pseudo_ms * sec_per_cal_sec;
}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
  /* Config code for STM32_WPAN (HSE Tuning must be done before system clock configuration) */
  MX_APPE_Config();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* IPCC initialisation */
  MX_IPCC_Init();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_RTC_Init();
  MX_SAI1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_RF_Init();
  /* USER CODE BEGIN 2 */
  AudioApp_Init();

  /* VM3011 bring-up via driver (Zephyr port): waits 1 s for ZPL settle,
   * verifies I2C_CNTRL==0x00 (device detect) and applies the config. */
  {
    VM3011_Config_t cfg;
    VM3011_ConfigDefault(&cfg);
    cfg.wos_dout_thresh = 7U; /* 8x margin (18 dBSPL): max, reject taps */
    int ret = VM3011_Init(&s_vm, &hi2c1, VM3011_I2C_ADDR_L, &cfg);
    printf("[VM3011] init ret=%d (0=OK, -1=I2C, -3=NODEV)\r\n", ret);
    if (ret == VM3011_OK)
    {
      uint8_t v[6];
      uint8_t reg_ok = 1U;
      uint8_t i;
      for (i = 0U; i <= 5U; i++)
      {
        if (VM3011_ReadReg(&s_vm, i, &v[i]) != VM3011_OK)
        {
          reg_ok = 0U;
        }
      }
      if (reg_ok != 0U)
      {
        static const char *lpf_tab[4] = {"2k", "4k", "6k", "8k"};
        static const char *hpf_tab[4] = {"200", "300", "400", "800"};
        static const uint16_t margin_db10[8] = {0U, 60U, 95U, 120U, 140U, 155U, 169U, 180U};
        uint8_t pga = v[1] & 0x1FU;
        int amb_x10 = 915 - (int)pga * 15;
        uint8_t t = v[5] & 0x07U;

        printf("[VM3011] raw: 00=%02X 01=%02X 02=%02X 03=%02X 04=%02X 05=%02X\r\n",
               v[0], v[1], v[2], v[3], v[4], v[5]);
        printf("[VM3011] wdt: %s, delay=%u ms\r\n",
               ((v[0] & 1U) != 0U) ? "ON" : "OFF",
               (unsigned)(8U << ((v[0] >> 1) & 3U)));
        printf("[VM3011] bpf: LPF=%s Hz, HPF=%s Hz\r\n",
               lpf_tab[v[2] & 3U], hpf_tab[(v[2] >> 2) & 3U]);
        printf("[VM3011] pga: gain=%u (ambient ~%d.%d dB SPL)\r\n",
               pga, amb_x10 / 10, amb_x10 % 10);
        printf("[VM3011] track: fast_mode=%u, min_thr=code%u(~%d dB), max_thr=code%u(~%d dB), rms=%s Hz\r\n",
               (v[3] >> 5) & 3U, v[3] & 0x1FU,
               (915 - (int)(v[3] & 0x1FU) * 15) / 10,
               v[4] & 0x1FU,
               (915 - (int)(v[4] & 0x1FU) * 15) / 10,
               (((v[4] >> 5) & 1U) != 0U) ? "2" : "1");
        printf("[VM3011] dout: margin=%ux (+%u.%u dB SPL over ambient)\r\n",
               t + 1U, margin_db10[t] / 10U, margin_db10[t] % 10U);
      }
    }
  }

  /* VM3011 DOUT (PA0) EXTI: ZPL FLAG monitor */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
  printf("[VM3011] DOUT(PA0) init %s\r\n",
         (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) != GPIO_PIN_RESET) ? "HIGH" : "LOW");
  /* USER CODE END 2 */

  /* Init code for STM32_WPAN */
  MX_APPE_Init();

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    MX_APPE_Process();

    /* USER CODE BEGIN 3 */
    AudioApp_Process();

    /* VM3011: periodic health readout via driver every 500 ms
     * (PGA gain code + derived ambient dB SPL + DOUT level) */
    {
      static uint32_t s_last_read = 0U;
      if ((HAL_GetTick() - s_last_read) >= 500U)
      {
        uint8_t pga = 0;
        s_last_read = HAL_GetTick();
        if (VM3011_PgaGainGet(&s_vm, &pga) == VM3011_OK)
        {
          int db_x10 = (int)((91.5f - (float)pga * 1.5f) * 10.0f + 0.5f);
          GPIO_PinState dout = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
          int cleared = 0;
          if (dout != GPIO_PIN_RESET)
          {
            /* DOUT latches high on detection: clear it via I2C_CNTRL bit4 */
            cleared = (VM3011_ClearDout(&s_vm) == VM3011_OK) ? 1 : 0;
          }
          printf("[VM3011] pga=%2u ~%d.%d dB dout=%d%s\r\n",
                 pga, db_x10 / 10, db_x10 % 10, (int)dout,
                 (cleared != 0) ? " [cleared]" : "");
        }
        else
        {
          printf("[VM3011] I2C lost\r\n");
        }
      }
    }

    /* ZPL (PA0) edge event: flagged by EXTI callback, printed from main context
     * (no printf in ISR: right after STOP2 wake the ISR runs on HSI -> wrong baud).
     * While parked in STOP2 below, the same flag is the wake trigger. */
    if (s_zpl_evt != 0U)
    {
      s_zpl_evt = 0U;
      printf("[ZPL] DOUT(PA0) %s\r\n",
             (HAL_GPIO_ReadPin(ZPL_GPIO_Port, ZPL_Pin) != GPIO_PIN_RESET) ? "HIGH" : "LOW");
    }

    /* ---- STOP2 park: ZPL pin (PA0 rising edge) wakes the MCU ---- */
    {
      static uint8_t s_lp_armed = 0U;
      static uint32_t s_lp_next = 0U;
      static uint32_t s_lp_cycle = 0U;     /* park/wake cycle counter */
      static uint32_t s_lp_wake_tick = 0U; /* tick of boot / last ZPL wake */
      static uint32_t s_lp_rtc_boot = 0U;  /* real time (RTC) at boot */

      if (s_lp_armed == 0U)
      {
        s_lp_armed = 1U;
        s_lp_wake_tick = HAL_GetTick();
        s_lp_rtc_boot = LP_rtc_ms();
        s_lp_next = HAL_GetTick() + ZPL_LP_ENTER_DELAY_MS;
      }
      else if ((int32_t)(HAL_GetTick() - s_lp_next) >= 0)
      {
        uint32_t tick_park = HAL_GetTick();
        uint32_t rtc_park = LP_rtc_ms();

        s_lp_cycle++;
        printf("[LP] #%lu -> STOP2, awake %lums, up %lums\r\n",
               (unsigned long)s_lp_cycle,
               (unsigned long)(tick_park - s_lp_wake_tick),
               (unsigned long)(rtc_park - s_lp_rtc_boot));
        /* printf is blocking (HAL_UART_Transmit) -> TX is complete here.
         * Clear the DOUT latch so PA0 is LOW: the next detection produces
         * the rising edge that wakes the MCU. */
        (void)VM3011_ClearDout(&s_vm);

        /* PWR_EnterStopMode(): same sequence as BLE-TEST stm32_lpm_if.c
         * (HSEM handshake with CPU2, switch sysclk to HSI, STOP2, WFI).
         * Re-park when woken by another source (CPU2/IPCC/RTC): only ZPL
         * (EXTI flag or latched-high DOUT) leaves the park loop. */
        do
        {
          __HAL_GPIO_EXTI_CLEAR_IT(ZPL_Pin);
          s_zpl_evt = 0U;
          PWR_EnterStopMode(); /* blocks in WFI until a CPU1 wake event */
        } while ((s_zpl_evt == 0U) &&
                 (HAL_GPIO_ReadPin(ZPL_GPIO_Port, ZPL_Pin) == GPIO_PIN_RESET));

        PWR_ExitStopMode(); /* release ENTRY_STOP_MODE semaphore + resume tick */
        if (__HAL_RCC_GET_SYSCLK_SOURCE() == RCC_SYSCLKSOURCE_STATUS_HSI)
        {
          SystemClock_Config(); /* restore 64 MHz PLL (UART baud + flash latency) */
        }
        {
          uint32_t slept = LP_rtc_ms() - rtc_park;       /* real time: RTC ran through STOP2 */
          uint32_t tick_gap = HAL_GetTick() - tick_park; /* frozen: systick stopped in STOP2 */
          printf("[LP] #%lu <- ZPL wake, slept %lums (RTC), tick +%lums, DOUT=%d\r\n",
                 (unsigned long)s_lp_cycle,
                 (unsigned long)slept,
                 (unsigned long)tick_gap,
                 (int)HAL_GPIO_ReadPin(ZPL_GPIO_Port, ZPL_Pin));
        }
        s_zpl_evt = 0U; /* consumed by the wake print above */
        s_lp_wake_tick = HAL_GetTick();
        s_lp_next = HAL_GetTick() + ZPL_LP_REARM_DELAY_MS;
      }
    }
    // HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    // HAL_Delay(200);
    // HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    // HAL_Delay(200);
    // HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    // HAL_Delay(200);
    // HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    // HAL_Delay(200);
    // HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    // HAL_Delay(600);
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure LSE Drive Capability
   */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);

  /** Configure the main internal regulator output voltage
   */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV2;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV8;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the SYSCLKSource, HCLK, PCLK1 and PCLK2 clocks dividers
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK4 | RCC_CLOCKTYPE_HCLK2 | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK2Divider = RCC_SYSCLK_DIV2;
  RCC_ClkInitStruct.AHBCLK4Divider = RCC_SYSCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief Peripherals Common Clock Configuration
 * @retval None
 */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
   */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SMPS | RCC_PERIPHCLK_RFWAKEUP;
  PeriphClkInitStruct.RFWakeUpClockSelection = RCC_RFWKPCLKSOURCE_HSE_DIV1024;
  PeriphClkInitStruct.SmpsClockSelection = RCC_SMPSCLKSOURCE_HSE;
  PeriphClkInitStruct.SmpsDivSelection = RCC_SMPSCLKDIV_RANGE1;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN Smps */

  /* USER CODE END Smps */
}

/* USER CODE BEGIN 4 */
/**
 * @brief EXTI line 0: VM3011 DOUT (ZPL FLAG) edge detected
 */
void EXTI0_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == ZPL_Pin)
  {
    /* Just flag it: also serves as the STOP2 wake flag. Printing is done
     * from the main loop (correct clock/baud there, even after STOP2 wake). */
    s_zpl_evt = 1U;
  }
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
