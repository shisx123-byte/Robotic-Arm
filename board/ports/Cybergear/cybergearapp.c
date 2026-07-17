#include "cybergear.h"
#include <stdio.h>
#include <stdlib.h>
#include <rtthread.h>
#include <rtdevice.h>
static rt_timer_t timer1;
rt_thread_t thread_can0;
rt_thread_t thread_can1;
rt_device_t can0_dev;
rt_device_t can1_dev;
uint32_t Motor_Can_ID;    //接收数据电机ID
static rt_sem_t mechPos_read_sem = RT_NULL;
static void motor_init();
void read_poch_thread(void *parameter);
static void timer_init(void);
void read_poch(void);
void speed_print_thread(void *parameter);
static void can_init(void);
void motor2_init_thread(void *parameter);
int cybergearapp_init(void)
{
    timer_init();
    can_init();
    read_poch();
    motor_init();
    return 0;
}
//can0 can1初始化
static void can_init(void)
{
    rt_uint8_t res;
    can0_dev = rt_device_find(CAN0_DEV_NAME);
    if (!can0_dev)
    {
        rt_kprintf("find %s failed!\n", CAN0_DEV_NAME);
        return;
    }
    can1_dev = rt_device_find(CAN1_DEV_NAME);
    if (!can1_dev)
    {
        rt_kprintf("find %s failed!\n", CAN1_DEV_NAME);
        return;
    }
    res = rt_device_open(can0_dev, RT_DEVICE_FLAG_INT_TX | RT_DEVICE_FLAG_INT_RX);
    res = rt_device_control(can0_dev, RT_CAN_CMD_SET_BAUD, (void *) CAN1MBaud);
    res = rt_device_open(can1_dev, RT_DEVICE_FLAG_INT_TX | RT_DEVICE_FLAG_INT_RX);
    res = rt_device_control(can1_dev, RT_CAN_CMD_SET_BAUD, (void *) CAN1MBaud);
    RT_ASSERT(res == RT_EOK);
    rt_sem_init(&rx_can0_sem, "rx_can0_sem", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&rx_can1_sem, "rx_can1_sem", 0, RT_IPC_FLAG_FIFO);
    thread_can0 = rt_thread_create("can0_rx", can0_rx_thread, RT_NULL, 1024, 10, 50);
    if (thread_can0 != RT_NULL)
    {
        rt_thread_startup(thread_can0);
    }
    else
    {
        rt_kprintf("create can_rx thread failed!\n");
    }
    thread_can1 = rt_thread_create("can1_rx", can1_rx_thread, RT_NULL, 1024, 25, 10);
    if (thread_can1 != RT_NULL)
    {
        rt_thread_startup(thread_can1);
    }
    else
    {
        rt_kprintf("create can1_rx thread failed!\n");
    }

}
/* 定时器 超时函数，定时读取电机参数 */
static void timer_100ms_callback(void *parameter)
{
    rt_sem_release(mechPos_read_sem);
}
static void timer_init(void)
{
    mechPos_read_sem = rt_sem_create("dsem", 0, RT_IPC_FLAG_PRIO);
    timer1 = rt_timer_create("timer1", timer_100ms_callback,
    RT_NULL, 10,
    RT_TIMER_FLAG_PERIODIC);
    /* 启动定时器 1 */
    if (timer1 != RT_NULL)
        rt_timer_start(timer1);
}
//电机初始化，默认电机ID 为0X7F
static void motor_init()
{
    txMsg0.id = 0;
    txMsg0.ide = RT_CAN_EXTID;
    txMsg0.rtr = RT_CAN_DTR;
    txMsg0.len = 8;
    txMsg1.id = 0;
    txMsg1.ide = RT_CAN_EXTID;
    txMsg1.rtr = RT_CAN_DTR;
    txMsg1.len = 8;
    mi_motor[1].CAN_ID = 0x7f;
    mi_motor[0].CAN_ID = 0x7f;
}
/**
 * @brief          设置电机模式；
 * @param[in]      argv[1]:  模式选择 0 -->运控 1-->位置 2-->速度 ，3-->电流
 * @param[in]      argv[2]:  电机索引index 0代表电机1 使用can0接口
 * @retval         none
 */
void cybergearapp_set_mode(int argc, char *argv[])
{
    if (argc != 3)
    {
        rt_kprintf("Usage: set_mode <mode>\n");
        return;
    }
    int mode = atoi(argv[1]);
    int device_type = atoi(argv[2]);
    switch (mode)
    {
    case 0:
        init_cybergear(&mi_motor[1], 0x7f, Motion_mode, device_type);
        rt_kprintf("Setting mode Motion_mode\r\n");
        break;
    case 1:
        init_cybergear(&mi_motor[1], 0x7f, Position_mode, device_type);
        rt_kprintf("Setting mode Position_mode\r\n");
        break;
    case 2:
        init_cybergear(&mi_motor[1], 0x7f, Velocity_mode, device_type);
        rt_kprintf("Setting mode Velocity_mode\r\n");
        break;
    case 3:
        init_cybergear(&mi_motor[1], 0x7f, Current_mode, device_type);

        rt_kprintf("Setting mode Current_mode\r\n");
    default:
        rt_kprintf("please choose the correct mode!\r\n");

    }
    return;
}

