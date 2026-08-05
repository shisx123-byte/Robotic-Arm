# v9 IgH DC 优化说明

基于 `ESC_407SPI_SSC_v8_ORDERED_DC`，保留 v8 原工程不变。

## 修改

1. AL IRQ 为低有效电平信号。一次 `PDI_Isr()` 可能已经清除多个排队事件；
   当 IRQ 已恢复高电平时，跳过陈旧的 AL 下降沿，避免无意义的 SPI 访问推迟
   SYNC0 服务。
2. 仅在 SYNC0/SYNC1 事件成功进入公共事件队列后增加 pending 计数，避免极端
   队列溢出后 pending 永久虚高。

## 已确认事项

- 当前源码及 `Debug/rtthread.bin` 中 0x1A02 最后一项均为
  `0x6020:11/16`。
- IgH 在线日志曾报告从站实际映射为 `0x6020:0B/16`，因此板上固件与当前
  v8 输出不一致。烧录 v9 后应首先确认该 PDO 警告消失。
- ESC 寄存器 0x092C 的系统时间差由 LAN9252 Distributed Clocks 硬件及
  EtherCAT 主站校时控制。STM32 SSC 只消费 SYNC 引脚，不直接调整该时钟。
