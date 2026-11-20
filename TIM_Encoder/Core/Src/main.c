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
/* ---------- 板载 LED：PC13，低电平点亮 ---------- */
#define LED_GPIO_Port     GPIOC
#define LED_Pin           GPIO_PIN_13

/* ---------- 电位器控制 LED 的阈值（带迟滞，防止临界点来回跳）---------- */
#define LED_ON_PCT        50U   /* 百分比 >= 50 时点亮 */
#define LED_OFF_PCT       47U   /* 百分比 <= 47 时熄灭，中间 48~49 保持原状态 */

/* ---------- 每次读 8 次 ADC 取平均，压掉单次读数的抖动 ---------- */
#define ADC_AVG_N         8U

/* ---------- 主循环刷新周期 ---------- */
#define UI_PERIOD_MS      50U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t  led_on       = 0U;   /* 当前 LED 状态：0=灭 1=亮 */
static uint32_t ui_last_tick = 0U;   /* 上次刷新屏幕的时刻 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  把 PC13 配成推挽输出
 * @note   CubeMX 里没勾这个引脚（它不在外设列表里），所以手动初始化。
 *         PC13 是板载 LED，低电平点亮，先把电平写高 = 熄灭。
 */
static void led_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();

  gpio.Pin   = LED_Pin;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &gpio);

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);  /* 高电平 = 灭 */
}

/**
 * @brief  连续读 ADC_AVG_N 次取平均
 * @note   单次读数会有 ±2~3 个字的抖动，取平均后屏幕上的数字才不来回跳。
 *         没读到就跳过这一次，最后仍然除以 ADC_AVG_N，不会除零。
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
static uint8_t adc_to_percent(uint16_t raw)
{
  uint32_t pct = (uint32_t)raw * 100U / 4095U;

  if (pct > 100U)
  {
    pct = 100U;
  }
  return (uint8_t)pct;
}

/**
 * @brief  画一屏：标题 / 大号百分比 / 进度条 / 原始值 + LED 状态
 */
static void ui_draw(uint16_t raw, uint8_t pct)
{
  char    buf[16];
  uint8_t bar_w;

  OLED_NewFrame();

  /* 第 1 行：标题（12x6 字体，一行最多 21 个字符） */
  OLED_PrintASCIIString(0, 0, "ROTARY SENSOR", &afont12x6, OLED_COLOR_NORMAL);

  /* 第 2 行：大号百分比（24x12 字体，4 个字符占 48 像素） */
  sprintf(buf, "%3u%%", (unsigned)pct);
  OLED_PrintASCIIString(4, 13, buf, &afont24x12, OLED_COLOR_NORMAL);

  /* 第 3 行：进度条，外框固定 126x10，内部按百分比填充 */
  OLED_DrawRectangle(0, 39, 126, 10, OLED_COLOR_NORMAL);
  bar_w = (uint8_t)((uint16_t)pct * 122U / 100U);
  if (bar_w > 0U)
  {
    OLED_DrawFilledRectangle(2, 41, bar_w, 6, OLED_COLOR_NORMAL);
  }

  /* 第 4 行：ADC 原始值 + LED 状态（8x6 字体，一行最多 21 个字符） */
  sprintf(buf, "ADC %4u", (unsigned)raw);
  OLED_PrintASCIIString(0, 52, buf, &afont8x6, OLED_COLOR_NORMAL);
  OLED_PrintASCIIString(72, 52, (led_on != 0U) ? "LED ON" : "LED OFF",
                        &afont8x6, OLED_COLOR_NORMAL);

  OLED_ShowFrame();
}

/**
 * @brief  一次完整的「采样 -> 判断 -> 输出 -> 显示」
 */
static void app_task(void)
{
  uint16_t raw = adc_read_avg();
  uint8_t  pct = adc_to_percent(raw);

  /* 迟滞判断：>=50% 点亮、<=47% 熄灭，48~49% 之间保持原状态。
     没有这两个点的差值，旋钮停在 50% 附近时 LED 会疯狂闪烁。 */
  if ((led_on == 0U) && (pct >= LED_ON_PCT))
  {
    led_on = 1U;
  }
  else if ((led_on != 0U) && (pct <= LED_OFF_PCT))
  {
    led_on = 0U;
  }

  /* PC13 低电平点亮 */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
                    (led_on != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);

  /* 三路 PWM 占空比同步跟随电位器：外接 LED 时就是连续调光 */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pct);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, pct);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, pct);

  ui_draw(raw, pct);
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
  MX_I2C1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  /* OLED 上电比 MCU 慢，先等 20ms 再初始化，否则第一批指令会丢 */
  HAL_Delay(20);
  OLED_Init();

  /* 板载 LED（PC13）在 CubeMX 里没配，这里手动初始化 */
  led_gpio_init();

  /* 三路 PWM 一起启动：CH1=PA6 / CH2=PA7 / CH3=PB0，接哪一路都有输出 */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);

  /* 先画一帧，避免开机后屏幕空白一段时间 */
  app_task();
  ui_last_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 每 50ms 采样并刷新一次。用 HAL_GetTick 判断而不是 HAL_Delay，
       这样以后要往主循环里加别的任务不会被卡住。 */
    if ((HAL_GetTick() - ui_last_tick) >= UI_PERIOD_MS)
    {
      ui_last_tick = HAL_GetTick();
      app_task();
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
