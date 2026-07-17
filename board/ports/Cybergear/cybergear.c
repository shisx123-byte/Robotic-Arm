/**
 ****************************(C)SWJTU_ROBOTCON****************************
 * @file       cybergear.c/h
 * @brief      小米电机函数库
 * @note
 * @history
 *  Version    Date            Author          Modification
 *  V1.0.0     1-10-2023       ZDYukino        1. done
 *
 @verbatim
 =========================================================================
 =========================================================================
 @endverbatim
 ****************************(C)SWJTU_ROBOTCON****************************
 **/
#include "cybergear.h"
struct rt_semaphore rx_can0_sem;
struct rt_semaphore rx_can1_sem;
struct rt_can_msg txMsg0 = { 0 }; //can发送结构体
struct rt_can_msg txMsg1 = { 0 }; //can接收结构体
uint8_t byte[4];
float mech_pos;
#define can0_txd()      rt_device_write(can0_dev, 0, &txMsg0, sizeof(txMsg0))  //can发送函数
#define can1_txd()      rt_device_write(can1_dev, 0, &txMsg1, sizeof(txMsg1))  //can发送函数
uint32_t Type_ID;    //接收数据电机ID
MI_Motor mi_motor[2];    //预定义两个电机结构体
/**
 * @brief          浮点数转4字节函数
 * @param[in]      f:浮点数
 * @retval         4字节数组
 * @description  : IEEE 754 协议
 */
static uint8_t* Float_to_Byte(float f)
{
    unsigned long longdata = 0;
    longdata = *(unsigned long*) &f;
    byte[0] = (longdata & 0xFF000000) >> 24;
    byte[1] = (longdata & 0x00FF0000) >> 16;
    byte[2] = (longdata & 0x0000FF00) >> 8;
    byte[3] = (longdata & 0x000000FF);
    return byte;
}

/**
 * @brief          小米电机回文16位数据转浮点
 * @param[in]      x:16位回文
 * @param[in]      x_min:对应参数下限
 * @param[in]      x_max:对应参数上限
 * @param[in]      bits:参数位数
 * @retval         返回浮点值
 */
static float uint16_to_float(uint16_t x, float x_min, float x_max, int bits)
{
    uint32_t span = (1 << bits) - 1;
    float offset = x_max - x_min;
    return offset * x / span + x_min;
}
/**
 * @brief          小米电机发送浮点转16位数据
 * @param[in]      x:浮点
 * @param[in]      x_min:对应参数下限
 * @param[in]      x_max:对应参数上限
 * @param[in]      bits:参数位数
 * @retval         返回浮点值
 */

static int float_to_uint(float x, float x_min, float x_max, int bits)
{
    float span = x_max - x_min;
    float offset = x_min;
    if (x > x_max)
        x = x_max;
    else if (x < x_min)
        x = x_min;
    return (int) ((x - offset) * ((float) ((1 << bits) - 1)) / span);
}
/**
 * @brief          写入电机参数 对应模式为 ：速度，位置，扭矩
 * @param[in]      Motor:对应控制电机结构体
 * @param[in]      Index:写入参数对应地址
 * @param[in]      Value:写入参数值
 * @param[in]      Value_type:写入参数数据类型
 * @retval         none
 */