/**
 * @brief          位置模式测试函数
 * @param[in]      argv[1]:  位移目标值
 * @param[in]      argv[2]:  电机索引index 0代表电机1 使用can0接口
 * @retval         none
 */
void test_motor_position(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: test_motor <position>\n");
        return;
    }
    // 打印字符串参数
    printf("argv[%d]: %s\n", 1, argv[1]);
    rt_uint8_t device_type = atoi(argv[2]);
    // 正确转换字符串为浮点数
    char *endptr;
    float value = strtof(argv[1], &endptr);
    // 检查转换是否成功
    if (*endptr != '\0')
    {
        printf("Invalid float value: %s\n", argv[1]);
        return;
    }
    // 调用电机控制函数
    set_position_cybergear(&mi_motor[1], value, 5, 8, 40, device_type);
}
/**
 * @brief          速度模式测试函数
 * @param[in]      argv[1]:  速度目标值
 * @param[in]      argv[2]:  电机索引index 0代表电机1 使用can0接口
 * @retval         none
 */
void test_motor_speed(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: test_motor <position>\n");
        return;
    }
    // 打印字符串参数
    printf("argv[%d]: %s\n", 1, argv[1]);
    // 正确转换字符串为浮点数
    char *endptr;
    float value = strtof(argv[1], &endptr);
    // 检查转换是否成功
    if (*endptr != '\0')
    {
        printf("Invalid float value: %s\n", argv[1]);
        return;
    }
    rt_uint8_t device_type = atoi(argv[2]);
    // 打印转换后的浮点数
    printf("Converted float value: %f\n", value);
    set_speed_cybergear(&mi_motor[1], value, device_type);
    rt_thread_mdelay(200);
}

void speed_print(void)
{
    //创建线程
    rt_thread_t thread;
    thread = rt_thread_create("speed_print", speed_print_thread, RT_NULL, 1024, 25, 10);
    if (thread != RT_NULL)
    {
        rt_thread_startup(thread);
    }
    else
    {
        rt_kprintf("create speed_print thread failed!\n");
    }
}
//速度角度测试线程 msh
void speed_print_thread(void *parameter)
{
    static rt_uint8_t result;
    while (1)
    {
        if (result == RT_EOK)
        {
            rt_thread_mdelay(3000);
            printf("Speed0:%f\n", mi_motor[0].Speed);
            printf("rad0:%f\n", mi_motor[0].Angle);
        }
    }
}

void motor2_init(void)
{
    //创建线程
    rt_thread_t thread;
    thread = rt_thread_create("motor2_init", motor2_init_thread, RT_NULL, 1024, 25, 10);
    if (thread != RT_NULL)
    {
        rt_thread_startup(thread);
    }
    else
    {
        rt_kprintf("create motor2_init thread failed!\n");
    }
}
/*电机2设为位置模式跟随电机1*/
void motor2_init_thread(void *parameter)
{
    while (1)
    {
        //设置位置函数
        set_position_cybergear(&mi_motor[1], mi_motor[0].MechPos, mi_motor[0].MechVel, 8, 40, DEVICE_OF_CAN1);
        rt_thread_mdelay(20);
    }
}
void read_poch(void)
{
    //创建线程
    rt_thread_t thread;
    thread = rt_thread_create("read_poch", read_poch_thread, RT_NULL, 1024, 25, 10);
    if (thread != RT_NULL)
    {
        rt_thread_startup(thread);
    }
    else
    {
        rt_kprintf("create read_poch thread failed!\n");
    }
}
//定时读取计圈角度，电流、速度
void read_poch_thread(void *parameter)
{
    static rt_uint8_t result;
    while (1)
    {
        result = rt_sem_take(mechPos_read_sem, RT_WAITING_FOREVER);
        if (result == RT_EOK)
        {
            motor_param_read(&mi_motor[0], Mech_Pos, DEVICE_OF_CAN0);    //读取电机0计圈角度
            motor_param_read(&mi_motor[0], mechVel, DEVICE_OF_CAN0);    //读取电机0速度
            motor_param_read(&mi_motor[0], Iq_Ref, DEVICE_OF_CAN0); //读取电机0的电流
            motor_param_read(&mi_motor[1], Mech_Pos, DEVICE_OF_CAN1); //读取电机1计圈角度
            motor_param_read(&mi_motor[1], mechVel, DEVICE_OF_CAN1); //读取电机1速度
        }
    }
}
/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(speed_print, demo);
MSH_CMD_EXPORT(motor2_init, demo);
MSH_CMD_EXPORT(read_poch, demo);
MSH_CMD_EXPORT(test_motor_position, demo);
MSH_CMD_EXPORT(test_motor_speed, demo);
MSH_CMD_EXPORT_ALIAS(cybergearapp_set_mode, set_mode, Set the mode of the cybergear);
INIT_APP_EXPORT(cybergearapp_init);
