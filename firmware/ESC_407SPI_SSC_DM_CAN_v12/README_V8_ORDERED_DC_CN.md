# v8 ORDERED DC：2 ms ERR OP 定位与修复版

## v7 中发现的结构性问题

v7 将 AL/SM2、SYNC0、SYNC1 分别存入三个计数器，线程醒来后按固定顺序处理：

```
全部 AL -> 全部 SYNC0 -> 全部 SYNC1
```

当线程偶尔积压两个总线周期时，真实顺序：

```
AL1 -> SYNC0_1 -> AL2 -> SYNC0_2
```

会被重排为：

```
AL1 -> AL2 -> SYNC0_1 -> SYNC0_2
```

SSC 5.11 使用 SM2/SYNC0 的先后关系判断同步状态，这种重排可能人为增加
`0x1C32:11` 的 SM Event Missed Counter，最终触发 AL Status Code `0x001A`。

## v8 修改

- 使用 128 项固定环形队列保存 AL、SYNC0、SYNC1 的真实到达顺序。
- ISR 只写入 8 字节事件并释放信号量，不访问 SPI。
- SSC 线程严格按队列顺序调用 `PDI_Isr/Sync0_Isr/Sync1_Isr`。
- 保留 AL IRQ 低电平兜底，避免边沿合并后事件悬空。
- 分别统计 PDI、SYNC0、SYNC1、MainLoop 的最大耗时和最大调度延迟。
- 在 `SetALStatus()` 写入错误状态之前永久锁存错误码和现场，TwinCAT 自动恢复 OP 后仍能查看。

## 调试器重点变量

错误根因：

```c
g_ssc_al_error_count
g_ssc_last_error_al_code
g_ssc_last_error_al_status
g_ssc_last_error_sm_missed
g_ssc_last_error_cycle_exceeded
g_ssc_last_error_sync_flag
g_ssc_last_error_queue_depth
g_ssc_last_error_queue_max_depth
g_ssc_last_error_queue_overflow
```

性能与调度延迟：

```c
g_ssc_pdi_max_cycles
g_ssc_sync0_max_cycles
g_ssc_mainloop_max_cycles
g_ssc_pdi_max_latency_cycles
g_ssc_sync0_max_latency_cycles
g_ssc_service_max_cycles
g_ssc_service_over_budget
```

STM32F407 168 MHz：

```
时间(微秒) = cycles / 168
```

判定：

- `last_error_al_code == 0x001A`：SM2/SYNC0 序列错误。
- `last_error_al_code == 0x001B`：主站过程数据看门狗。
- `last_error_al_code == 0x002C`：SYNC0 看门狗/严重同步丢失。
- `queue_overflow` 必须始终为 0。
- `sync0_max_latency_cycles / 168` 建议低于 100 us。

## TwinCAT 测试配置

- Task cycle：2 ms
- DC：DC-Sync0
- Sync0 cycle：2,000,000 ns
- Shift：先用 500,000 ns；若错误码为 0x001A，再测试 1,000,000 ns