static void Set_Motor_Parameter(MI_Motor *Motor, uint16_t Index, float Value, char Value_type, rt_uint8_t device_type)
{

    if (device_type == DEVICE_OF_CAN0)
    {
        txMsg0.id = 0;
        txMsg0.id = Communication_Type_SetSingleParameter << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        txMsg0.data[0] = Index;
        txMsg0.data[1] = Index >> 8;
        txMsg0.data[2] = 0x00;
        txMsg0.data[3] = 0x00;
        if (Value_type == 'f')
        {
            Float_to_Byte(Value);
            txMsg0.data[4] = byte[3];
            txMsg0.data[5] = byte[2];
            txMsg0.data[6] = byte[1];
            txMsg0.data[7] = byte[0];
        }
        else if (Value_type == 's')
        {
            txMsg0.data[4] = (uint8_t) Value;
            txMsg0.data[5] = 0x00;
            txMsg0.data[6] = 0x00;
            txMsg0.data[7] = 0x00;
        }
        can0_txd();
    }
    else if (device_type == DEVICE_OF_CAN1)
    {
        txMsg1.id = 0;
        txMsg1.id = Communication_Type_SetSingleParameter << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        txMsg1.data[0] = Index;
        txMsg1.data[1] = Index >> 8;
        txMsg1.data[2] = 0x00;
        txMsg1.data[3] = 0x00;
        if (Value_type == 'f')
        {
            Float_to_Byte(Value);
            txMsg1.data[4] = byte[3];
            txMsg1.data[5] = byte[2];
            txMsg1.data[6] = byte[1];
            txMsg1.data[7] = byte[0];
        }
        else if (Value_type == 's')
        {
            txMsg1.data[4] = (uint8_t) Value;
            txMsg1.data[5] = 0x00;
            txMsg1.data[6] = 0x00;
            txMsg1.data[7] = 0x00;
        }
        can1_txd();
    }
    rt_thread_mdelay(10);
}

/**
 * @brief          提取电机回复帧扩展ID中的Type
 * @param[in]      CAN_ID_Frame:电机回复帧中的扩展CANID
 * @retval         Type mode
 */
uint32_t Get_Type_Mode(uint32_t CAN_ID_Frame)
{
    return (CAN_ID_Frame & 0xFFFFFFFF) >> 24;
}
/**
 * @brief          提取电机回复帧扩展ID中的电机CANID
 * @param[in]      CAN_ID_Frame:电机回复帧中的扩展CANID
 * @retval         电机CANID
 */
uint32_t Get_Motor_ID(uint32_t CAN_ID_Frame)
{
    return (CAN_ID_Frame & 0xFFFF) >> 8;

}
float ieee_4byte_to_float(const uint8_t bytes[4])
{
    // 使用内存拷贝将4字节数组转为float（IEEE 754标准）
    float value = 0;
    memcpy(&value, bytes, sizeof(float));
    return value;
}
/**
 * @brief          电机回复帧数据处理函数
 * @param[in]      Motor:对应控制电机结构体
 * @param[in]      DataFrame:数据帧
 * @param[in]      IDFrame:扩展ID帧
 * @retval         None
 */
void Motor_Data_Handler(MI_Motor *Motor, uint8_t DataFrame[8], uint32_t IDFrame)
{
    Motor->Angle = uint16_to_float(DataFrame[0] << 8 | DataFrame[1], MIN_P, MAX_P, 16);
    Motor->Speed = uint16_to_float(DataFrame[2] << 8 | DataFrame[3], V_MIN, V_MAX, 16);
    Motor->Torque = uint16_to_float(DataFrame[4] << 8 | DataFrame[5], T_MIN, T_MAX, 16);
    Motor->Temp = (DataFrame[6] << 8 | DataFrame[7]) * Temp_Gain;
    Motor->error_code = (IDFrame & 0x1F0000) >> 16;
    if (Motor->error_code == 1)
    {
        rt_kprintf("Error Code:0x%x\n", Motor->error_code);
        rt_pin_write(LED_PIN_G, PIN_HIGH); //电机出现异常，关闭rgb green
    }
}
/**
 * @brief          电机回复帧数据处理函数
 * @param[in]      Motor:对应控制电机结构体
 * @param[in]      DataFrame:数据帧
 * @param[in]      IDFrame:扩展ID帧
 * @retval         None
 */
