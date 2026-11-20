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
 * TIM2: 72MHz / 72 = 1MHz，Period 100-1 → 1MHz / 100 = 10kHz
 *       10kHz 在人耳听觉范围之外，电机不会发出"嗡嗡"啸叫
 *       CH1 = PA15 → DRV8833 AIN1
 *       CH2 = PB3  → DRV8833 AIN2
 *
 * DRV8833 单向驱动规则（同一时刻只让一路出 PWM，另一路拉低）：
 *       AIN1 = PWM, AIN2 = 0   → 正转，占空比 = 转速
 *       AIN1 = 0,   AIN2 = PWM → 反转，占空比 = 转速
 *       两路都是 0             → 滑行（自由停转，慢慢停下来）
 *       两路都是 1             → 刹车（电机两端短接，立刻停）
 * --------------------------------------------------------------- */

/* 电位器中点。旋钮旋到中间时 ADC 大约是 2048，
   如果实测静止点不在中间（比如 2000 停不住、一直在爬行），把这个数改成实测值 */
#define KNOB_CENTER        2048
#define KNOB_FULL_SCALE    2048    /* 中点往一个方向的最大行程，用于换算百分比 */

#define ADC_AVG_N          8U      /* 每次读 8 次 ADC 取平均，压掉抖动 */
#define CTRL_PERIOD_MS     20U     /* 每 20ms 采一次、更新一次输出 */

/* 中点死区：|百分比| 小于 3 就当作 0（停止）。
   没有死区，旋钮停在中间时 ADC 在 2047/2049 之间抖，电机会慢慢爬行 */
#define DEADBAND_PCT       3U

/* 电机死区补偿：直流电机在低占空比下根本转不起来（只有电流声、不转）。
   所以只要不是 0 档，最低也给 25% —— 1% 的旋钮量对应 25% 占空比，
   100% 对应 100%，中间线性过渡。想要严格线性就把这里改成 0 */
#define DUTY_MIN           25U

/* 加速度限制：每 20ms 最多变化 5%，从 0 到满速约 0.4 秒。
   这样旋钮一转电机是"逐渐加快"而不是"啪一下"全速；
   反向时也会先减速到 0 再反向加速，不会瞬间反接电流冲击 */
#define SLEW_STEP          5
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static int16_t  out_duty  = 0;    /* 当前实际输出 -100 ~ +100，正=前进 负=后退 */
static uint32_t ctrl_tick = 0U;   /* 上次采样的时刻 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  连续读 ADC_AVG_N 次取平均
 * @note   单次读数有 ±2~3 个字的抖动。旋钮停在中间时，2048/2050 的差别
 *         足以让电机慢慢爬行，取平均能压掉大部分。
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
 * @brief  旋钮原始值 0~4095 换算成有符号百分比 -100 ~ +100
 * @note   中点 2048 为 0；往一侧是正（前进），另一侧是负（后退）。
 *         中点附近留 DEADBAND_PCT 的死区，直接归 0。
 */
static int16_t knob_to_pct(uint16_t raw)
{
  int32_t offset = (int32_t)raw - (int32_t)KNOB_CENTER;
  int32_t pct    = offset * 100 / (int32_t)KNOB_FULL_SCALE;

  if (pct > 100)
  {
    pct = 100;
  }
  else if (pct < -100)
  {
    pct = -100;
  }

  if ((pct > -(int32_t)DEADBAND_PCT) && (pct < (int32_t)DEADBAND_PCT))
  {
    pct = 0;
  }

  return (int16_t)pct;
}

/**
 * @brief  百分比 -100~100 换算成实际占空比 -100~100（含死区补偿）
 * @note   0 档输出 0（停转）；非 0 档最低给 DUTY_MIN，再线性加到 100。
 */
static int16_t duty_from_pct(int16_t pct)
{
  int32_t mag;
  int32_t duty;

  if (pct == 0)
  {
    return 0;
  }

  mag  = (pct > 0) ? (int32_t)pct : -(int32_t)pct;
  duty = (int32_t)DUTY_MIN + mag * (100 - (int32_t)DUTY_MIN) / 100;

  return (int16_t)((pct > 0) ? duty : -duty);
}

/**
 * @brief  把带符号占空比写到 DRV8833 的两路输入
 * @note   同一时刻只有一路出 PWM，另一路固定低电平 —— DRV8833 靠这个判方向
 */
static void motor_apply(int16_t duty)
{
  if (duty > 0)
  {
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (uint32_t)duty);   /* AIN1 = PWM */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0U);              /* AIN2 = 0   */
  }
  else if (duty < 0)
  {
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);              /* AIN1 = 0   */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, (uint32_t)(-duty)); /* AIN2 = PWM */
  }
  else
  {
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);              /* 两路都 0：滑行 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0U);
  }
}

/**
 * @brief  一次完整的「采样 -> 换算 -> 限速 -> 输出」
 * @note   out_duty 每 20ms 最多向目标靠近 SLEW_STEP。
 *         反向时它会先经过 0 再到负值，相当于自动先刹车再反转。
 */
static void motor_task(void)
{
  uint16_t raw    = adc_read_avg();
  int16_t  pct    = knob_to_pct(raw);
  int16_t  target = duty_from_pct(pct);

  if (out_duty < target)
  {
    out_duty += SLEW_STEP;
    if (out_duty > target)
    {
      out_duty = target;
    }
  }
  else if (out_duty > target)
  {
    out_duty -= SLEW_STEP;
    if (out_duty < target)
    {
      out_duty = target;
    }
  }

  motor_apply(out_duty);
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
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  /* 启动前先把两路比较值清零：CCR 默认就是 0，但显式写一遍更保险，
     避免上电瞬间输出一个意料之外的占空比让电机抽一下 */
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0U);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);

  /* 先跑一次，让输出和旋钮当前位置一致 */
  motor_task();
  ctrl_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 每 20ms 采一次旋钮。不用 HAL_Delay，主循环保持可响应。 */
    if ((HAL_GetTick() - ctrl_tick) >= CTRL_PERIOD_MS)
    {
      ctrl_tick = HAL_GetTick();
      motor_task();
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
