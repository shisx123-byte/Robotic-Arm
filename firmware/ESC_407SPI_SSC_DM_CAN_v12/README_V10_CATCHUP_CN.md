# v10 100 us 事件追赶优化

基于 `ESC_407SPI_SSC_v9_IGH_DC`，保留 v9 工程不变。

## 已确认的 v9 故障

100 us 长测的错误快照显示：

- AL 状态码 `0x002C`（Fatal Sync Error）
- `SM missed = 0`
- `cycle_exceeded = 33940`
- `SYNC0 edge = 2367160`
- `SYNC0 service = 897524`
- 事件队列达到 127，累计溢出 1965935 次
- SPI fault 和 command timeout 均为 0

故障是延迟执行线程回放大量过期 AL/SYNC 边沿形成的积压雪崩，不是 LAN9252 SPI 通信错误。

## v10 修改

1. SPI1 从 21 MHz 提升到 42 MHz。
2. 事件队列深度达到 8 时进入追赶模式，只保留最新 2 个有序事件。
3. AL IRQ 继续按低有效电平补服务，因此丢弃陈旧 AL 边沿不会丢失当前有效电平事件。
4. 被裁剪的 SYNC0/SYNC1 同步扣减 pending 计数。
5. `ssc_stat` 增加 catchup 次数及 AL/SYNC0/SYNC1 丢弃统计。
6. 错误快照增加故障发生时的追赶统计。
7. MainLoop 维持 v9 修复后的 RT-Thread tick 1 kHz 门控。

## 首轮验证

先以 1 ms 周期验证 42 MHz SPI 信号和基本 OP，再依次测试 250 us、200 us 和 100 us。

100 us 长测要求重点观察：

```text
queue max
queue overflow
catchup count
dropped SYNC0
SYNC0 edge/service
AL code
cycle_exceeded
```

追赶计数允许低频增长，但队列不应长期满载，overflow 应保持 0，且不得再次出现 `0x002C`。