void MechPos_Data_Handler(MI_Motor *Motor, uint8_t DataFrame[8], uint32_t IDFrame)
{
    rt_uint8_t dataFrame[4] = { 0 };
    dataFrame[0] = DataFrame[4];
    dataFrame[1] = DataFrame[5];
    dataFrame[2] = DataFrame[6];
    dataFrame[3] = DataFrame[7];
    if ((DataFrame[1] << 8 | DataFrame[0]) == 0X7019)
    {
        Motor->MechPos = ieee_4byte_to_float(dataFrame);
    }
    if ((DataFrame[1] << 8 | DataFrame[0]) == 0X701B)
    {
        /* code */
        Motor->MechVel = ieee_4byte_to_float(dataFrame);
    }
    if ((DataFrame[1] << 8 | DataFrame[0]) == 0X7006)
    {
        Motor->Iq_ref = ieee_4byte_to_float(dataFrame);
    }

}
/**
 * @brief          小米电机ID检查
 * @param[in]      id:  控制电机CAN_ID【出厂默认0x7F】
 * @retval         none
 */
void chack_cybergear(uint8_t ID)
{
    txMsg0.id = Communication_Type_GetID << 24 | Master_CAN_ID << 8 | ID;
    can0_txd();
}

/**
 * @brief          使能小米电机
 * @param[in]      Motor:对应控制电机结构体
 * @retval         none
 */
void start_cybergear(MI_Motor *Motor, rt_uint8_t device_type)
{

    if (device_type == DEVICE_OF_CAN0)
    {
        txMsg0.id = 0;
        txMsg0.id = Communication_Type_MotorEnable << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        txMsg0.data[0] = 0x00;
        txMsg0.data[1] = 0x00;
        txMsg0.data[2] = 0x00;
        txMsg0.data[3] = 0x00;
        txMsg0.data[4] = 0x00;
        txMsg0.data[5] = 0x00;
        txMsg0.data[6] = 0x00;
        txMsg0.data[7] = 0x00;
        can0_txd();
    }
    else if (device_type == DEVICE_OF_CAN1)
    {
        txMsg1.id = 0;
        txMsg1.id = Communication_Type_MotorEnable << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        txMsg1.data[0] = 0x00;
        txMsg1.data[1] = 0x00;
        txMsg1.data[2] = 0x00;
        txMsg1.data[3] = 0x00;
        txMsg1.data[4] = 0x00;
        txMsg1.data[5] = 0x00;
        txMsg1.data[6] = 0x00;
        txMsg1.data[7] = 0x00;
        can1_txd();
    }
}

/**
 * @brief          停止电机
 * @param[in]      Motor:对应控制电机结构体
 * @param[in]      clear_error:清除错误位（0 不清除 1清除）
 * @retval         None
 */
void stop_cybergear(MI_Motor *Motor, uint8_t clear_error, rt_uint8_t device_type)
{

    if (device_type == DEVICE_OF_CAN0)
    {
        txMsg0.data[0] = clear_error;
        txMsg0.data[1] = 0x00;
        txMsg0.data[2] = 0x00;
        txMsg0.data[3] = 0x00;
        txMsg0.data[4] = 0x00;
        txMsg0.data[5] = 0x00;
        txMsg0.data[6] = 0x00;
        txMsg0.data[7] = 0x00;
        txMsg0.id = 0;
        txMsg0.id = Communication_Type_MotorStop << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        can0_txd();
    }
    else if (device_type == DEVICE_OF_CAN1)
    {
        txMsg1.data[0] = clear_error;
        txMsg1.data[1] = 0x00;
        txMsg1.data[2] = 0x00;
        txMsg1.data[3] = 0x00;
        txMsg1.data[4] = 0x00;
        txMsg1.data[5] = 0x00;
        txMsg1.data[6] = 0x00;
        txMsg1.data[7] = 0x00;
        txMsg1.id = 0;
        txMsg1.id = Communication_Type_MotorStop << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        can1_txd();
    }

}

/**
 * @brief          设置电机模式(必须停止时调整！)
 * @param[in]      Motor:  电机结构体
 * @param[in]      Mode:   电机工作模式（1.运动模式Motion_mode 2. 位置模式Position_mode 3. 速度模式Speed_mode 4. 电流模式Current_mode）
 * @retval         none
 */
void set_mode_cybergear(MI_Motor *Motor, uint8_t Mode, rt_uint8_t device_type)
{
    stop_cybergear(Motor, 1, device_type);
    rt_thread_mdelay(10);
    Set_Motor_Parameter(Motor, Run_mode, Mode, 's', device_type);
    rt_thread_mdelay(10);
    start_cybergear(Motor, device_type);
    rt_thread_mdelay(10);
}

