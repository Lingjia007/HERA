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
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "audio_app.h"
#include "vm3011.h"
#include "platform_w25q80_stm32_impl.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

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
  MX_SPI1_Init();
  MX_RF_Init();
  /* USER CODE BEGIN 2 */
  /* W25Q80 SPI Flash 自检：初始化 -> 读 ID -> 读 0x000000 前 16 字节 */
  {
    static w25q80_stm32_t s_w25q80;
    uint8_t buf[16];
    uint32_t i;

    printf("[W25Q80] selftest start (CS=PA4, SPI1)\r\n");

    /* CS=PA4 已由 MX_GPIO_Init() 配好：开漏输出、初始高，板载上拉拉高。
     * 拉低为主动驱动、释放靠上拉，2 MHz SPI 下时序无问题，无需再改回推挽。 */

    platform_w25q80_stm32_register(&s_w25q80, &hspi1, GPIOA, GPIO_PIN_4, "w25q80");

    int16_t st = SPI_FLASH_INIT(&s_w25q80.base);
    printf("[W25Q80] init ret=%d, id(0x90)=0x%04X\r\n", st, SPI_FLASH_READ_ID(&s_w25q80.base));
    {
      uint8_t jid[3] = {0};
      if (platform_w25q80_stm32_read_jedec_id(&s_w25q80, jid) == SPI_FLASH_STATUS_OK)
      {
        printf("[W25Q80] jedec(0x9F)=0x%02X 0x%02X 0x%02X (expect EF 40 14)\r\n",
               jid[0], jid[1], jid[2]);
      }
    }
    if (st == SPI_FLASH_STATUS_OK)
    {
      printf("[W25Q80] total=%lu B, sector=%lu B, block=%lu B, page=%u B\r\n",
             (unsigned long)SPI_FLASH_GET_TOTAL_SIZE(&s_w25q80.base),
             (unsigned long)SPI_FLASH_GET_SECTOR_SIZE(&s_w25q80.base),
             (unsigned long)SPI_FLASH_GET_BLOCK_SIZE(&s_w25q80.base),
             SPI_FLASH_GET_PAGE_SIZE(&s_w25q80.base));

      if (SPI_FLASH_READ(&s_w25q80.base, 0x000000, buf, sizeof(buf)) == SPI_FLASH_STATUS_OK)
      {
        printf("[W25Q80] @0x000000:");
        for (i = 0; i < sizeof(buf); i++)
        {
          printf(" %02X", buf[i]);
        }
        printf("\r\n");
      }
      else
      {
        printf("[W25Q80] read failed\r\n");
      }
    }
  }

  /* VM3011 bring-up via driver (Zephyr port): waits 1 s for ZPL settle,
   * verifies I2C_CNTRL==0x00 (device detect) and applies the config.
   * Runs BEFORE AudioApp_Init: the audio app stores the handle and clears
   * DOUT itself when a listen session closes. */
  {
    VM3011_Config_t cfg;
    VM3011_ConfigDefault(&cfg);
    cfg.wos_dout_thresh = 3U; /* 4x margin (12.0 dB SPL over ambient).
                               * 1 (6 dB, most sensitive) false-woke constantly
                               * in a noisy room (VAD floor ~430): after every
                               * ClearDout the ambient swell re-asserted DOUT
                               * instantly -> wake/sleep loop. 4x requires the
                               * event to be >= 12 dB over ambient, which an
                               * ordinary noise swell does not reach while
                               * speech does. Reg05 table: 1..7 = 6.0/9.5/
                               * 12.0/14.0/15.5/16.9/18.0 dB - tune per room. */
    cfg.wos_rms = 1U;         /* 2 Hz WoS comparator: DOUT asserts ~2x
                              * faster than the 1 Hz default - observed:
                              * with 1 Hz the wake arrived so late that the
                              * keyword head was already gone. */
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

        /* Drop any power-up DOUT indication so the wake line starts LOW. */
        (void)VM3011_ClearDout(&s_vm);
      }
    }
  }

  /* ZPL-gated KWS: AudioApp_Init does NOT start the audio stream - the
   * first DOUT wake (see while(1)) opens the first listening session. */
  AudioApp_Init(&s_vm);

  /* VM3011 DOUT (PA0) ZPL wake line, two independent paths:
   *  - EXTI0 rising edge -> HAL_GPIO_EXTI_Callback sets s_zpl_evt;
   *  - PWR WKUP1 (PA0 is WKUP1, level HIGH) -> STOP2 wake even if the
   *    edge slips by; the main loop polls WUF1 and treats it as a ZPL. */
  LL_PWR_SetWakeUpPinPolarityHigh(LL_PWR_WAKEUP_PIN1);
  LL_PWR_EnableWakeUpPin(LL_PWR_WAKEUP_PIN1);
  LL_PWR_ClearFlag_WU1();
  HAL_NVIC_SetPriority(EXTI0_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
  printf("[VM3011] DOUT(PA0) init %s, STOP2 wake armed\r\n",
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
    /* ZPL event detection: EXTI edge flag and/or the STOP2 wake flag
     * (WUF1). Either way, open/extend an audio listening session. */
    if (LL_PWR_IsActiveFlag_WU1() != 0U)
    {
      LL_PWR_ClearFlag_WU1();
      s_zpl_evt = 1U;
    }
    if (s_zpl_evt != 0U)
    {
      s_zpl_evt = 0U;
      AudioApp_ZplNotify();
    }

    AudioApp_Process();
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE
                              |RCC_OSCILLATORTYPE_LSE;
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
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK4|RCC_CLOCKTYPE_HCLK2
                              |RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
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
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SMPS|RCC_PERIPHCLK_RFWAKEUP;
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
