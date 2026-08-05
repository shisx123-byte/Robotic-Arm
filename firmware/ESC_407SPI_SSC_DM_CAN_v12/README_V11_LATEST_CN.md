# v11 最新周期状态模型

基于 `ESC_407SPI_SSC_v10_CATCHUP`，保留 V10 工程不变。

## V10 实测结论

100 us 连续运行 30 分钟无 AL、SPI 和队列溢出错误，但：

- SYNC0 edge：17990888
- SYNC0 service：12068100
- SYNC0 dropped：5922788
- MainLoop：197684（约 110 Hz）

V10 通过批量回放加队列裁剪保持 OP，但没有实现严格的逐周期处理。

## V11 修改

1. 删除 128 槽事件历史回放模型。
2. ISR 只累计 AL、SYNC0、SYNC1 次数并保存最新序号和时间戳。
3. SSC 每次唤醒原子取得状态快照，每类事件最多服务一次。
4. 同类多余事件计为 coalesced，不再回放历史。
5. 最新 AL 与 SYNC0 按最新序号保持相对顺序。
6. AL IRQ 每轮最多执行一次 PDI_Isr；若电平仍低，安排下一轮处理。
7. MainLoop 独立按 1 ms tick 执行，不随 AL 事件重复运行。
8. TIM5 配置为 1 MHz 32 位自由运行计数器，替代失效的 DWT。
9. 保持 V10 已验证的 SPI1 42 MHz。

## 新诊断

`ssc_stat`输出：

- 完整轮次 max/average 和 over100us
- PDI、SYNC0、SYNC1、MainLoop max/average
- AL、SYNC0、SYNC1最大服务延迟
- pending 当前值/最大值
- AL/SYNC0 edge、service、coalesced
- SPI故障及永久AL错误快照

## 验证顺序

1. 1 ms，5分钟
2. 250 us，5分钟
3. 200 us，10分钟
4. 100 us，5分钟
5. 100 us，30分钟

100 us严格通过条件：

- AL error = 0
- SPI fault/timeout = 0
- SYNC0 coalesced = 0
- 完整轮次最大耗时 < 100 us
- MainLoop接近1000 Hz
