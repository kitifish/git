# STM32F103C8T6 学习工程集

蓝色药丸（STM32F103C8T6 最小系统板）的裸机练习代码，工具链为 **STM32CubeMX 6.15 + CLion + CMake + arm-none-eabi-gcc**。

## 目录结构

```
STM32/
├── STM32中断/       外部中断练习：PC13 LED + PA0 按键，四档闪烁状态机
├── helloworldsent/  串口双向通信：USART1 轮询发送 Hello World + 接收命令控制闪灯
├── interruptsent/   中断版串口控制：USART1 接收中断 + 四档闪烁 + 定时翻转
├── receive/         不定长数据收发：DMA + 空闲中断（IDLE），回显长度与原文
├── TIM/             定时器计时：TIM4 计数换算成毫秒，串口定时上报
├── xunjiTIM/        定时器外部时钟：循迹模块 DO→PA15，TIM2 ETR 计数，OLED 显示速度
├── TIM_SlaveMode/   定时器从模式：TIM2 复位模式 + TI1FP1 触发，CNT 即外部信号周期
├── PWM_Light/       PWM 呼吸灯：TIM3_CH1（PA6）10kHz 输出，主循环扫 CCR
├── TIM_Encoder/     旋转电位器：ADC 采样 → PC13 亮灭 + 三路 PWM 调光 + OLED 显示
├── LED/             点灯入门（第一版）
└── lighting/        点灯入门（第二版）
```

十一个工程各约 283 个受版本控制的文件。构建产物（`build/`、`cmake-build-*/`）与 CMSIS 中本工程未使用的 `DSP` / `Lib` / `NN` / `RTOS` / `Core_A` / `docs`（合计约 50MB）均已排除，clone 后可直接编译。

## 硬件接线

### 板载 LED

PC13 驱动，**低电平点亮**（输出 `GPIO_PIN_RESET` 亮，`GPIO_PIN_SET` 灭）。

### 串口（USB 转 TTL）

| 开发板 | USB-TTL 模块 | 说明 |
|---|---|---|
| PA9（USART1_TX） | RXD | 必须交叉，不能 TX 接 TX |
| PA10（USART1_RX） | TXD | 接收电脑指令 |
| GND | GND | 必须共地 |
| — | 3V3/5V | 不接，板子单独供电 |

串口参数：**115200 / 8 数据位 / 无校验 / 1 停止位 / 无流控**

### SWD 下载

| 开发板 | ST-Link |
|---|---|
| 3.3V | 3.3V |
| DIO（PA13） | SWDIO |
| CLK（PA14） | SWCLK |
| GND | GND |

## helloworldsent：串口命令控制闪烁

上电后串口打印 `Hello World!` 和命令菜单，此后由电脑下发单字符命令控制 LED：

| 命令 | 效果 |
|---|---|
| `0` | 停止闪烁，LED 熄灭 |
| `1` | 周期 1000 ms（慢） |
| `2` | 周期 500 ms |
| `3` | 周期 250 ms |
| `4` | 周期 100 ms |
| `5` | 周期 50 ms（快） |
| `+` | 周期 +100 ms（上限 2000） |
| `-` | 周期 -100 ms（下限 100） |

每条命令都会回显 `OK <周期> ms`。串口助手**不需要勾选"发送新行"**，单字符即时生效。

实现要点：

- 串口接收走 USART1 中断，回调里只存一个字节并置标志，然后重新挂上 `HAL_UART_Receive_IT`
- 闪烁用 `HAL_GetTick()` 计时，主循环非阻塞翻转，**没有用 `HAL_Delay`**，否则收不到串口数据
- 命令解析与回显都放在主循环，不在中断里做阻塞发送

## interruptsent：全中断版串口控制

和 `helloworldsent` 同样的命令集，但接收侧改成了中断驱动，是"轮询 → 中断"的对照版本：

| 命令 | 效果 |
|---|---|
| `0` | 停止闪烁，LED 熄灭 |
| `1` | 每 500 ms 翻转一次 |
| `2` | 每 100 ms 翻转一次 |
| `3` | 常亮 |

每条命令回显当前模式。实现要点：

- `HAL_UART_Receive_IT(&huart1, &rx_byte, 1)` 只挂 1 字节，进 `HAL_UART_RxCpltCallback` 后**必须重新挂接收**，否则只能收到第一个字符
- 回调里只做三件事：存字节、置 `volatile` 标志、重挂接收，不做任何打印
- 主循环用 `HAL_GetTick()` 判间隔翻转，`uint32_t` 无符号相减天然处理 49 天溢出
- 没有用 `HAL_Delay`，否则串口响应会被拖死

