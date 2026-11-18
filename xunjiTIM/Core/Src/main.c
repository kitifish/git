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
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "stdio.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ================== 可调参数 ================== */
/* 一条"黑+白"条纹对应的传送距离(毫米)。
   用尺子量传送带上一个黑条 + 一个白条的总宽度，填进来。
   这个数错了，速度会整体成比例偏大或偏小，但波形/趋势依然正确。 */
#define STRIPE_MM       20U

/* 测速窗口(毫秒)：每多少毫秒算一次速度。
   太小 → 一窗口内脉冲太少，量化误差大、数字跳；太大 → 反应迟钝。 */
#define WIN_MS          500U

/* 脉冲来源：
   1 = 模拟脉冲（手里没有循迹模块时用，屏幕上是"假数据"，用来验证
       显示、单位换算、界面布局是否正常）
   0 = 真实脉冲（循迹模块 DO 接 PA15，由 TIM2 外部时钟计数）
   拿到模块后把这里改成 0 就行，其它代码一行都不用动。 */
#define SIM_PULSE       1
/* ============================================== */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint32_t total_pulse = 0U;    /* 累计脉冲数（一条黑+白算 1 个） */
static uint32_t speed_01mms = 0U;    /* 速度，单位 0.1 mm/s，用整数存避免浮点 */
static uint32_t distance_mm = 0U;    /* 累计传送距离，mm */
static uint32_t win_tick    = 0U;    /* 上一个测速窗口的时间戳 */
#if (SIM_PULSE == 0)
static uint16_t last_cnt    = 0U;    /* 上次读到的 TIM2->CNT */
#endif
static char line_buf[24];            /* 屏幕文本缓冲 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#if (SIM_PULSE == 1)
/**
  * @brief  模拟脉冲发生器（没有循迹模块时用）
  * @note   让"每秒脉冲数"在 20~80 之间来回摆动，模拟传送带加速/减速。
  *         每 6 秒一个来回，屏幕上的数字会明显变化，方便确认刷新正常。
  */
static uint16_t sim_pulse(uint32_t now_ms, uint32_t win_ms)
{
  uint32_t t   = (now_ms / 100U) % 60U;              /* 0~59，共 6 秒 */
  uint32_t tri = (t < 30U) ? t : (60U - t);          /* 0→30→0 三角波 */
  uint32_t rate = 20U + tri * 2U;                    /* 20~80 脉冲/秒 */
  return (uint16_t)((rate * win_ms) / 1000U);
}
#endif

/**
  * @brief  一个测速窗口到了：取增量、算速度、算距离
  * @note   核心是 (uint16_t)(now - last) 无符号相减，天然跨过 65535 回绕，
  *         所以不需要开 TIM2 更新中断去数溢出次数。
  */
static void speed_task(uint32_t now_ms)
{
  uint16_t delta;
#if (SIM_PULSE == 1)
  delta = sim_pulse(now_ms, WIN_MS);
#else
  uint16_t now_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
  delta    = (uint16_t)(now_cnt - last_cnt);
  last_cnt = now_cnt;
#endif

  total_pulse += delta;
  distance_mm  = total_pulse * STRIPE_MM;

  /* 速度(mm/s) = 脉冲数 × 每脉冲距离 × 1000 / 窗口ms
     换算成 0.1mm/s 再 ×10，用 uint64 中间量防止乘法溢出 */
  speed_01mms = (uint32_t)(((uint64_t)delta * STRIPE_MM * 10000ULL) / WIN_MS);

  if (speed_01mms > 999999U) { speed_01mms = 999999U; }   /* 显示上限 99999.9 */
}

/**
  * @brief  刷新 OLED：大字号速度 + 小字号脉冲数 / 距离
  */
static void display_update(void)
{
  OLED_NewFrame();

  /* 第 1 行：标题 */
  OLED_PrintASCIIString(0, 0, "SPEED   mm/s", &afont12x6, OLED_COLOR_NORMAL);
#if (SIM_PULSE == 1)
  OLED_PrintASCIIString(106, 2, "SIM", &afont8x6, OLED_COLOR_NORMAL);
#endif

  /* 第 2 行：速度，24×12 大字号，形如 " 123.4" */
  sprintf(line_buf, "%5lu.%01lu",
          (unsigned long)(speed_01mms / 10U),
          (unsigned long)(speed_01mms % 10U));
  OLED_PrintASCIIString(0, 14, line_buf, &afont24x12, OLED_COLOR_NORMAL);

  /* 第 3 行：累计脉冲 */
  sprintf(line_buf, "PULSE %lu", (unsigned long)total_pulse);
  OLED_PrintASCIIString(0, 41, line_buf, &afont12x6, OLED_COLOR_NORMAL);

  /* 第 4 行：累计距离 */
  sprintf(line_buf, "LEN %lu mm", (unsigned long)distance_mm);
  OLED_PrintASCIIString(0, 52, line_buf, &afont12x6, OLED_COLOR_NORMAL);

  OLED_ShowFrame();
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
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  HAL_Delay(20);              /* STM32 上电比 OLED 快，等它稳定再初始化 */
  OLED_Init();

  /* 启动 TIM2：时钟源是 PA15 上的外部脉冲，不是内部 72MHz */
  HAL_TIM_Base_Start(&htim2);

  win_tick = HAL_GetTick();
  display_update();           /* 先把界面画出来，避免上电黑屏 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* 每 WIN_MS 毫秒结算一次：取增量 → 算速度 → 刷屏 */
    if ((uint32_t)(now - win_tick) >= WIN_MS)
    {
      win_tick = now;
      speed_task(now);
      display_update();
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
