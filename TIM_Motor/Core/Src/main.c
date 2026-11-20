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
#include "i2c.h"      /* hi2c1、MX_I2C1_Init */
#include "oled.h"     /* OLED_NewFrame / OLED_ShowFrame 等绘图接口 */
#include "font.h"     /* afont8x6 / afont12x6 / afont24x12 三套 ASCII 字体 */
#include <stdio.h>    /* sprintf，把数字拼成字符串再画到屏上 */
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

/* 屏幕刷新周期。整屏 1024 字节走 100kHz I2C 大约 95ms，
   所以屏幕必须比控制慢得多：20ms 控制电机、150ms 画一屏。
   如果两件事挤在一起，I2C 会把主循环堵住，电机变成一顿一顿的 */
#define UI_PERIOD_MS       150U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static int16_t  out_duty  = 0;    /* 当前实际输出 -100 ~ +100，正=前进 负=后退 */
static uint32_t ctrl_tick = 0U;   /* 上次采样的时刻 */
static uint16_t last_raw  = 0U;   /* 最近一次 ADC 原始值，只用于屏幕显示 */
static uint32_t ui_tick   = 0U;   /* 上次刷屏的时刻 */
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

  last_raw = raw;   /* 留给屏幕显示 */
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

/**
 * @brief  画一屏：标题 / 带符号占空比 / 方向 / 双向进度条 / ADC 原始值
 * @note   这里显示的是 out_duty（实际输出）而不是旋钮百分比，
 *         因为加速度限制会让两者短暂不同步，看实际输出更符合"电机现在多快"
 *         屏幕布局 128x64：
 *           y=0   小字标题
 *           y=14  24x12 大字数字（4 个字符 = 48 像素宽）
 *           y=20  右侧方向词
 *           y=39  进度条（中线为 0，向右正转、向左反转）
 *           y=53  小字显示 ADC
 */
static void ui_draw(void)
{
  char     buf[20];
  int16_t  duty = out_duty;
  uint8_t  mag  = (uint8_t)((duty >= 0) ? duty : -duty);   /* 取绝对值，条的长度 */
  uint8_t  bar;

  OLED_NewFrame();   /* 先在内存里清空一整帧，最后一次性推给屏幕 */

  /* 第 1 行：标题（12x6 字体，每字符 6 像素宽） */
  OLED_PrintASCIIString(0, 0, "KNOB -> MOTOR", &afont12x6, OLED_COLOR_NORMAL);

  /* 第 2 行：带符号的占空比。%+4d 对正数强制显示 '+'，
     0 的时候显示成 "0"，避免出现 "  +0" 这种别扭的样子 */
  if (duty > 0)
  {
    sprintf(buf, "%+4d", (int)duty);
  }
  else
  {
    sprintf(buf, "%4d", (int)duty);
  }
  OLED_PrintASCIIString(0, 14, buf, &afont24x12, OLED_COLOR_NORMAL);   /* 24 高 12 宽，4 字符占 x=0~47 */
  OLED_PrintASCIIString(52, 22, "%", &afont12x6, OLED_COLOR_NORMAL);   /* 百分号用小字挂在右边 */

  /* 方向词：FWD 正转 / REV 反转 / STOP 停转 */
  if (duty > 0)
  {
    OLED_PrintASCIIString(84, 22, "FWD", &afont12x6, OLED_COLOR_NORMAL);
  }
  else if (duty < 0)
  {
    OLED_PrintASCIIString(84, 22, "REV", &afont12x6, OLED_COLOR_NORMAL);
  }
  else
  {
    OLED_PrintASCIIString(78, 22, "STOP", &afont12x6, OLED_COLOR_NORMAL);
  }

  /* 第 3 行：双向进度条。中线 x=63 代表 0，往右填=正转，往左填=反转 */
  OLED_DrawRectangle(0, 39, 127, 11, OLED_COLOR_NORMAL);   /* 外框 w=127 表示画到 x=126 */
  OLED_DrawLine(63, 40, 63, 48, OLED_COLOR_NORMAL);        /* 中线标记 */

  bar = (uint8_t)((uint16_t)mag * 60U / 100U);             /* 半条最多 60 像素 */
  if (bar > 0U)
  {
    if (duty > 0)
    {
      OLED_DrawFilledRectangle(64, 41, bar, 7, OLED_COLOR_NORMAL);            /* 向右 */
    }
    else
    {
      OLED_DrawFilledRectangle((uint8_t)(63U - bar), 41, bar, 7, OLED_COLOR_NORMAL); /* 向左 */
    }
  }

  /* 第 4 行：ADC 原始值，调 KNOB_CENTER 时对着这个数看 */
  sprintf(buf, "ADC%4u", (unsigned)last_raw);
  OLED_PrintASCIIString(0, 53, buf, &afont8x6, OLED_COLOR_NORMAL);

  OLED_ShowFrame();  /* 整帧 1024 字节一次性写进屏幕，约 95ms */
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

  /* OLED 的上电复位比 MCU 慢，等 20ms 再发初始化指令，
     否则第一批命令会被屏里的驱动芯片当成噪声丢掉，屏幕全黑 */
  HAL_Delay(20);
  OLED_Init();

  /* 把"上次刷屏时刻"往回拨一个周期，让第一帧立刻画出来
     （无符号减法自动回绕，不用担心下溢） */
  ui_tick = HAL_GetTick() - UI_PERIOD_MS;
  ui_draw();
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

    /* 每 150ms 刷一次屏。刷屏是阻塞的（约 95ms 全在等 I2C），
       所以必须和控制分开计时，否则电机速度会被刷屏拖成锯齿 */
    if ((HAL_GetTick() - ui_tick) >= UI_PERIOD_MS)
    {
      ui_tick = HAL_GetTick();
      ui_draw();
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
