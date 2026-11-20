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
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

#define SERVO_STEP_DEG     1      /* 每次走 1 度 */
#define SERVO_PERIOD_MS    20U    /* 每 20ms 走一步，和舵机刷新周期对齐 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static int16_t  servo_angle = 0;    /* 当前角度 0~180 */
static int8_t   servo_dir   = 1;    /* +1 = 往 180 度走，-1 = 往 0 度走 */
static uint32_t servo_tick  = 0U;   /* 上次走步的时刻 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  角度(0~180) 换算成 CCR(50~250)
 * @note   用整数线性插值，不碰浮点：180 度对应 CCR 差 200，正好 1 度 ≈ 1.11 个 CCR
 */
static uint16_t servo_angle_to_ccr(int16_t deg)
{
  if (deg < 0)   { deg = 0; }
  if (deg > 180) { deg = 180; }

  return (uint16_t)(SERVO_MIN_CCR +
                    ((int32_t)deg * (int32_t)(SERVO_MAX_CCR - SERVO_MIN_CCR)) / 180);
}

/**
 * @brief  走一步：角度加/减 1 度，到两端就掉头，然后写 CCR
 */
static void servo_step(void)
{
  servo_angle += (int16_t)(servo_dir * SERVO_STEP_DEG);

  if (servo_angle >= 180)
  {
    servo_angle = 180;
    servo_dir   = -1;
  }
  else if (servo_angle <= 0)
  {
    servo_angle = 0;
    servo_dir   = 1;
  }

  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, servo_angle_to_ccr(servo_angle));
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
  MX_TIM1_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
  /* 先把 CCR 设到 0 度对应的脉宽，再启动 PWM，
     否则会先输出一个 CCR=0 的"无脉冲"，某些舵机会抖一下 */
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, servo_angle_to_ccr(servo_angle));
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);

  servo_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 每 20ms 走一度，0→180 约 3.6 秒，再掉头回来。
       不用 HAL_Delay，主循环保持可响应。 */
    if ((HAL_GetTick() - servo_tick) >= SERVO_PERIOD_MS)
    {
      servo_tick = HAL_GetTick();
      servo_step();
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
