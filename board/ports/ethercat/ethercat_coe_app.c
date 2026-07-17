/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2024-09-11     Wangyuqiang   first version
 */

#include <rtthread.h>
#include <board.h>

#include "hal_data.h"
#include "ecat_def.h"
#include "ecatappl.h"
#include "ecatslv.h"
#include "applInterface.h"
#if (CiA402_SAMPLE_APPLICATION == 1)
#include "cia402appl.h"
#else
#include "sampleappl.h"
#endif
#include "renesashw.h"
#include <stdio.h>

static rt_uint8_t ethercat_thread_stack[1024 * 4];
static struct rt_thread ethercat_thread;

#ifndef ETHERCAT_MAINLOOP_DELAY_MS
#define ETHERCAT_MAINLOOP_DELAY_MS 0U
#endif

#ifndef ETHERCAT_LOW_PRIORITY_IO_PERIOD_TICKS
#define ETHERCAT_LOW_PRIORITY_IO_PERIOD_TICKS 1U
#endif

extern void APPL_LowPriorityIo(void);

#ifdef BSP_USING_CYBERGEAR_MOTOR
#include  "cybergear.h"
#include "samplecia402.h"

static rt_sem_t cyber_sem = RT_NULL;
static rt_timer_t cyber_timer;

void cyber_motor_entry(void *param);
static void timer_cyber_init(void);
#endif

static void ecat_thread_entry ();

void phy_rtl8211f_initial(ether_phy_instance_ctrl_t *phydev)
{
#define RTL_8211F_PAGE_SELECT 0x1F
#define RTL_8211F_EEELCR_ADDR 0x11
#define RTL_8211F_LED_PAGE 0xD04
#define RTL_8211F_LCR_ADDR 0x10

    uint32_t val1, val2 = 0;

    /* switch to led page */
    R_ETHER_PHY_Write(phydev, RTL_8211F_PAGE_SELECT, RTL_8211F_LED_PAGE);

    /* set led1(green) Link 10/100/1000M, and set led2(yellow) Link 10/100/1000M+Active */
    R_ETHER_PHY_Read(phydev, RTL_8211F_LCR_ADDR, &val1);
    val1 |= (1 << 5);
    val1 |= (1 << 8);
    val1 &= (~(1 << 9));
    val1 |= (1 << 10);
    val1 |= (1 << 11);
    R_ETHER_PHY_Write(phydev, RTL_8211F_LCR_ADDR, val1);

    /* set led1(green) EEE LED function disabled so it can keep on when linked */
    R_ETHER_PHY_Read(phydev, RTL_8211F_EEELCR_ADDR, &val2);
    val2 &= (~(1 << 2));
    R_ETHER_PHY_Write(phydev, RTL_8211F_EEELCR_ADDR, val2);

    /* switch back to page0 */
    R_ETHER_PHY_Write(phydev, RTL_8211F_PAGE_SELECT, 0xa42);

    rt_thread_mdelay(100);

    return;
}

static int coe_app(void)
{
    rt_err_t ret = rt_thread_init(&ethercat_thread,
                "ec",
                ecat_thread_entry,
                RT_NULL,
                &ethercat_thread_stack[0],
                sizeof(ethercat_thread_stack),
                5, 10);

    if(ret == RT_EOK)
    {
        rt_thread_startup(&ethercat_thread);
    }

#ifdef BSP_USING_CYBERGEAR_MOTOR
    timer_cyber_init();
    rt_thread_t cy_tid = rt_thread_create("cyber_motor", cyber_motor_entry, RT_NULL, 1024, 5, 20);
    if (cy_tid != RT_NULL)
    {
        rt_thread_startup(cy_tid);
    }
#endif

    return 0;
}
INIT_APP_EXPORT(coe_app);

static void ecat_thread_entry ()
{
    rt_tick_t next_io_tick = rt_tick_get();

    /* Initialize EtherCAT SSC Port */
    RM_ETHERCAT_SSC_PORT_Open(gp_ethercat_ssc_port->p_ctrl, gp_ethercat_ssc_port->p_cfg);

    /* Initilize the stack */
    MainInit();

    /* Turn off all LEDs at boot (EtherKit LEDs are active LOW,
       and pin_data.c defaults them to OUTPUT_LOW = ON) */
    APPL_SetLed(0);

#if (CiA402_SAMPLE_APPLICATION == 1)
    /* Initialize axis structures */
    CiA402_Init();
#endif

    /* Create basic mapping */
    APPL_GenerateMapping(&nPdInputSize,&nPdOutputSize);
    /* Set stack run flag */
    bRunApplication = TRUE;

    /* Execute the stack */
    while(bRunApplication == TRUE)
    {
        MainLoop();
        rt_tick_t now_tick = rt_tick_get();
        if ((rt_int32_t)(now_tick - next_io_tick) >= 0)
        {
            APPL_LowPriorityIo();
            next_io_tick = now_tick + ETHERCAT_LOW_PRIORITY_IO_PERIOD_TICKS;
        }

        if (ETHERCAT_MAINLOOP_DELAY_MS > 0U)
        {
            rt_thread_mdelay(ETHERCAT_MAINLOOP_DELAY_MS);
        }
        else
        {
            rt_thread_yield();
        }
    }
#if (CiA402_SAMPLE_APPLICATION == 1)
    /* Remove all allocated axes resources */
    CiA402_DeallocateAxis();
#endif
    /* Close SSC Port */
    RM_ETHERCAT_SSC_PORT_Close(gp_ethercat_ssc_port->p_ctrl);

    return;
}

#ifdef BSP_USING_CYBERGEAR_MOTOR
void cyber_motor_entry(void *param)
{
    rt_uint8_t result;
    while (1)
    {
        result = rt_sem_take(cyber_sem, RT_WAITING_FOREVER);
        if (result == RT_EOK)
        {
            //电机控制函数
            CybergearMotor();
        }
    }
}

/* 定时器 1 超时函数 负责定时扫描主站映射数据 */
static void cyimer_100ms_callback(void *parameter)
{
    rt_sem_release(cyber_sem);
}

static void timer_cyber_init(void)
{
    cyber_sem = rt_sem_create("sy_sem", 0, RT_IPC_FLAG_PRIO);
    cyber_timer = rt_timer_create("sy_timer", cyimer_100ms_callback,
    RT_NULL, 100,
    RT_TIMER_FLAG_PERIODIC);
    /* 启动定时器 1 */
    if (cyber_timer != RT_NULL)
        rt_timer_start(cyber_timer);
}
#endif