## receive：不定长数据收发（DMA + 空闲中断）

串口收到的数据长度事先不知道，用定长的 `HAL_UART_Receive_IT` 没法处理。这里用**DMA 搬运 + 串口空闲中断（IDLE）**判帧尾：字节连续进内存，总线静默超过 3.5 个字符时间就认为一帧结束。

上电打印提示，之后发什么都能收，回显格式为 `[收到 N 字节] 原文`。

CubeMX 关键配置：

| 项目 | 设置 |
|---|---|
| USART1 | 115200 / 8N1 / 收发双向 / `USART1 global interrupt` 使能 |
| DMA Request | `USART1_RX` → DMA1 Channel 5（外设→内存、字节、地址自增） |
| DMA Request | `USART1_TX` → DMA1 Channel 4（内存→外设） |
| NVIC | `DMA1_Channel5_IRQn` 使能 |
| SYS → Debug | **Serial Wire**（选 No Debug 会锁死 SWD） |

实现要点：

- 启动接收只用 `HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf, RX_BUF_SIZE)`
- 这个 API 每次都顺手打开 DMA 半传输中断，缓冲区填一半（32 字节）会误触发回调，必须紧跟一句 `__HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT)` 关掉
- 回调 `HAL_UARTEx_RxEventCallback(huart, Size)` 里的 `Size` 就是本帧真实字节数，由 HAL 用「缓冲大小 − DMA 剩余计数」算出
- 回调里只做四件事：`memcpy` 搬走、置 `volatile` 标志、重挂接收、返回。**绝不在中断里打印**，回显放在主循环
- `HAL_UART_ErrorCallback` 里清溢出标志并重挂接收，否则一次溢出会让串口永久哑掉

已知限制：缓冲区 64 字节，超长会在填满时先交一次回调、剩余部分算下一帧；主循环未处理完而新帧到达会覆盖 `frame_buf`，工程上需要双缓冲或环形队列。

## TIM：定时器计时 + 串口上报

TIM4 自由计数，主循环把 CNT 的增量累加成真实毫秒，每 100 ms 通过 USART1 打印一行。串口助手 115200，输出形如：

```
TIM4 timer started
CNT=  999  elapsed=  100 ms
CNT= 1999  elapsed=  200 ms
CNT=   58  elapsed= 6554 ms
```

`CNT` 会从 65535 翻回 0，而 `elapsed` 继续增长 —— 这是判断"回绕处理正确"的直接证据。

CubeMX 配置：TIM4 `Clock Source = Internal Clock`，`Prescaler = 7199`，`Period` 用默认 65535。TIM4 挂在 APB1（分频 /2 后定时器时钟仍为 72 MHz），所以 72 MHz ÷ 7200 = 10 kHz，**一个计数 = 0.1 ms**，CNT 从 0 数到 65535 约 6.55 秒一轮。

实现要点：

- **不能直接打印 CNT**：它只反映当前这一轮的位置，到顶就归零，不是时间
- 每圈主循环取一次增量并累加：`delta = (uint16_t)(now - last)`。无符号相减天然跨过回绕，不需要 `if (now < last)` 分支
- 用 `HAL_UART_Transmit` 而不是 `HAL_UART_Transmit_IT`：后者异步返回，同一个 `sprintf` 缓冲区被下一轮覆写会发出残缺数据，且 `HAL_BUSY` 时静默丢帧
- 打印带 `\r\n`。缺了它串口助手里所有内容挤成一条不断刷新的长串，看起来像"没有输出"
- `#include <stdio.h>` 必须写在 `USER CODE BEGIN Includes` 内，否则重新生成代码时被清掉

三个曾经踩过的坑都在这份代码里修正了：无换行、单位不是时间、异步发送复用缓冲区。

## xunjiTIM：定时器外部时钟 + 传送带测速

传送带上铺一圈黑白相间的条纹，循迹模块（TCRT5000 一类）架在条纹上方。黑白交替时模块的 `DO` 引脚输出方波，把方波接到 **PA15**，配置成 **TIM2 的外部时钟（ETR 模式 2）** —— 此时 TIM2 不再数内部 72 MHz，而是**每来一个上升沿 `CNT` 加 1**，计数完全由硬件完成，CPU 不用管。

