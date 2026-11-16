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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static const uint8_t HELLO_MSG[] = "Hello World!\r\n";
static const uint8_t MENU_MSG[] =
  "\r\n=== LED Blink Control ===\r\n"
  "1:P1000  2:P500  3:P250  4:P100  5:P50\r\n"
  "0:stop   +:slower   -:faster\r\n"
  "========================\r\n";

volatile uint32_t led_interval_ms = 500U;
volatile uint8_t  led_running     = 1U;
uint32_t          last_toggle_tick = 0U;

uint8_t          rx_byte   = 0U;
volatile uint8_t rx_cmd    = 0U;
volatile uint8_t rx_ready  = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_send(const char *s)
{
  uint16_t len = 0U;

  while (s[len] != '\0')
  {
    len++;
  }
  HAL_UART_Transmit(&huart1, (uint8_t *)s, len, 100U);
}

static void uart_report(uint32_t ms)
{
  char     buf[24];
  char     num[10];
  uint32_t i = 0U;
  uint32_t n = 0U;

  buf[i++] = 'O';
  buf[i++] = 'K';
  buf[i++] = ' ';

  if (ms == 0U)
  {
    num[n++] = '0';
  }
  while (ms > 0U)
  {
    num[n++] = (char)('0' + (ms % 10U));
    ms /= 10U;
  }
  while (n > 0U)
  {
    buf[i++] = num[--n];
  }

  buf[i++] = ' ';
  buf[i++] = 'm';
  buf[i++] = 's';
  buf[i++] = '\r';
  buf[i++] = '\n';

  HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)i, 100U);
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
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  uart_send((const char *)HELLO_MSG);
  uart_send((const char *)MENU_MSG);

  led_running        = 1U;
  led_interval_ms    = 500U;
  last_toggle_tick   = HAL_GetTick();

  HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (rx_ready != 0U)
    {
      uint8_t cmd = rx_cmd;
      rx_ready = 0U;

      switch (cmd)
      {
        case '0':
          led_running = 0U;
          HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
          uart_send("STOP\r\n");
          break;

        case '1':
          led_interval_ms = 1000U;
          led_running     = 1U;
          uart_report(led_interval_ms);
          break;

        case '2':
          led_interval_ms = 500U;
          led_running     = 1U;
          uart_report(led_interval_ms);
          break;

        case '3':
          led_interval_ms = 250U;
          led_running     = 1U;
          uart_report(led_interval_ms);
          break;

        case '4':
          led_interval_ms = 100U;
          led_running     = 1U;
          uart_report(led_interval_ms);
          break;

        case '5':
          led_interval_ms = 50U;
          led_running     = 1U;
          uart_report(led_interval_ms);
          break;

        case '+':
          if (led_interval_ms < 2000U)
          {
            led_interval_ms += 100U;
          }
          led_running = 1U;
          uart_report(led_interval_ms);
          break;

        case '-':
          if (led_interval_ms > 100U)
          {
            led_interval_ms -= 100U;
          }
          led_running = 1U;
          uart_report(led_interval_ms);
          break;

        case '\r':
        case '\n':
          break;

        default:
          uart_send("? use 0-5 or + -\r\n");
          break;
      }
    }

    if ((led_running != 0U) &&
        ((HAL_GetTick() - last_toggle_tick) >= led_interval_ms))
    {
      last_toggle_tick = HAL_GetTick();
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
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
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    rx_cmd   = rx_byte;
    rx_ready = 1U;

    /* 重新挂上接收，否则只能收到一个字节 */
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);
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