/**
 * @brief          位置控制模式下设置位置
 * @param[in]      Motor:  电机结构体
 * @param[in]      position:目标位置
 * @retval         none
 */
void set_position_cybergear(MI_Motor *Motor, float position, float max_speed, float limit_current, float kp,
        rt_uint8_t device_type)
{
    Set_Motor_Parameter(Motor, Limit_Spd, max_speed, 'f', device_type);
    //Set_Motor_Parameter(Motor,Limit_Cur,limit_current,'f');
    Set_Motor_Parameter(Motor, Loc_Kp, kp, 'f', device_type);
    Set_Motor_Parameter(Motor, Loc_Ref, position, 'f', device_type);

}

/**
 * @brief          速度控制模式下设置速度
 * @param[in]      Motor:  电机结构体
 * @param[in]      speed:目标速度
 * @retval         none
 */
void set_speed_cybergear(MI_Motor *Motor, float speed, rt_uint8_t device_type)
{
    Set_Motor_Parameter(Motor, Spd_Ref, speed, 'f', device_type);
}

/**
 * @brief          电流控制模式下设置电流
 * @param[in]      Motor:  电机结构体
 * @param[in]      Current:电流设置
 * @retval         none
 */
void set_current_cybergear(MI_Motor *Motor, float Current, rt_uint8_t device_type)
{
    Set_Motor_Parameter(Motor, Iq_Ref, Current, 'f', device_type);
    rt_thread_mdelay(10);
}
/**
 * @brief          设置电机零点
 * @param[in]      Motor:  电机结构体
 * @retval         none
 */

void set_zeropos_cybergear(MI_Motor *Motor, rt_uint8_t device_type)
{

    if (device_type == DEVICE_OF_CAN0)
    {
        txMsg0.id = 0;
        txMsg0.id = Communication_Type_SetPosZero << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        txMsg0.data[0] = 0x01;
        txMsg0.data[1] = 0x00;
        txMsg0.data[2] = 0x00;
        txMsg0.data[3] = 0x00;
        txMsg0.data[4] = 0x00;
        txMsg0.data[5] = 0x00;
        txMsg0.data[6] = 0x00;
        txMsg0.data[7] = 0x00;
        can0_txd();
    }
    else if (device_type == DEVICE_OF_CAN1)
    {
        txMsg1.id = 0;
        txMsg1.id = Communication_Type_SetPosZero << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        txMsg1.data[0] = 0x01;
        txMsg1.data[1] = 0x00;
        txMsg1.data[2] = 0x00;
        txMsg1.data[3] = 0x00;
        txMsg1.data[4] = 0x00;
        txMsg1.data[5] = 0x00;
        txMsg1.data[6] = 0x00;
        txMsg1.data[7] = 0x00;
        can1_txd();
    }
    else
    {
        rt_kprintf("device type param error:%d\r\n", device_type);
    }
}

/**
 * @brief          设置电机CANID
 * @param[in]      Motor:  电机结构体
 * @param[in]      Motor:  设置新ID
 * @retval         none
 */
void set_CANID_cybergear(MI_Motor *Motor, uint8_t CAN_ID)
{
    txMsg0.id = Communication_Type_CanID << 24 | CAN_ID << 16 | Master_CAN_ID << 8 | Motor->CAN_ID;
    Motor->CAN_ID = CAN_ID;
    can0_txd();
}
/**
 * @brief          小米电机初始化
 * @param[in]      Motor:  电机结构体
 * @param[in]      Can_Id: 小米电机ID(默认0x7F)
 * @param[in]      Motor_Num: 电机编号
 * @param[in]      mode: 电机工作模式（0.运动模式Motion_mode 1. 位置模式Position_mode 2. 速度模式Speed_mode 3. 电流模式Current_mode）
 * @retval         none
 */
void init_cybergear(MI_Motor *Motor, uint8_t Can_Id, uint8_t mode, rt_uint8_t device_type)
{
    Motor->CAN_ID = Can_Id;
    set_mode_cybergear(Motor, mode, device_type);
}

