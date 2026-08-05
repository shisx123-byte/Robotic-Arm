#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "ssc_irq.h"

#ifndef SSC_LAN9252_IRQ_PIN
#define SSC_LAN9252_IRQ_PIN       GET_PIN(C, 0)
#endif

#ifndef SSC_LAN9252_IRQ_PIN_NAME
#define SSC_LAN9252_IRQ_PIN_NAME  "PC0"
#endif

#define SSC_LAN9252_IRQ_GPIO_PORT     GPIOC
#define SSC_LAN9252_IRQ_GPIO_MASK     GPIO_PIN_0

static struct rt_semaphore g_ssc_irq_sem;
static volatile rt_bool_t g_ssc_irq_initialized = RT_FALSE;
static volatile rt_bool_t g_ssc_irq_enabled = RT_FALSE;
static volatile rt_bool_t g_ssc_wakeup_queued = RT_FALSE;

static volatile rt_uint32_t g_sequence;
static volatile rt_uint32_t g_al_pending;
static volatile rt_uint32_t g_sync0_pending;
static volatile rt_uint32_t g_sync1_pending;
static volatile rt_uint32_t g_al_sequence;
static volatile rt_uint32_t g_sync0_sequence;
static volatile rt_uint32_t g_sync1_sequence;
static volatile rt_uint32_t g_al_timestamp_us;
static volatile rt_uint32_t g_sync0_timestamp_us;
static volatile rt_uint32_t g_sync1_timestamp_us;
static volatile rt_uint32_t g_pending_max;

static volatile rt_uint32_t g_ssc_irq_edge_count;
static volatile rt_uint32_t g_ssc_irq_wakeup_count;
static volatile rt_uint32_t g_ssc_irq_pdi_service_count;
static volatile rt_uint32_t g_ssc_irq_active_after_service_count;

static rt_uint32_t pending_total_locked(void)
{
    return g_al_pending + g_sync0_pending + g_sync1_pending;
}

