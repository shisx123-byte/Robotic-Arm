# v7_RT_FAST：厂家代码对比后的 2 ms 实时优化版

## 最关键差异

厂家裸机例程的优势不是 SSC 协议栈不同。两边绝大部分 SSC 5.11 源码完全相同，真正的差异在硬件适配层：

1. 厂家直接访问 SPI1 寄存器；v6 使用 HAL_SPI_Transmit/TransmitReceive。
2. 厂家 SPI1 为 21 MHz；v6 为 10.5 MHz。
3. 厂家一次 PRAM 命令传完整个 PDO，并使用 FIFO burst；v6 每 4 字节重新执行 abort、地址、busy、轮询，写入还先读后改写。
4. 厂家在 EXTI ISR 内直接运行 PDI_Isr/Sync0_Isr；v6 为避免 RTOS 与 SPI 并发，ISR 唤醒最高优先级 SSC 单线程。
5. 厂家主循环持续运行；v6 原来每 1 ms 超时唤醒一次，容易与 2 ms SYNC0 周期形成固定碰撞。
6. 厂家 GPIO 使用寄存器宏；v6 每周期多次经过 RT-Thread pin API。

## v7 已修改

- SPI 热路径改为直接寄存器轮询。
- SPI 时钟提高到 21 MHz（与厂家一致）。
- PRAM 改为一次命令 + FIFO burst，取消每 4 字节 abort 和读改写。
- CSR 单所有者路径取消多余的命令前 busy 查询。
- SSC 线程优先级改为 1；AL IRQ / SYNC0 / SYNC1 优先级为 0 / 1 / 2。
- 空闲兜底唤醒由 1 ms 改为 10 ms；OP 时仍由 IRQ/SYNC0 立即唤醒。
- 8 路 LED 和按键改为 GPIO BSRR/IDR 批量访问。
- 循环期 SPI 错误只锁存，不在实时路径打印串口。

## 调试变量

- `g_ssc_service_max_cycles`：单次 SSC 服务最大 CPU 周期。
- `g_ssc_service_over_budget`：超过 1000 us 的次数。
- `g_lan9252_spi_fault_count`：SPI 标志超时次数，必须为 0。
- `g_lan9252_command_timeout_count`：CSR/PRAM 命令超时次数，必须为 0。
- `g_hw_error_count`：SSC 硬件访问错误次数，必须为 0。
- `g_hw_last_error_address / length / code`：最近一次硬件访问错误。
- `g_sync0_max_backlog`：SYNC0 最大积压，正常应不超过 1。

STM32F407 168 MHz：`耗时 us = cycles / 168`。

## 说明

本版仍保留“单 SSC 线程拥有全部 SPI”的设计。它比在 RTOS 中直接让 PDI ISR 和 SYNC0 ISR 同时访问 SPI 更安全，同时通过 burst 和直接寄存器访问把线程服务时间尽量压短。
