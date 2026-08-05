#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "ecat_def.h"
#include "applInterface.h"
#include "ecatappl.h"
#include "ecatslv.h"
#include "objdef.h"
#include "esc.h"
#include "el9800hw.h"
#include "ssc_irq.h"
#include "ssc_dc_sync.h"

#define SSC_THREAD_STACK_SIZE       8192U
#define SSC_THREAD_PRIORITY         1U
#define SSC_THREAD_TIMESLICE        1U
#define SSC_WAIT_TIMEOUT_TICKS      1
#define SSC_SERVICE_BUDGET_US       100U
#define SSC_MAINLOOP_PERIOD_MS      1U

ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t g_ssc_stack[SSC_THREAD_STACK_SIZE];
static struct rt_thread g_ssc_thread;
static volatile rt_bool_t g_ssc_thread_initialized = RT_FALSE;
static volatile UINT16 g_ssc_init_result = 0xFFFFU;

volatile rt_uint32_t g_ssc_service_count;
volatile rt_uint32_t g_ssc_service_max_us;
volatile rt_uint32_t g_ssc_service_over_budget;
volatile rt_uint64_t g_ssc_service_total_us;

volatile rt_uint32_t g_ssc_pdi_count;
volatile rt_uint32_t g_ssc_pdi_max_us;
volatile rt_uint64_t g_ssc_pdi_total_us;
volatile rt_uint32_t g_ssc_sync0_max_us;
volatile rt_uint64_t g_ssc_sync0_total_us;
volatile rt_uint32_t g_ssc_sync1_max_us;
volatile rt_uint64_t g_ssc_sync1_total_us;
volatile rt_uint32_t g_ssc_mainloop_count;
volatile rt_uint32_t g_ssc_mainloop_max_us;
volatile rt_uint64_t g_ssc_mainloop_total_us;
volatile rt_uint32_t g_ssc_mainloop_deferred;

volatile rt_uint32_t g_ssc_pdi_max_latency_us;
volatile rt_uint32_t g_ssc_sync0_max_latency_us;
volatile rt_uint32_t g_ssc_sync1_max_latency_us;
volatile rt_uint32_t g_ssc_coalesced_al;
volatile rt_uint32_t g_ssc_coalesced_sync0;
volatile rt_uint32_t g_ssc_coalesced_sync1;
volatile rt_uint32_t g_ssc_stale_al;

/* Permanent error snapshot. */
volatile rt_uint32_t g_ssc_al_error_count;
volatile UINT8  g_ssc_last_error_al_status;
volatile UINT16 g_ssc_last_error_al_code;
volatile UINT16 g_ssc_last_error_sm_missed;
volatile UINT16 g_ssc_last_error_cycle_exceeded;
volatile UINT8  g_ssc_last_error_sync_flag;
volatile rt_uint32_t g_ssc_last_error_service_count;
volatile rt_uint32_t g_ssc_last_error_pending;
volatile rt_uint32_t g_ssc_last_error_pending_max;
volatile rt_uint32_t g_ssc_last_error_sync0_edges;
volatile rt_uint32_t g_ssc_last_error_sync0_services;
volatile rt_uint32_t g_ssc_last_error_coalesced_al;
volatile rt_uint32_t g_ssc_last_error_coalesced_sync0;
volatile rt_uint32_t g_ssc_last_error_coalesced_sync1;
volatile rt_uint32_t g_ssc_last_error_time_us;

static inline rt_uint32_t ssc_time_us(void)
{
    return TIM5->CNT;
}

static void ssc_profile_init(void)
{
    rt_uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    rt_uint32_t timer_clock = pclk1;

    /* STM32F4 timer clock doubles when the APB prescaler is not 1. */
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U)
    {
        timer_clock *= 2U;
    }

    __HAL_RCC_TIM5_CLK_ENABLE();
    TIM5->CR1 = 0U;
    TIM5->PSC = (timer_clock / 1000000U) - 1U;
    TIM5->ARR = 0xFFFFFFFFUL;
    TIM5->EGR = TIM_EGR_UG;
    TIM5->CNT = 0U;
    TIM5->CR1 = TIM_CR1_CEN;
}