rt_bool_t ssc_irq_queue_event(ssc_irq_event_type_t type)
{
    rt_base_t level;
    rt_bool_t release = RT_FALSE;
    rt_uint32_t sequence;
    rt_uint32_t timestamp_us;
    rt_uint32_t pending;

    if (!g_ssc_irq_initialized || (type == SSC_IRQ_EVENT_NONE))
    {
        return RT_FALSE;
    }

    /* TIM5 is a 1 MHz free-running timer initialized before GPIO IRQs start. */
    timestamp_us = TIM5->CNT;
    level = rt_hw_interrupt_disable();
    sequence = ++g_sequence;

    switch (type)
    {
        case SSC_IRQ_EVENT_AL:
            g_al_pending++;
            g_al_sequence = sequence;
            g_al_timestamp_us = timestamp_us;
            break;
        case SSC_IRQ_EVENT_SYNC0:
            g_sync0_pending++;
            g_sync0_sequence = sequence;
            g_sync0_timestamp_us = timestamp_us;
            break;
        case SSC_IRQ_EVENT_SYNC1:
            g_sync1_pending++;
            g_sync1_sequence = sequence;
            g_sync1_timestamp_us = timestamp_us;
            break;
        default:
            rt_hw_interrupt_enable(level);
            return RT_FALSE;
    }

    pending = pending_total_locked();
    if (pending > g_pending_max)
    {
        g_pending_max = pending;
    }

    if (!g_ssc_wakeup_queued)
    {
        g_ssc_wakeup_queued = RT_TRUE;
        release = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    if (release)
    {
        (void)rt_sem_release(&g_ssc_irq_sem);
    }

    return RT_TRUE;
}

static void ssc_lan9252_irq_handler(void *parameter)
{
    RT_UNUSED(parameter);
    g_ssc_irq_edge_count++;
    (void)ssc_irq_queue_event(SSC_IRQ_EVENT_AL);
}

rt_err_t ssc_irq_init(void)
{
    rt_err_t result;

    if (g_ssc_irq_initialized)
    {
        return RT_EOK;
    }

    result = rt_sem_init(&g_ssc_irq_sem, "sscirq", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        return result;
    }

    rt_pin_mode(SSC_LAN9252_IRQ_PIN, PIN_MODE_INPUT_PULLUP);
    result = rt_pin_attach_irq(SSC_LAN9252_IRQ_PIN,
                               PIN_IRQ_MODE_FALLING,
                               ssc_lan9252_irq_handler,
                               RT_NULL);
    if (result != RT_EOK)
    {
        rt_sem_detach(&g_ssc_irq_sem);
        return result;
    }

    g_sequence = 0U;
    g_al_pending = 0U;
    g_sync0_pending = 0U;
    g_sync1_pending = 0U;
    g_pending_max = 0U;
    g_ssc_irq_edge_count = 0U;
    g_ssc_irq_wakeup_count = 0U;
    g_ssc_irq_pdi_service_count = 0U;
    g_ssc_irq_active_after_service_count = 0U;
    g_ssc_wakeup_queued = RT_FALSE;
    g_ssc_irq_enabled = RT_FALSE;
    g_ssc_irq_initialized = RT_TRUE;

    return RT_EOK;
}

rt_err_t ssc_irq_start(void)
{
    rt_err_t result;

    if (!g_ssc_irq_initialized)
    {
        result = ssc_irq_init();
        if (result != RT_EOK)
        {
            return result;
        }
    }

    result = rt_pin_irq_enable(SSC_LAN9252_IRQ_PIN, PIN_IRQ_ENABLE);
    if (result == RT_EOK)
    {
        HAL_NVIC_SetPriority(EXTI0_IRQn, 0U, 0U);
        g_ssc_irq_enabled = RT_TRUE;
    }

    return result;
}

void ssc_irq_stop(void)
{
    if (g_ssc_irq_initialized && g_ssc_irq_enabled)
    {
        (void)rt_pin_irq_enable(SSC_LAN9252_IRQ_PIN, PIN_IRQ_DISABLE);
        g_ssc_irq_enabled = RT_FALSE;
    }
}

rt_err_t ssc_irq_wait(rt_int32_t timeout)
{
    rt_err_t result;

    if (!g_ssc_irq_initialized)
    {
        return -RT_ERROR;
    }

    result = rt_sem_take(&g_ssc_irq_sem, timeout);
    if (result == RT_EOK)
    {
        g_ssc_irq_wakeup_count++;
    }
    return result;
}

rt_bool_t ssc_irq_take_snapshot(ssc_irq_snapshot_t *snapshot)
{
    rt_base_t level;
    rt_bool_t available;

    if (snapshot == RT_NULL)
    {
        return RT_FALSE;
    }

    level = rt_hw_interrupt_disable();
    snapshot->al_count = g_al_pending;
    snapshot->sync0_count = g_sync0_pending;
    snapshot->sync1_count = g_sync1_pending;
    snapshot->al_sequence = g_al_sequence;
    snapshot->sync0_sequence = g_sync0_sequence;
    snapshot->sync1_sequence = g_sync1_sequence;
    snapshot->al_timestamp_us = g_al_timestamp_us;
    snapshot->sync0_timestamp_us = g_sync0_timestamp_us;
    snapshot->sync1_timestamp_us = g_sync1_timestamp_us;

    available = ((g_al_pending | g_sync0_pending | g_sync1_pending) != 0U)
                    ? RT_TRUE : RT_FALSE;
    g_al_pending = 0U;
    g_sync0_pending = 0U;
    g_sync1_pending = 0U;
    g_ssc_wakeup_queued = RT_FALSE;
    rt_hw_interrupt_enable(level);

    return available;
}

rt_bool_t ssc_irq_events_pending(void)
{
    return ((g_al_pending | g_sync0_pending | g_sync1_pending) != 0U)
               ? RT_TRUE : RT_FALSE;
}

rt_bool_t ssc_irq_is_active(void)
{
    if (!g_ssc_irq_initialized)
    {
        return RT_FALSE;
    }

    return ((SSC_LAN9252_IRQ_GPIO_PORT->IDR & SSC_LAN9252_IRQ_GPIO_MASK) == 0U)
               ? RT_TRUE : RT_FALSE;
}

rt_base_t ssc_irq_get_level(void)
{
    if (!g_ssc_irq_initialized)
    {
        return PIN_HIGH;
    }

    return ((SSC_LAN9252_IRQ_GPIO_PORT->IDR & SSC_LAN9252_IRQ_GPIO_MASK) != 0U)
               ? PIN_HIGH : PIN_LOW;
}

const char *ssc_irq_get_pin_name(void) { return SSC_LAN9252_IRQ_PIN_NAME; }
void ssc_irq_note_pdi_service(void) { g_ssc_irq_pdi_service_count++; }
void ssc_irq_note_active_after_service(void) { g_ssc_irq_active_after_service_count++; }
rt_bool_t ssc_irq_is_enabled(void) { return g_ssc_irq_enabled; }
rt_uint32_t ssc_irq_get_edge_count(void) { return g_ssc_irq_edge_count; }
rt_uint32_t ssc_irq_get_wakeup_count(void) { return g_ssc_irq_wakeup_count; }
rt_uint32_t ssc_irq_get_pdi_service_count(void) { return g_ssc_irq_pdi_service_count; }
rt_uint32_t ssc_irq_get_active_after_service_count(void) { return g_ssc_irq_active_after_service_count; }
rt_uint32_t ssc_irq_get_pending_count(void)
{
    rt_base_t level;
    rt_uint32_t pending;
    level = rt_hw_interrupt_disable();
    pending = pending_total_locked();
    rt_hw_interrupt_enable(level);
    return pending;
}
rt_uint32_t ssc_irq_get_pending_max(void) { return g_pending_max; }
