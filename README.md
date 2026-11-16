# STM32F103C8T6 学习工程集

蓝色药丸（STM32F103C8T6 最小系统板）的裸机练习代码，工具链为 **STM32CubeMX 6.15 + CLion + CMake + arm-none-eabi-gcc**。

## 目录结构

```
STM32/
├── STM32中断/       外部中断练习：PC13 LED + PA0 按键，四档闪烁状态机
├── helloworldsent/  串口双向通信：USART1 轮询发送 Hello World + 接收命令控制闪灯
├── interruptsent/   中断版串口控制：USART1 接收中断 + 四档闪烁 + 定时翻转
├── LED/             点灯入门（第一版）
└── lighting/        点灯入门（第二版）
```

五个工程各约 283 个受版本控制的文件。构建产物（`build/`、`cmake-build-*/`）与 CMSIS 中本工程未使用的 `DSP` / `Lib` / `NN` / `RTOS` / `Core_A` / `docs`（合计约 50MB）均已排除，clone 后可直接编译。

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