static rt_uint32_t profile_elapsed(volatile rt_uint32_t *maximum,
                                  volatile rt_uint64_t *total,
                                  rt_uint32_t begin)
{
    rt_uint32_t elapsed = ssc_time_us() - begin;
    if (elapsed > *maximum)
    {
        *maximum = elapsed;
    }
    *total += elapsed;
    return elapsed;
}

static void latency_max(volatile rt_uint32_t *maximum, rt_uint32_t timestamp)
{
    rt_uint32_t latency = ssc_time_us() - timestamp;
    if (latency > *maximum)
    {
        *maximum = latency;
    }
}

static rt_bool_t is_sync_error(UINT16 code)
{
    return ((code == ALSTATUSCODE_SYNCERROR) ||
            (code == ALSTATUSCODE_FATALSYNCERROR) ||
            (code == ALSTATUSCODE_NOSYNCERROR)) ? RT_TRUE : RT_FALSE;
}

void ssc_diag_latch_al_status(UINT8 al_status, UINT16 al_code)
{
    if ((al_code == 0U) || (al_code == 0xFFFFU))
    {
        return;
    }

    g_ssc_al_error_count++;

    /* Once a synchronization failure is captured, a later watchdog caused by
     * stopping the master must not overwrite the useful evidence. */
    if (is_sync_error(g_ssc_last_error_al_code) &&
        !is_sync_error(al_code))
    {
        return;
    }

    g_ssc_last_error_al_status = al_status;
    g_ssc_last_error_al_code = al_code;
    g_ssc_last_error_sm_missed = sSyncManOutPar.u16SmEventMissedCounter;
    g_ssc_last_error_cycle_exceeded = sSyncManOutPar.u16CycleExceededCounter;
    g_ssc_last_error_sync_flag = sSyncManOutPar.u8SyncError;
    g_ssc_last_error_service_count = g_ssc_service_count;
    g_ssc_last_error_pending = ssc_irq_get_pending_count();
    g_ssc_last_error_pending_max = ssc_irq_get_pending_max();
    g_ssc_last_error_sync0_edges = ssc_dc_sync0_edge_count();
    g_ssc_last_error_sync0_services = ssc_dc_sync0_service_count();
    g_ssc_last_error_coalesced_al = g_ssc_coalesced_al;
    g_ssc_last_error_coalesced_sync0 = g_ssc_coalesced_sync0;
    g_ssc_last_error_coalesced_sync1 = g_ssc_coalesced_sync1;
    g_ssc_last_error_time_us = ssc_time_us();
}

static void service_pdi_once(rt_uint32_t timestamp_us)
{
    rt_uint32_t begin;

    if (!bEscIntEnabled)
    {
        return;
    }

    latency_max(&g_ssc_pdi_max_latency_us, timestamp_us);
    if (!ssc_irq_is_active())
    {
        g_ssc_stale_al++;
        return;
    }

    begin = ssc_time_us();
    PDI_Isr();
    (void)profile_elapsed(&g_ssc_pdi_max_us,
                          &g_ssc_pdi_total_us,
                          begin);
    g_ssc_pdi_count++;
    ssc_irq_note_pdi_service();

    if (ssc_irq_is_active())
    {
        ssc_irq_note_active_after_service();
    }
}

static void service_sync0_once(rt_uint32_t timestamp_us)
{
#if DC_SUPPORTED
    rt_uint32_t begin;
    latency_max(&g_ssc_sync0_max_latency_us, timestamp_us);
    begin = ssc_time_us();
    Sync0_Isr();
    (void)profile_elapsed(&g_ssc_sync0_max_us,
                          &g_ssc_sync0_total_us,
                          begin);
    ssc_dc_sync_note_sync0_service();
#else
    RT_UNUSED(timestamp_us);
#endif
}

static void service_sync1_once(rt_uint32_t timestamp_us)
{
#if DC_SUPPORTED
    rt_uint32_t begin;
    latency_max(&g_ssc_sync1_max_latency_us, timestamp_us);
    begin = ssc_time_us();
    Sync1_Isr();
    (void)profile_elapsed(&g_ssc_sync1_max_us,
                          &g_ssc_sync1_total_us,
                          begin);
    ssc_dc_sync_note_sync1_service();
#else
    RT_UNUSED(timestamp_us);
#endif
}

