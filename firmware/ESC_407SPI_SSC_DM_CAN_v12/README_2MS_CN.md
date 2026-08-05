# ESC_407SPI_SSC_v6_RT_2MS

面向 **STM32F407ZE + LAN9252（SPI PDI）+ RT-Thread 4.1.0 + SSC 5.11** 的 2 ms DC-Sync0 实时测试工程。

## 这个版本做了什么

1. EtherCAT SSC 使用静态线程，优先级为 `2`，时间片为 `1`。
2. 关闭 FinSH/MSH、软件定时器线程、内核调试、Hook、Event、Mailbox、MessageQueue 等无关功能。
3. 编译优化从 `-O0` 改为 `-O2`。
4. LAN9252 不再经过 RT-Thread SPI 设备框架和总线互斥锁，改为 SPI1 独占 HAL 轮询。
5. SPI1 默认约为 `10.5 MHz`：APB2 84 MHz，预分频 `/8`。
6. 删除原来的每次 CSR/PRAM Busy 查询后 `10 us` 延时，改为紧凑轮询。
7. LAN9252 IRQ、SYNC0、SYNC1 只在 GPIO ISR 中累计事件并唤醒 SSC 线程，所有 SPI/SSC 调用仍由同一个线程执行。
8. 多个 IRQ/SYNC 边沿合并成一次线程唤醒，避免信号量积压造成无效调度。
9. EXTI0、EXTI1、EXTI3 中断优先级设为 `2`；USART3 降到 `12`。
10. 去除状态切换期间的串口长打印，避免 115200 波特率输出阻塞数毫秒。
11. 使用 DWT 记录 SSC 单次服务耗时，不创建统计线程，也不周期打印。

## 烧录后的正常现象

本工程关闭了 FinSH/MSH。串口仍可输出初始化失败或硬件错误，但终端不能输入 `ssc_status` 等命令，这是有意设计，不是终端被占用。

## RT-Thread Studio 使用

1. 解压工程。
2. 在 RT-Thread Studio 中选择“导入已有 RT-Thread Studio 工程”。
3. 选择文件夹 `ESC_407SPI_SSC_v6_RT_2MS`。
4. 执行 Clean，然后 Build。
5. 使用原来的 J-Link/ST-Link 配置下载。
6. 建议第一次下载前执行一次整片擦除，避免旧固件或旧配置干扰。

## TwinCAT 2 ms 测试

1. 使用与当前 SSC 对象字典匹配的 ESI 文件，工程内副本位于：
   `applications/ssc/doc/LAN9252-EVB-SPIXML.xml`
2. TwinCAT 任务周期设为 `2 ms`。
3. 从站同步方式选择 `DC-Sync0`。
4. Sync0 Cycle Time 应为 `2,000,000 ns`。
5. 激活配置并切到 Run。
6. 检查从站是否稳定进入 OP，PDO 的 LED/KEY 是否持续刷新。

不要一开始同时测试 1 ms。先确认 2 ms 连续运行，再逐步压缩周期。

## DWT 实时统计

可在 RT-Thread Studio Debug 的 Watch 中观察：

- `g_ssc_service_count`：SSC 服务次数。
- `g_ssc_service_max_cycles`：历史最大服务耗时，单位为 CPU Cycle。
- `g_ssc_service_over_budget`：服务时间超过 `1500 us` 的次数。

STM32F407 当前主频为 168 MHz，因此：

`最大耗时(us) = g_ssc_service_max_cycles / 168`

2 ms 测试时建议：

- `g_ssc_service_over_budget` 长时间保持为 0。
- 最大耗时尽量小于 1000 us。
- 如果从站掉出 OP，但统计值很低，应重点检查 TwinCAT 主站任务抖动、CPU 隔离和网卡驱动，而不是继续修改从站线程。

## SPI 稳定性调整

默认配置在 `drivers/lan9252.c`：

```c
#define LAN9252_SPI_PRESCALER SPI_BAUDRATEPRESCALER_8
```

如果 10.5 MHz 下出现 Byte Test 错误、随机 SPI 读写错误或短杜邦线接触不稳定，可先改为：

```c
#define LAN9252_SPI_PRESCALER SPI_BAUDRATEPRESCALER_16
```

此时 SPI 约为 5.25 MHz。不要在没有 SPI 错误证据时主动降速，因为较慢 SPI 会压缩 2 ms 周期余量。

## 保留的硬件引脚

- LAN9252 SPI1：PA5/PA6/PA7
- LAN9252 CS：PA8
- LAN9252 IRQ：PC0
- LAN9252 SYNC0：PC3
- LAN9252 SYNC1：PC1
- USART3：PC10/PC11
- LED/KEY PDO：保持 v5 工程原映射不变

## 注意

这个版本用于验证高实时 EtherCAT 路径，不适合作为带 Shell、日志、网络协议栈和多业务线程的通用 RT-Thread 模板。验证 2 ms 稳定后，再逐项加入业务线程，并确保其优先级低于 SSC 线程。