主循环每 500 ms 取一次 `CNT` 增量，就能算出这段时间走过几个条纹，进而得到速度和累计距离。

CubeMX 关键配置：

| 项目 | 设置 |
|---|---|
| TIM2 → Clock Source | **ETR2**（不是 Internal Clock） |
| TIM2 → Prescaler / Counter Period | `0` / `65535` |
| ETR2 → Clock Prescaler / Polarity | `/1` / Non Inverted |
| ETR2 → **Clock Filter** | `15`（消抖，见下） |
| PA15 | `TIM2_ETR`，GPIO **Pull-up** |
| I2C1 | PB6=SCL、PB7=SDA、100 kHz（OLED） |
| SYS → Debug | Serial Wire（PA15 同时是 JTDI，关掉 JTAG 才能当 ETR 用） |

板子上 TIM2_CH1_ETR 默认在 PA0，本工程 PA0 被 ADC 占用，CubeMX 自动做了 **TIM2 部分重映射 1**（`__HAL_AFIO_REMAP_TIM2_PARTIAL_1()`），把 ETR 挪到 PA15。

OLED 界面（ASCII 大字 + 小字）：

```
SPEED   mm/s  SIM
  832.0
PULSE 1042
LEN 20840 mm
```

换算关系：

```c
delta       = (uint16_t)(now_cnt - last_cnt);          /* 本窗口脉冲数 */
speed_01mms = delta * STRIPE_MM * 10000 / WIN_MS;      /* 单位 0.1 mm/s */
distance_mm = total_pulse * STRIPE_MM;
```

`STRIPE_MM` 是一条"黑+白"对应的传送距离（毫米），用尺子量出来填进去；这个数错了速度会整体成比例偏大/偏小，但趋势一定正确。

实现要点：