static void account_coalesced(const ssc_irq_snapshot_t *snapshot)
{
    if (snapshot->al_count > 1U)
    {
        g_ssc_coalesced_al += snapshot->al_count - 1U;
    }
    if (snapshot->sync0_count > 1U)
    {
        rt_uint32_t skipped = snapshot->sync0_count - 1U;
        g_ssc_coalesced_sync0 += skipped;
        ssc_dc_sync_note_sync0_drop(skipped);
    }
    if (snapshot->sync1_count > 1U)
    {
        rt_uint32_t skipped = snapshot->sync1_count - 1U;
        g_ssc_coalesced_sync1 += skipped;
        ssc_dc_sync_note_sync1_drop(skipped);
    }
}

static void service_latest_snapshot(const ssc_irq_snapshot_t *snapshot)
{
    rt_bool_t has_al = (snapshot->al_count > 0U) ? RT_TRUE : RT_FALSE;
    rt_bool_t has_sync0 = (snapshot->sync0_count > 0U) ? RT_TRUE : RT_FALSE;

    /*
     * Preserve the order of the newest AL and SYNC0 observations. Old events
     * of the same type are coalesced, never replayed.
     */
    if (has_al && has_sync0)
    {
        if ((rt_int32_t)(snapshot->al_sequence -
                         snapshot->sync0_sequence) <= 0)
        {
            service_pdi_once(snapshot->al_timestamp_us);
            service_sync0_once(snapshot->sync0_timestamp_us);
        }
        else
        {
            service_sync0_once(snapshot->sync0_timestamp_us);
            service_pdi_once(snapshot->al_timestamp_us);
        }
    }
    else if (has_al)
    {
        service_pdi_once(snapshot->al_timestamp_us);
    }
    else if (has_sync0)
    {
        service_sync0_once(snapshot->sync0_timestamp_us);
    }

    if (snapshot->sync1_count > 0U)
    {
        service_sync1_once(snapshot->sync1_timestamp_us);
    }
}

static void ssc_service_cycle(void)
{
    static rt_tick_t last_mainloop_tick;
    ssc_irq_snapshot_t snapshot = {0};
    rt_uint32_t begin_cycle = ssc_time_us();
    rt_uint32_t elapsed;
    rt_tick_t now_tick;
    rt_tick_t mainloop_period =
        rt_tick_from_millisecond(SSC_MAINLOOP_PERIOD_MS);
    rt_bool_t had_events = ssc_irq_take_snapshot(&snapshot);

    if (mainloop_period == 0U)
    {
        mainloop_period = 1U;
    }

    if (had_events)
    {
        account_coalesced(&snapshot);
        service_latest_snapshot(&snapshot);
    }

    now_tick = rt_tick_get();
    if ((rt_tick_t)(now_tick - last_mainloop_tick) >= mainloop_period)
    {
        rt_uint32_t begin = ssc_time_us();
        MainLoop();
        (void)profile_elapsed(&g_ssc_mainloop_max_us,
                              &g_ssc_mainloop_total_us,
                              begin);
        last_mainloop_tick = now_tick;
        g_ssc_mainloop_count++;
    }
    else
    {
        g_ssc_mainloop_deferred++;
    }

    /*
     * PDI_Isr may leave the level active. Schedule a fresh state observation
     * instead of draining repeatedly in this execution window.
     */
    if (ssc_irq_is_active() && bEscIntEnabled &&
        !ssc_irq_events_pending())
    {
        (void)ssc_irq_queue_event(SSC_IRQ_EVENT_AL);
    }

    elapsed = profile_elapsed(&g_ssc_service_max_us,
                              &g_ssc_service_total_us,
                              begin_cycle);
    g_ssc_service_count++;
    if (elapsed > SSC_SERVICE_BUDGET_US)
    {
        g_ssc_service_over_budget++;
    }
}

static void ssc_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);
    ssc_profile_init();

    if (HW_Init() != 0U)
    {
        g_ssc_init_result = 1U;
        rt_kprintf("[SSC] HW init failed\r\n");
        return;
    }

    g_ssc_init_result = MainInit();
    if (g_ssc_init_result != 0U)
    {
        rt_kprintf("[SSC] MainInit failed: 0x%04X\r\n", g_ssc_init_result);
        HW_Release();
        return;
    }

    rt_kprintf("[SSC v11] latest-state model, SPI1=42MHz, TIM5=1MHz\r\n");

    bRunApplication = TRUE;
    while (bRunApplication == TRUE)
    {
        (void)ssc_irq_wait(SSC_WAIT_TIMEOUT_TICKS);
        ssc_service_cycle();
    }

    HW_Release();
}

