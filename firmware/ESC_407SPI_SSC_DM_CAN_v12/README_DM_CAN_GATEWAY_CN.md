# EtherCAT - 达秒单电机网关（V12 原型）

本工程由 `ESC_407SPI_SSC_v11_LATEST` 复制，原工程未修改。当前目标是：

- LAN9252 SPI EtherCAT 从站；
- STM32F407 的 CAN1，经典 CAN 500 kbit/s；
- 达秒电机 ID `0x01`，反馈 ID `0x11`；
- 默认按 4340/4340P 的 MIT 量程编码：P ±12.5 rad、V ±8 rad/s、T ±28 Nm；
- EtherCAT 退出 OP、控制位急停或 20 ms 未收到新 RxPDO 时发送失能帧。

当前原型只发送 MIT 数据帧，不自动改写电机寄存器。上板前应先用现有 USB-CAN
工具确认电机 ID=0x01、反馈 ID=0x11，并已配置为 MIT 模式。

主站导入文件：
`applications/ssc/doc/DM-CAN-Gateway-1Axis.xml`。该 ESI 保持现有 EEPROM
身份（Vendor 0x9、Product 0x9252、Revision 1），因此无需先改写 LAN9252 EEPROM。

## 上板前必须确认

本板已经确认使用 CAN1：PB8=RX、PB9=TX，工程已在 `rtconfig.h` 中定义：

```c
#define DM_CAN1_USE_PB8_PB9
```

已按 `EtherCAT开发板原理图V3.pdf` 复核：收发器 U5 为 TJA1050，RS（8 脚）
直接接 GND，Vref 悬空，因此没有需要软件控制的 EN/STB；R25=120R 已跨接
CANH/CANL。若与其他带终端的节点直连，整条总线只能保留两端的两个 120R。

## PDO

RxPDO，0x1601 -> 0x7010，28 字节：

| 字节 | 类型 | 含义 |
|---:|---|---|
| 0 | UINT16 | control_word：bit0 使能，bit1 快停/失能，bit2 清错预留 |
| 2 | UINT16 | mode_flags，当前预留且只实现 MIT |
| 4 | UINT32 | sequence |
| 8 | REAL32 | position，rad |
| 12 | REAL32 | velocity，rad/s |
| 16 | REAL32 | Kp |
| 20 | REAL32 | Kd |
| 24 | REAL32 | torque，Nm |

TxPDO，0x1A00 -> 0x6010，32 字节：

| 字节 | 类型 | 含义 |
|---:|---|---|
| 0 | UINT16 | status_word |
| 2 | UINT16 | 电机状态高 4 bit |
| 4 | UINT32 | 已执行的 sequence |
| 8 | UINT32 | 反馈时间戳，us（1 ms 分辨率） |
| 12 | REAL32 | position，rad |
| 16 | REAL32 | velocity，rad/s |
| 20 | REAL32 | torque，Nm |
| 24 | UINT16 | 低字节 MOS 温度，高字节转子温度 |
| 26 | UINT16 | CAN 错误码预留 |
| 28 | UINT32 | 有效反馈计数 |

`status_word`：

- bit0 CAN ready
- bit1 EtherCAT outputs active
- bit2 motor enabled
- bit3 feedback valid
- bit4 command timeout
- bit5 feedback timeout
- bit6 CAN transmit error
- bit7 invalid command
- bit15 board CAN pin configuration missing

首次带电测试时，先保持 `control_word=0` 验证反馈和 PDO，再以低 Kp/Kd、当前位置、
零速度、零力矩置 bit0。不要在机械臂装配状态下直接给未知位置目标。
