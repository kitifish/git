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
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "oled.h"
#include "font.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ---------------------------------------------------------------
 * TIM4: 72MHz / 720 = 100kHz，一个计数 = 10us
 *       Period = 2000-1 → 2000 x 10us = 20ms = 50Hz（舵机的标准周期）
 *
 * 脉宽与角度（以 SG90 为例，标称 1.0ms = 0 度，2.0ms = 180 度）：
 *       CCR = 100 → 1.0ms →   0 度
 *       CCR = 150 → 1.5ms →  90 度（中位）
 *       CCR = 200 → 2.0ms → 180 度
 * 先用这个安全范围测试。如果发现转不到头，再把 MIN/MAX 放宽到
 * 50/250（0.5ms / 2.5ms）——但别一上来就用 0.5~2.5ms，
 * 很多舵机会顶到机械限位堵转、发热甚至烧掉。
 * --------------------------------------------------------------- */
#define SERVO_MIN_CCR      100U   /* 对应   0 度 / 1.0ms */
#define SERVO_MAX_CCR      200U   /* 对应 180 度 / 2.0ms */

/* ---------- 电位器 -> 舵机 ---------- */
#define ADC_AVG_N          8U     /* 每次读 8 次 ADC 取平均，压掉抖动 */
#define CTRL_PERIOD_MS     20U    /* 每 20ms 采一次、更新一次 CCR */
#define UI_PERIOD_MS       150U   /* 屏幕 150ms 刷一次：整屏 I2C 传输约 95ms */
#define DEADBAND_PCT       1U     /* 变化小于 1% 就不动，避免舵机原地嗡嗡抖 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t  last_pct     = 50U;    /* 上次的旋钮百分比，初值取中位 */
static uint16_t last_ccr     = 0U;     /* 当前输出的 CCR */
static uint32_t ctrl_tick    = 0U;     /* 上次采样的时刻 */
static uint32_t ui_last_tick = 0U;     /* 上次刷屏的时刻 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  连续读 ADC_AVG_N 次取平均
 * @note   单次读数有 ±2~3 个字的抖动，直接拿去算角度，舵机会在原地小幅抽动。
 */
static uint16_t adc_read_avg(void)
{
  uint32_t sum = 0U;
  uint8_t  i;

  for (i = 0U; i < ADC_AVG_N; i++)
  {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK)
    {
      sum += HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
  }

  return (uint16_t)(sum / ADC_AVG_N);
}

/**
 * @brief  原始值 0~4095 换算成百分比 0~100
 */
static uint8_t adc_to_pct(uint16_t raw)
{
  uint32_t pct = (uint32_t)raw * 100U / 4095U;

  if (pct > 100U)
  {
    pct = 100U;
  }
  return (uint8_t)pct;
}

/**
 * @brief  百分比 0~100 换算成 CCR 100~200（即 1.0ms ~ 2.0ms）
 * @note   CCR 差正好 100，所以 CCR = 100 + pct，不用做除法
 */
static uint16_t pct_to_ccr(uint8_t pct)
{
  return (uint16_t)(SERVO_MIN_CCR +
                    ((uint16_t)pct * (SERVO_MAX_CCR - SERVO_MIN_CCR)) / 100U);
}

/**
 * @brief  百分比 0~100 换算成角度 0~180
 */
static uint16_t pct_to_deg(uint8_t pct)
{
  return (uint16_t)(((uint16_t)pct * 180U) / 100U);
}

/**
 * @brief  画一屏：标题 / 大号角度 / 进度条 / ADC 与 CCR
 */
static void ui_draw(uint16_t deg, uint8_t pct, uint16_t ccr, uint16_t raw)
{
  char    buf[20];
  uint8_t bar_w;

  OLED_NewFrame();

  /* 第 1 行：标题（12x6 字体） */
  OLED_PrintASCIIString(0, 0, "KNOB -> SERVO", &afont12x6, OLED_COLOR_NORMAL);

  /* 第 2 行：大号角度（24x12 字体，一个字符 12 像素宽），右边补单位 */
  sprintf(buf, "%3u", (unsigned)deg);
  OLED_PrintASCIIString(2, 13, buf, &afont24x12, OLED_COLOR_NORMAL);
  OLED_PrintASCIIString(44, 20, "deg", &afont12x6, OLED_COLOR_NORMAL);

  /* 第 3 行：进度条，外框 126x10，内部按旋钮百分比填充 */
  OLED_DrawRectangle(0, 39, 126, 10, OLED_COLOR_NORMAL);
  bar_w = (uint8_t)((uint16_t)pct * 122U / 100U);
  if (bar_w > 0U)
  {
    OLED_DrawFilledRectangle(2, 41, bar_w, 6, OLED_COLOR_NORMAL);
  }

  /* 第 4 行：ADC 原始值 + 当前 CCR（8x6 字体） */
  sprintf(buf, "ADC%4u PWM%3u", (unsigned)raw, (unsigned)ccr);
  OLED_PrintASCIIString(0, 53, buf, &afont8x6, OLED_COLOR_NORMAL);

  OLED_ShowFrame();
}

/**
 * @brief  一次完整的「采样 -> 换算 -> 驱动舵机 -> 刷新屏幕」
 * @note   屏幕 150ms 才刷一次：整屏 1024 字节走 100kHz I2C 约 95ms，
 *         如果每 20ms 都刷，主循环会被 I2C 完全堵住，舵机就卡成一段一段的。
 */
static void control_task(void)
{
  uint16_t raw = adc_read_avg();
  uint8_t  pct = adc_to_pct(raw);
  uint8_t  diff;

  /* 旋钮几乎没动（差值 < DEADBAND_PCT）就不改 CCR，
     否则 ADC 的最后一位在抖，舵机会一直听到新脉宽、原地嗡嗡响 */
  diff = (pct > last_pct) ? (uint8_t)(pct - last_pct)
                          : (uint8_t)(last_pct - pct);

  if (diff >= DEADBAND_PCT)
  {
    last_pct = pct;
    last_ccr = pct_to_ccr(pct);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, last_ccr);
  }

  if ((HAL_GetTick() - ui_last_tick) >= UI_PERIOD_MS)
  {
    ui_last_tick = HAL_GetTick();
    ui_draw(pct_to_deg(last_pct), last_pct, last_ccr, raw);
  }
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

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM1_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
  /* OLED 上电比 MCU 慢，先等 20ms 再初始化，否则第一批指令会丢 */
  HAL_Delay(20);
  OLED_Init();

  /* 先把 CCR 设到中位（90 度）再启动 PWM，
     否则会先输出一个 CCR=0 的"无脉冲"，某些舵机会抖一下 */
  last_ccr = pct_to_ccr(last_pct);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, last_ccr);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);

  /* 把"上次刷屏时刻"设成刚好过期，让第一帧立刻画出来（无符号减法会自动回绕） */
  ui_last_tick = HAL_GetTick() - UI_PERIOD_MS;
  control_task();
  ctrl_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 每 20ms 跟着旋钮更新一次。不用 HAL_Delay，主循环保持可响应。 */
    if ((HAL_GetTick() - ctrl_tick) >= CTRL_PERIOD_MS)
    {
      ctrl_tick = HAL_GetTick();
      control_task();
    }
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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