static int ssc_start(void)
{
    rt_err_t result;

    if (g_ssc_thread_initialized)
    {
        return RT_EOK;
    }

    result = rt_thread_init(&g_ssc_thread,
                            "ssc",
                            ssc_thread_entry,
                            RT_NULL,
                            g_ssc_stack,
                            sizeof(g_ssc_stack),
                            SSC_THREAD_PRIORITY,
                            SSC_THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        return result;
    }

    g_ssc_thread_initialized = RT_TRUE;
    return rt_thread_startup(&g_ssc_thread);
}
INIT_APP_EXPORT(ssc_start);

extern volatile rt_uint32_t g_lan9252_spi_fault_count;
extern volatile rt_uint32_t g_lan9252_spi_last_sr;
extern volatile rt_uint32_t g_lan9252_command_timeout_count;

static rt_uint32_t average_us(volatile rt_uint64_t total,
                             volatile rt_uint32_t count)
{
    return (count > 0U) ? (rt_uint32_t)(total / count) : 0U;
}

static int ssc_stat(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("SSC v11 cycles=%u max/avg=%u/%uus over100us=%u main=%u deferred=%u\r\n",
               g_ssc_service_count,
               g_ssc_service_max_us,
               average_us(g_ssc_service_total_us, g_ssc_service_count),
               g_ssc_service_over_budget,
               g_ssc_mainloop_count,
               g_ssc_mainloop_deferred);
    rt_kprintf("runtime max/avg(us): PDI=%u/%u SYNC0=%u/%u SYNC1=%u/%u main=%u/%u\r\n",
               g_ssc_pdi_max_us,
               average_us(g_ssc_pdi_total_us, g_ssc_pdi_count),
               g_ssc_sync0_max_us,
               average_us(g_ssc_sync0_total_us,
                          ssc_dc_sync0_service_count()),
               g_ssc_sync1_max_us,
               average_us(g_ssc_sync1_total_us,
                          ssc_dc_sync1_service_count()),
               g_ssc_mainloop_max_us,
               average_us(g_ssc_mainloop_total_us, g_ssc_mainloop_count));
    rt_kprintf("latency max(us): AL=%u SYNC0=%u SYNC1=%u pending=%u/%u\r\n",
               g_ssc_pdi_max_latency_us,
               g_ssc_sync0_max_latency_us,
               g_ssc_sync1_max_latency_us,
               ssc_irq_get_pending_count(),
               ssc_irq_get_pending_max());
    rt_kprintf("events: AL=%u service=%u stale=%u coalesced=%u; SYNC0=%u service=%u coalesced=%u\r\n",
               ssc_irq_get_edge_count(),
               ssc_irq_get_pdi_service_count(),
               g_ssc_stale_al,
               g_ssc_coalesced_al,
               ssc_dc_sync0_edge_count(),
               ssc_dc_sync0_service_count(),
               g_ssc_coalesced_sync0);
    rt_kprintf("LAN9252: spi_fault=%u last_sr=0x%08X cmd_timeout=%u\r\n",
               g_lan9252_spi_fault_count,
               g_lan9252_spi_last_sr,
               g_lan9252_command_timeout_count);
    rt_kprintf("last AL error: count=%u status=0x%02X code=0x%04X missed=%u cycle_exceeded=%u sync=%u\r\n",
               g_ssc_al_error_count,
               g_ssc_last_error_al_status,
               g_ssc_last_error_al_code,
               g_ssc_last_error_sm_missed,
               g_ssc_last_error_cycle_exceeded,
               g_ssc_last_error_sync_flag);
    rt_kprintf("error snapshot: pending=%u/%u SYNC0=%u/%u coalesced AL=%u S0=%u S1=%u\r\n",
               g_ssc_last_error_pending,
               g_ssc_last_error_pending_max,
               g_ssc_last_error_sync0_services,
               g_ssc_last_error_sync0_edges,
               g_ssc_last_error_coalesced_al,
               g_ssc_last_error_coalesced_sync0,
               g_ssc_last_error_coalesced_sync1);
    return 0;
}
MSH_CMD_EXPORT(ssc_stat, Show EtherCAT SSC v11 real-time diagnostics);