/**
 * @brief          小米运控模式指令
 * @param[in]      Motor:  目标电机结构体
 * @param[in]      torque: 力矩设置[-12,12] N*M
 * @param[in]      MechPosition: 位置设置[-12.5,12.5] rad
 * @param[in]      speed: 速度设置[-30,30] rpm
 * @param[in]      kp: 比例参数设置
 * @param[in]      kd: 微分参数设置
 * @retval         none
 */
void motor_controlmode(MI_Motor *Motor, float torque, float MechPosition, float speed, float kp, float kd,
        rt_uint8_t device_type)
{

    if (device_type == DEVICE_OF_CAN0)
    {
        txMsg0.data[0] = float_to_uint(MechPosition, P_MIN, P_MAX, 16) >> 8;
        txMsg0.data[1] = float_to_uint(MechPosition, P_MIN, P_MAX, 16);
        txMsg0.data[2] = float_to_uint(speed, V_MIN, V_MAX, 16) >> 8;
        txMsg0.data[3] = float_to_uint(speed, V_MIN, V_MAX, 16);
        txMsg0.data[4] = float_to_uint(kp, KP_MIN, KP_MAX, 16) >> 8;
        txMsg0.data[5] = float_to_uint(kp, KP_MIN, KP_MAX, 16);
        txMsg0.data[6] = float_to_uint(kd, KD_MIN, KD_MAX, 16) >> 8;
        txMsg0.data[7] = float_to_uint(kd, KD_MIN, KD_MAX, 16);
        txMsg0.id = Communication_Type_MotionControl << 24 | float_to_uint(torque, T_MIN, T_MAX, 16) << 8
                | Motor->CAN_ID;
        can0_txd();
    }
    else if (device_type == DEVICE_OF_CAN1)
    {
        txMsg1.data[0] = float_to_uint(MechPosition, P_MIN, P_MAX, 16) >> 8;
        txMsg1.data[1] = float_to_uint(MechPosition, P_MIN, P_MAX, 16);
        txMsg1.data[2] = float_to_uint(speed, V_MIN, V_MAX, 16) >> 8;
        txMsg1.data[3] = float_to_uint(speed, V_MIN, V_MAX, 16);
        txMsg1.data[4] = float_to_uint(kp, KP_MIN, KP_MAX, 16) >> 8;
        txMsg1.data[5] = float_to_uint(kp, KP_MIN, KP_MAX, 16);
        txMsg1.data[6] = float_to_uint(kd, KD_MIN, KD_MAX, 16) >> 8;
        txMsg1.data[7] = float_to_uint(kd, KD_MIN, KD_MAX, 16);
        txMsg1.id = Communication_Type_MotionControl << 24 | float_to_uint(torque, T_MIN, T_MAX, 16) << 8
                | Motor->CAN_ID;
        can1_txd();
    }
    else
    {
        rt_kprintf("device type param error:%d\r\n", device_type);
    }
}
/**
 * @brief          单个参数读取
 * @param[in]      Motor:  电机结构体
 * @param[in]      index:  索引号
 * @retval         none
 */
