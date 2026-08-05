# ESC_407SPI_SSC_v6_RT_2MS

## 目标

在已上板验证的 v4 SM-Sync + AL Event IRQ 基线上，加入 LAN9252 Distributed Clocks 的 SYNC0/SYNC1 GPIO 接入。

本版重点验证：

- LAN9252 AL Event IRQ -> PC0
- LAN9252 SYNC0 -> PC3
- LAN9252 SYNC1 -> PC1
- 三路硬件 ISR 都只排队/唤醒线程
- `PDI_Isr()`、`Sync0_Isr()`、`Sync1_Isr()` 全部在同一个 SSC 高优先级线程中执行
- TwinCAT 可在 SM-Sync 与 DC-Sync0 两种模式之间选择

> v5 是 DC 实验分支。v4 已经是稳定的 SM-Sync 基线，测试 v5 前请保留 v4 工程和固件。

## 硬件接线

厂家 STM32F407 例程给出的连接：

```text
LAN9252 IRQ   -> STM32 PC0（低有效）
LAN9252 SYNC0 -> STM32 PC3（低有效脉冲）
LAN9252 SYNC1 -> STM32 PC1（低有效脉冲）
```

本阶段 TwinCAT ESI 的 DC 模式使用 `AssignActivate = 0x0300`，核心是 SYNC0。PC1/SYNC1 同时接入诊断，当前模式下可能没有脉冲。

## 架构

```text
PC0/PC3/PC1 GPIO ISR
        -> 增加 pending 计数
        -> 释放公共信号量
        -> SSC 高优先级线程
             1. PDI_Isr()
             2. Sync0_Isr()/Sync1_Isr()
             3. MainLoop()
```

任何 GPIO ISR 都不会直接访问 SPI，也不会在中断上下文执行 SSC 协议栈。

## 主要修改

- `DC_SUPPORTED = 1`
- `MIN_PD_CYCLE_TIME = 1 ms`（适配 RT-Thread 延迟执行模型）
- 新增 `applications/ssc/port/ssc_dc_sync.c/.h`
- CoE `0x1C32/0x1C33:04` 增加 DC-Sync0/DC-Sync1 支持位
- ESI 增加 `DC-Synchron (SYNC0)` 模式
- 修复 SSC 5.11 中 `0x1C33:0A` 的 Sync0 周期被 `UINT16` 截断的问题
- 新增 `ssc_dc_status` 诊断命令

## 编译与烧录

1. 导入 `ESC_407SPI_SSC_v6_RT_2MS`。
2. Refresh。
3. Project -> Clean。
4. Build Project。
5. 烧录并复位。

预期启动：

```text
[SSC] HW_Init OK, IRQ=PC0, SYNC0=PC3, SYNC1=PC1 active-low
[SSC] stack started, state=INIT ..., mode=SM/DC deferred ...
```

## TwinCAT

将工程中的 ESI：

```text
applications/ssc/doc/LAN9252-EVB-SPIXML.xml
```

复制到 TwinCAT ESI 目录，重启 Visual Studio/TwinCAT，删除旧 Box 后重新扫描。

先验证 SM-Sync 仍能进入 OP，再在 Box 的 **DC** 页选择 `DC-Synchron (SYNC0)`。建议第一轮保持当前 4 ms EtherCAT 周期，不要直接降到 1 ms。

## 成功状态

执行：

```text
ssc_status
ssc_dc_status
```

DC 模式下应看到：

```text
AL state     : OP
AL code      : 0x0000
Sync output  : DC-Sync0 (0x0002)
Sync input   : DC-Sync0 (0x0002)
DC sync      : active
SYNC0 edges  : 持续增加
SYNC0 services: 与 edges 基本一致
SYNC0 queue pending: 通常为 0
HW errors    : 0
```

`SYNC1 edges` 在 `AssignActivate 0x0300` 下为 0 属于正常现象。

## 快速故障定位

- DC 模式无法进入 SAFEOP/OP，`SYNC0 edges = 0`：检查 LAN9252 SYNC0 是否确实接到 PC3。
- `AL code 0x0036`：TwinCAT 配置的 Sync0 周期小于当前声明的 1 ms，先改为 4 ms。
- `AL code 0x0034` 或运行后退 SAFEOP：SYNC0 脉冲未持续到达，检查 DC 选择、PC3 接线和计数。
- `SYNC0 pending/max` 持续增加：SSC 线程无法在一个周期内处理完事件，应提高 SPI 频率或放宽 EtherCAT 周期。
- SM-Sync 正常、DC 异常：立即回退 v4，说明 SPI/SSC 主链路没有问题，故障范围仅在 SYNC0/DC。

## 当前验证范围

已完成静态代码检查和 XML 解析检查。目标板编译、PC3/PC1 实际脉冲与 TwinCAT DC 进入 OP 需要上板验证。
