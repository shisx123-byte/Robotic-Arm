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
#ifndef __CYBERBERBER_H__
#define __CYBERBERBER_H__

#include "rtdevice.h"
#include "rtthread.h"
#include "board.h"
#define LED_PIN_G BSP_IO_PORT_14_PIN_1 /* Onboard GREEN LED pins */

#define P_MIN -12.5f
#define P_MAX 12.5f
#define V_MIN -1.0f
#define V_MAX 1.0f
#define KP_MIN 0.0f
#define KP_MAX 500.0f
#define KD_MIN 0.0f
#define KD_MAX 5.0f
#define T_MIN -12.0f
#define T_MAX 12.0f
#define MAX_P 720
#define MIN_P -720
#define LEADER_FOLLOWER_GAIN   1.0f
#define Master_CAN_ID         0x01                  
//通信类型指令定义
#define Communication_Type_GetID 0x00           //获取设备的 ID 和 64 位 MCU 唯一标识符
#define Communication_Type_MotionControl 0x01 	  //运控模式电机控制指令 （通信类型 1）用来向电机发送控制指
#define Communication_Type_MotorRequest 0x02	 //电机反馈数据 用来向主机反馈电机运行状态
#define Communication_Type_MotorEnable 0x03	    //电机使能运行
#define Communication_Type_MotorStop 0x04	    //电机停止运行
#define Communication_Type_SetPosZero 0x06	     //设置电机机械零位
#define Communication_Type_CanID 0x07	        //设置电机 CAN_ID（通信类型 7）更改当前电机 CAN_ID , 立即生效
#define Communication_Type_Control_Mode 0x12   //
#define Communication_Type_GetSingleParameter 0x11	//单个参数读取
#define Communication_Type_SetSingleParameter 0x12	//单个参数写入
#define Communication_Type_ErrorFeedback 0x15	    //故障反馈

#define Run_mode 0x7005	           // index of  run mode 
#define Iq_Ref   0x7006            // index of  Iq_Ref 电流模式 Iq 指令
#define Spd_Ref  0x700A            // index of  Spd_Ref 转速模式转速指令
#define Limit_Torque 0x700B       // index of  Limit_Torque  转矩限制
#define Cur_Kp 0x7010           // index of  Cur_Kp 电流的 Kp
#define Cur_Ki 0x7011          // index of  Cur_Ki 电流的 Ki
#define Cur_Filt_Gain 0x7014    // index of  Cur_Filt_Gain 电流的滤波系数
#define Loc_Ref 0x7016         // index of  Loc_Ref 位置模式角度指令
#define Loc_Kp 0x701E          // index of  Loc_Kp 位置模式的 Kp
#define Limit_Spd 0x7017        // index of  Limit_Spd  位置模式速度限制
#define Limit_Cur 0x7018        // index of  Limit_Cur  速度位置模式电流限制
#define Mech_Pos 0x7019        // index of  Mech_Pos  负载端计圈机械角度
#define  Iqf      0x701A         // index of  Iqf  iq 滤波值
#define  mechVel      0x701B       //   负载端的转速
#define Gain_Angle 720/32767.0       
//参数宏定义
#define Bias_Angle 0x8000          
#define Gain_Speed 30/32767.0 
#define Bias_Speed 0x8000             
#define Gain_Torque 12/32767.0
#define Bias_Torque 0x8000
#define Temp_Gain   0.1

#define Motor_Error 0x00
#define Motor_OK 0X01
#define CAN0_DEV_NAME       "canfd0"    
#define CAN1_DEV_NAME       "canfd1"     
#define DEVICE_OF_CAN0       0x00
#define DEVICE_OF_CAN1       0X01
extern rt_thread_t thread;
extern rt_device_t can0_dev;
extern rt_device_t can1_dev;
extern struct rt_semaphore rx_can0_sem;
extern struct rt_semaphore rx_can1_sem;
extern struct rt_can_msg txMsg0 ;
extern struct rt_can_msg txMsg1 ;
extern struct rt_can_msg rxMsg ;

//控制模式定义结构体
enum CONTROL_MODE    
{
    Motion_mode = 0,
    Position_mode,  
    Velocity_mode,     
    Current_mode    
};
//故障类型定义
enum ERROR_TAG     
{
    OK                 = 0, //无故障
    BAT_LOW_ERR        = 1, //电池低电压故障
    OVER_CURRENT_ERR   = 2, //过流故障
    OVER_TEMP_ERR      = 3,//过温
    MAGNETIC_ERR       = 4, //磁编码故障
    HALL_ERR_ERR       = 5, //霍尔编码故障
    NO_CALIBRATION_ERR = 6 //预定义
};
//电机结构体
typedef struct{           
	uint8_t CAN_ID;       //can id 
    uint8_t MCU_ID;       //mcu id 标识符
	float Angle;          //回传角度
	float Speed;          //回传速度
	float Torque;        //回传力矩
	float Temp;			  //回传温度
	uint16_t set_current;  
	uint16_t set_speed;
	uint16_t set_position;
	
	uint8_t error_code;
	float  MechPos;
    float  MechVel;
	float Angle_Bias;
    float Iq_ref;
	float  current_iq;
}MI_Motor;
extern MI_Motor mi_motor[2];

extern void chack_cybergear(uint8_t ID);
extern void start_cybergear(MI_Motor *Motor,rt_uint8_t device_type);
extern void stop_cybergear(MI_Motor *Motor, uint8_t clear_error,rt_uint8_t device_type);
extern void set_mode_cybergear(MI_Motor *Motor, uint8_t Mode,rt_uint8_t device_type);
extern void set_current_cybergear(MI_Motor *Motor, float Current,rt_uint8_t device_type);
extern void set_zeropos_cybergear(MI_Motor *Motor,rt_uint8_t device_type);
extern void set_CANID_cybergear(MI_Motor *Motor, uint8_t CAN_ID);
extern void init_cybergear(MI_Motor *Motor, uint8_t Can_Id, uint8_t mode,rt_uint8_t device_type);
extern void motor_controlmode(MI_Motor *Motor,float torque, float MechPosition, float speed, float kp, float kd,rt_uint8_t device_type);
extern void set_position_cybergear(MI_Motor *Motor, float position, float max_speed, float limit_current , float kp,rt_uint8_t device_type);
extern void set_speed_cybergear(MI_Motor *Motor, float speed,rt_uint8_t device_type);
extern void motor_param_read(MI_Motor *Motor,uint16_t Index,rt_uint8_t device_type);
extern uint32_t Get_Type_Mode(uint32_t CAN_ID_Frame);
extern void can0_rx_thread(void *parameter);
extern void can1_rx_thread(void *parameter);
#endif