void motor_param_read(MI_Motor *Motor, uint16_t Index, rt_uint8_t device_type)
{
    if (device_type == DEVICE_OF_CAN0)
    {
        txMsg0.id = 0;
        txMsg0.id = Communication_Type_GetSingleParameter << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        txMsg0.data[0] = Index;
        txMsg0.data[1] = Index >> 8;
        txMsg0.data[2] = 0x00;
        txMsg0.data[3] = 0x00;
        txMsg0.data[4] = 0x00;
        txMsg0.data[5] = 0x00;
        txMsg0.data[6] = 0x00;
        txMsg0.data[7] = 0x00;
        can0_txd();
    }
    else if (device_type == DEVICE_OF_CAN1)
    {
        txMsg1.id = 0;
        txMsg1.id = Communication_Type_GetSingleParameter << 24 | Master_CAN_ID << 8 | Motor->CAN_ID;
        txMsg1.data[0] = Index;
        txMsg1.data[1] = Index >> 8;
        txMsg1.data[2] = 0x00;
        txMsg1.data[3] = 0x00;
        txMsg1.data[4] = 0x00;
        txMsg1.data[5] = 0x00;
        txMsg1.data[6] = 0x00;
        txMsg1.data[7] = 0x00;
        can1_txd();
    }
    else
    {
        rt_kprintf("device type param error:%d\r\n", device_type);
    }
    rt_thread_mdelay(10);
}
static rt_err_t can0_rx_call(rt_device_t dev, rt_size_t size)
{
    rt_sem_release(&rx_can0_sem);
    return RT_EOK;
}
static rt_err_t can1_rx_call(rt_device_t dev, rt_size_t size)
{
    rt_sem_release(&rx_can1_sem);
    return RT_EOK;
}
/*****************************回调函数 负责接回传信息 可转移至别处*****************************/
/**
 * @brief          hal库CAN回调函数,接收电机数据

 * @param[in]      hcan:CAN句柄指针
 * @retval         none
 */
void can0_rx_thread(void *parameter)
{
    struct rt_can_msg rxmsg = { 0 };

    rt_device_set_rx_indicate(can0_dev, can0_rx_call);
    while (1)
    {
        if (rt_sem_take(&rx_can0_sem, RT_WAITING_FOREVER) == RT_EOK)
        {
            rxmsg.hdr_index = -1;
            rxmsg.len = 8;
//            rt_sem_take(&rx_can0_sem, RT_WAITING_FOREVER);
            rt_device_read(can0_dev, 0, &rxmsg, sizeof(rxmsg));
            Type_ID = Get_Type_Mode(rxmsg.id); //首先获取回传电机ID信息
            switch (Type_ID)
            //将对应ID电机信息提取至对应结构体
            {
            //类型为MechPos参数读取返回帧，
            case 0X11:
                MechPos_Data_Handler(&mi_motor[0], rxmsg.data, rxmsg.id);
                //type 类型为 2 的返回帧获取current 速度
                break;
            case 0X02:
                Motor_Data_Handler(&mi_motor[0], rxmsg.data, rxmsg.id);
                break;
            default:
                break;
            }
//               rt_kprintf("ID:%x\r\n", rxmsg.id);
            //   for (rt_uint8_t i = 0; i < 8; i++)
            //   {
            //       rt_kprintf("data[%d]:%x\r\n", i,rxmsg.data[i]);
            //   }

            //  rt_kprintf("\n");
        }
    }
}
/**
 * @brief          hal库CAN回调函数,接收电机数据

 * @param[in]      hcan:CAN句柄指针
 * @retval         none
 */
void can1_rx_thread(void *parameter)
{
    struct rt_can_msg rxmsg = { 0 };

    rt_device_set_rx_indicate(can1_dev, can1_rx_call);

    while (1)
    {
        rxmsg.hdr_index = -1;
        rxmsg.len = 8;
        rt_sem_take(&rx_can1_sem, RT_WAITING_FOREVER);
        rt_device_read(can1_dev, 0, &rxmsg, sizeof(rxmsg));
        Type_ID = Get_Type_Mode(rxmsg.id);            //首先获取回传电机ID信息
        switch (Type_ID)
        //将对应ID电机信息提取至对应结构体
        {
        //类型为MechPos参数读取返回帧，
        case 0X11:
            MechPos_Data_Handler(&mi_motor[1], rxmsg.data, rxmsg.id);
            //type 类型为 2 的返回帧获取current 速度        
            break;
        case 0X02:
            Motor_Data_Handler(&mi_motor[1], rxmsg.data, rxmsg.id);
            break;
        default:
            break;
        }
        //   rt_kprintf("ID:%x\r\n", rxmsg.id);
        //   for (rt_uint8_t i = 0; i < 8; i++)
        //   {
        //       rt_kprintf("data[%d]:%x\r\n", i,rxmsg.data[i]);
        //   }

        //  rt_kprintf("\n");
    }
}