- **不需要开 TIM2 更新中断**。教程用中断数溢出次数，但 `(uint16_t)(now - last)` 无符号相减本身就能跨过 65535 回绕，只要一个窗口内脉冲数小于 65536（约 13 万脉冲/秒，循迹模块远远到不了）就永远正确
- **Clock Filter 的作用**：滤波器按固定采样率连续检验 N 次，窄于真实脉宽的毛刺被丢弃。72 MHz 下 `Filter = 15` 约对应 3.5 µs 最小脉宽，传送带速度完全够用
- **避免浮点**：速度用整数存 0.1 mm/s，显示时拆成 `speed/10` 和 `speed%10` 两段拼接
- **有模拟模式**：`SIM_PULSE 1` 时用内部三角波造脉冲（20~80 脉冲/秒），没有传感器也能验证显示、换算和刷新节奏；拿到模块后改成 `0` 即可，其它代码不用动
- **中文字库只有 4 个字**：`font16x16` 里仅"波特律动"四个汉字，所以界面标签全用 ASCII。要显示中文得用[取模助手](https://led.baud-dance.com)生成字模补进 `font.c` 的 `zh16x16[]`（4 字节 UTF-8 + 32 字节点阵）
- `oled.c` / `font.c` 是手写文件，CubeMX 不知道它们的存在，必须在**顶层** `CMakeLists.txt` 的 `target_sources` 里登记，否则链接报 `undefined reference to OLED_Init`

已知限制：一个窗口内脉冲超过 65536 时增量会被截断（需要改累计方式）；`STRIPE_MM` 与实际不符会带来固定比例误差。

## TIM_SlaveMode：定时器从模式（复位模式）

和 `xunjiTIM` 的"外部时钟"是两条不同的路：这里 TIM2 **仍然用内部 72 MHz 时钟**数数，但把外部信号接到 **TI1FP1（PA15）**，配置成从模式 **Reset（复位模式）** —— 外部信号**每来一个上升沿就把 CNT 清零**。于是 CNT 里存的不是"总共来了多少脉冲"，而是"**两个上升沿之间过了多少个内部计数**"，也就是外部信号的周期（测周法）。

CubeMX 配置：

| 项目 | 设置 |
|---|---|
| TIM2 → Clock Source | Internal Clock |
| TIM2 → Prescaler / Counter Period | `8000-1` / `5000-1`（72 MHz÷8000 = **9 kHz**，一个计数 ≈ 0.111 ms） |
| TIM2 → Slave Mode | **Reset Mode** |
| TIM2 → Trigger Source | **TI1FP1** |
| Trigger Polarity / Filter | Rising / `15` |
| PA15 | `TIM2_CH1`，GPIO Pull-up |
| NVIC | `TIM2 global interrupt` 使能 |
| USART1 | 115200 / 8N1，PA9=TX、PA10=RX |

当前代码行为：更新中断里发一串中文，主循环每 500 ms 打印一次 `counter: N`。周期换算：`t = CNT / 9000` 秒，`f = 9000 / CNT` Hz。

注意几点：

- 更新频率实际是 72 MHz ÷ 8000 ÷ 5000 = **1.8 Hz**（约 556 ms 一次），不是 1 秒。要正好 1 秒，`Period` 填 `9000-1`
- 中断回调里用 `HAL_UART_Transmit` 是**阻塞发送**，33 字节在 115200 下要 3 ms 左右，期间中断被占着；而且它和主循环的发送共用 `huart1`，两边同时发会互相打断。练手可以，正式代码应该在中断里置标志、主循环再发
- `sprintf(message, "counter: %d")` 结尾**没有 `\r\n`**，串口助手里会挤成一条不断刷新的长串，看着像"输出不正常"
- 发中文和 emoji 时串口助手显示乱码是正常的 —— 它们是 UTF-8 多字节，串口助手按 GBK 解码就对不上，不是代码有问题

## PWM_Light：PWM 呼吸灯

TIM3 的通道 1 输出 PWM 到 **PA6**，主循环把比较值从 0 扫到 99 再扫回来，每档停留 10 ms，灯就呈现出约 2 秒一呼一吸的效果。

CubeMX 配置：

| 项目 | 设置 |
|---|---|
| TIM3 → Channel1 | **PWM Generation CH1** |
| PA6 | `TIM3_CH1` |
| Prescaler / Counter Period | `72-1` / `100-1` |
| Pulse（初始占空比） | `50` |
| 时钟 | APB1 定时器时钟 72 MHz |

换算：72 MHz ÷ 72 = **1 MHz** 计数频率，再数 100 个 → PWM 频率 **10 kHz**，占空比 = `CCR / 100`，一共 100 档。10 kHz 远高于人眼闪烁感知，所以看到的是亮度连续变化而不是闪。

代码只有两处：

```c
/* USER CODE BEGIN 2 */
HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);   /* 必须启动，否则引脚没波形 */

/* 主循环里改占空比 */
__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, i);   /* i = 0~99 */
```

要点：

- `HAL_TIM_PWM_Start` 只调一次，**改亮度不需要停/启定时器**，直接写 CCR 立刻生效（`__HAL_TIM_SET_COMPARE` 就是写 `TIMx->CCR1`）
- 分辨率和 PWM 频率是一对矛盾：`Period` 越大档位越细，但 72 MHz ÷ Prescaler ÷ Period 会变低。现在 100 档 / 10 kHz 是折中值
- 真正的"呼吸"用线性扫 CCR 效果偏硬，人眼对亮度是对数感知的，想要更自然可以按平方/查表来扫

⚠️ **PA6 不是板载 LED**。蓝色药丸的板载灯在 **PC13**，而且没有接到任何定时器通道上，改 PWM 占空比对它无效。要用这个工程，得外接一颗 LED：

```
PA6 ── 220Ω~1kΩ ──|>|── GND     （LED 长脚接电阻，短脚接 GND）
```

或者把板载灯那侧当作"只验证代码跑通"，想看效果还是外接一颗。ADC1 在 `.ioc` 里仍勾着（PA0），本工程没用到，可忽略。

## TIM_Encoder：旋转电位器 → LED 亮灭 + OLED 显示

对应 B 站「旋转控制小灯」那一集。**注意：这套代码是给「旋转电位器模块」（3 脚 OUT/VCC/GND，丝印 `Rotation Sensor`）写的，不是给旋转编码器（EC11，A/B 两相脉冲）写的。**

| 项目 | 配置 |
|---|---|
| 模块供电 | VCC → **3V3**（不能接 5V，OUT 会超过 3.3V 击穿 PA0）、GND → GND |
| 信号 | OUT → **PA0**（ADC1_IN0） |
| 板载 LED | **PC13**，低电平点亮，代码里手动初始化（CubeMX 勾不到这个脚） |
| 外接 LED（可选） | PA6 / PA7 / PB0（TIM3 CH1/CH2/CH3）任选一路，串 220Ω~1kΩ 到 GND |
| OLED | I2C1：PB6=SCL、PB7=SDA，地址 `0x7A`（CH1116） |

**CubeMX 关键配置**

| 外设 | 设置 | 说明 |
|---|---|---|
| ADC1 | IN0，Software Start，单次转换 | 采样时间代码里改成 55.5 周期（见下） |
| TIM3 | PWM Generation CH1/CH2/CH3，Prescaler 72-1，Period 100-1 | 10 kHz、100 档，占空比 = CCR/100 |
| I2C1 | I2C，Standard Mode 100 kHz | OLED 走这条总线 |
| SYS | Debug = **Serial Wire** | 千万别选 No Debug，会把 SWD 关掉锁死 |

**代码结构**

| 锚点 | 行号 | 内容 |
|---|---|---|
| `USER CODE BEGIN PD` | 39–53 | LED 引脚、50%/47% 双阈值、8 次平均、50 ms 刷新 |
| `USER CODE BEGIN PV` | 62–65 | `led_on`、`ui_last_tick` |
| `USER CODE BEGIN 0` | 74–198 | `led_gpio_init` / `adc_read_avg` / `adc_to_percent` / `ui_draw` / `app_task` |
| `USER CODE BEGIN 2` | 233–249 | 初始化 OLED、启动三路 PWM、先画一帧 |
| `USER CODE BEGIN 3` | 257–265 | 每 50 ms 调一次 `app_task` |
| `adc.c` 的 `USER CODE ADC1_Init 2` | — | 采样时间 1.5 → 55.5 周期 |

**三个关键点**

1. **迟滞比较**：`>=50%` 点亮、`<=47%` 熄灭。少了 3% 的死区，旋钮停在 50% 附近时读数在 4090/4100 之间抖，LED 就会疯狂闪烁。
2. **采样时间要够长**：电位器分压后源阻抗最大约 2.5 kΩ，`1.5` 个 ADC 周期（12 MHz 下 125 ns）采样电容充不满，读数会偏低且不稳。`55.5` 周期（约 4.6 µs）才是常规选择。
3. **不接外部元件也能验证**：PC13 是板载灯，转动旋钮过 50% 就会亮；OLED 上同时显示百分比、进度条、ADC 原始值和 `LED ON/OFF`。TIM3 的三路 PWM 只是额外送的调光输出，不接也没关系。

> `.ioc` 里还留着 **TIM1 编码器模式（PA8/PA9）**，那是给 EC11 用的，本模块用不上。留着无害（只是白占两个脚，其中 PA9 还是 USART1_TX），想干净就在 CubeMX 里取消 TIM1 后重新生成。

## 编译

```bash
cd <工程目录>
cmake --preset Debug
cd build/Debug && ninja
```

## 烧录

```powershell
& "C:\ST\STM32CubeCLT_1.18.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" `
  -c port=SWD -w "<工程目录>\build\Debug\xxx.bin" 0x08000000 -rst
```

CLion 里直接点调试按钮也可以。

## 踩坑记录

**1. `SYS → Debug` 选成 `No Debug` 会把下载口锁死**

CubeMX 生成 `__HAL_AFIO_REMAP_SWJ_DISABLE()`，上电几微秒内 JTAG 和 SWD 一起关闭，之后 ST-Link 报 `Unable to get core ID`。正确选项是 **`Serial Wire`**，生成 `__HAL_AFIO_REMAP_SWJ_NOJTAG()`，只关 JTAG 保留 SWD。

救援方法：BOOT0 跳线帽从 `0` 挪到 `1`，按一次复位，芯片进入内置 bootloader，此时 SWD 必然可用；擦除重烧后**记得把帽子插回 `0`**。

**2. ST-Link 固件过旧**

`V2J37S7` 会被 ST-Link GDB Server 7.x 拒绝，报 `ST-LINK firmware upgrade required`。用 `STLinkUpgrade.jar` 升级到 `V2J46S7` 后恢复。

**3. `while(1)` 重复嵌套**

粘贴代码时容易把整个 `while(1) { ... }` 连同右括号一起带进来，表现为 `invalid storage class for function 'MX_GPIO_Init'`。注意这类代码编译能过，但用户逻辑在死循环里永远执行不到。

**4. PC13 是低电平点亮**

和常见开发板相反，写代码时 `SET` / `RESET` 要注意，否则会出现"一直亮不闪"。
