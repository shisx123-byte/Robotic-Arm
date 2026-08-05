#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "ssc_irq.h"
#include "ssc_dc_sync.h"

#ifndef SSC_LAN9252_SYNC0_PIN
#define SSC_LAN9252_SYNC0_PIN       GET_PIN(C, 3)
#endif
#ifndef SSC_LAN9252_SYNC1_PIN
#define SSC_LAN9252_SYNC1_PIN       GET_PIN(C, 1)
#endif
#ifndef SSC_LAN9252_SYNC0_PIN_NAME
#define SSC_LAN9252_SYNC0_PIN_NAME  "PC3"
#endif
#ifndef SSC_LAN9252_SYNC1_PIN_NAME
#define SSC_LAN9252_SYNC1_PIN_NAME  "PC1"
#endif

static volatile rt_bool_t g_initialized = RT_FALSE;
static volatile rt_bool_t g_enabled = RT_FALSE;
volatile rt_uint32_t g_sync0_edges;
volatile rt_uint32_t g_sync1_edges;
volatile rt_uint32_t g_sync0_pending;
volatile rt_uint32_t g_sync1_pending;
volatile rt_uint32_t g_sync0_services;
volatile rt_uint32_t g_sync1_services;
volatile rt_uint32_t g_sync0_max_backlog;
volatile rt_uint32_t g_sync1_max_backlog;

static void ssc_sync0_irq_handler(void *parameter)
{
    RT_UNUSED(parameter);

    g_sync0_edges++;
    if (ssc_irq_queue_event(SSC_IRQ_EVENT_SYNC0))
    {
        g_sync0_pending++;
        if (g_sync0_pending > g_sync0_max_backlog)
        {
            g_sync0_max_backlog = g_sync0_pending;
        }
    }
}

static void ssc_sync1_irq_handler(void *parameter)
{
    RT_UNUSED(parameter);

    g_sync1_edges++;
    if (ssc_irq_queue_event(SSC_IRQ_EVENT_SYNC1))
    {
        g_sync1_pending++;
        if (g_sync1_pending > g_sync1_max_backlog)
        {
            g_sync1_max_backlog = g_sync1_pending;
        }
    }
}

rt_err_t ssc_dc_sync_init(void)
{
    rt_err_t result;

    if (g_initialized)
    {
        return RT_EOK;
    }

    rt_pin_mode(SSC_LAN9252_SYNC0_PIN, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(SSC_LAN9252_SYNC1_PIN, PIN_MODE_INPUT_PULLUP);

    result = rt_pin_attach_irq(SSC_LAN9252_SYNC0_PIN,
                               PIN_IRQ_MODE_FALLING,
                               ssc_sync0_irq_handler,
                               RT_NULL);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_pin_attach_irq(SSC_LAN9252_SYNC1_PIN,
                               PIN_IRQ_MODE_FALLING,
                               ssc_sync1_irq_handler,
                               RT_NULL);
    if (result != RT_EOK)
    {
        (void)rt_pin_detach_irq(SSC_LAN9252_SYNC0_PIN);
        return result;
    }

    g_sync0_edges = 0U;
    g_sync1_edges = 0U;
    g_sync0_pending = 0U;
    g_sync1_pending = 0U;
    g_sync0_services = 0U;
    g_sync1_services = 0U;
    g_sync0_max_backlog = 0U;
    g_sync1_max_backlog = 0U;
    g_enabled = RT_FALSE;
    g_initialized = RT_TRUE;
    return RT_EOK;
}

rt_err_t ssc_dc_sync_start(void)
{
    rt_err_t result;

    if (!g_initialized)
    {
        result = ssc_dc_sync_init();
        if (result != RT_EOK)
        {
            return result;
        }
    }

    result = rt_pin_irq_enable(SSC_LAN9252_SYNC0_PIN, PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_pin_irq_enable(SSC_LAN9252_SYNC1_PIN, PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        (void)rt_pin_irq_enable(SSC_LAN9252_SYNC0_PIN, PIN_IRQ_DISABLE);
        return result;
    }

    /* Keep IRQ work bounded: timestamp/queue only. The SSC owner thread runs
     * the handlers without allowing an ISR to interrupt an SPI transaction. */
    HAL_NVIC_SetPriority(EXTI3_IRQn, 1U, 0U);
    HAL_NVIC_SetPriority(EXTI1_IRQn, 2U, 0U);

    g_enabled = RT_TRUE;
    return RT_EOK;
}

void ssc_dc_sync_stop(void)
{
    if (g_initialized && g_enabled)
    {
        (void)rt_pin_irq_enable(SSC_LAN9252_SYNC0_PIN, PIN_IRQ_DISABLE);
        (void)rt_pin_irq_enable(SSC_LAN9252_SYNC1_PIN, PIN_IRQ_DISABLE);
        g_enabled = RT_FALSE;
    }
}

static rt_bool_t take_pending(volatile rt_uint32_t *counter)
{
    rt_base_t level;
    rt_bool_t result = RT_FALSE;

    level = rt_hw_interrupt_disable();
    if (*counter > 0U)
    {
        (*counter)--;
        result = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

rt_bool_t ssc_dc_sync_take_sync0(void)
{
    return take_pending(&g_sync0_pending);
}

rt_bool_t ssc_dc_sync_take_sync1(void)
{
    return take_pending(&g_sync1_pending);
}

void ssc_dc_sync_note_sync0_service(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (g_sync0_pending > 0U)
    {
        g_sync0_pending--;
    }
    g_sync0_services++;
    rt_hw_interrupt_enable(level);
}

void ssc_dc_sync_note_sync1_service(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (g_sync1_pending > 0U)
    {
        g_sync1_pending--;
    }
    g_sync1_services++;
    rt_hw_interrupt_enable(level);
}

static void drop_pending(volatile rt_uint32_t *counter, rt_uint32_t count)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (count >= *counter)
    {
        *counter = 0U;
    }
    else
    {
        *counter -= count;
    }
    rt_hw_interrupt_enable(level);
}

void ssc_dc_sync_note_sync0_drop(rt_uint32_t count)
{
    drop_pending(&g_sync0_pending, count);
}

void ssc_dc_sync_note_sync1_drop(rt_uint32_t count)
{
    drop_pending(&g_sync1_pending, count);
}

const char *ssc_dc_sync0_pin_name(void) { return SSC_LAN9252_SYNC0_PIN_NAME; }
const char *ssc_dc_sync1_pin_name(void) { return SSC_LAN9252_SYNC1_PIN_NAME; }
rt_base_t ssc_dc_sync0_level(void) { return rt_pin_read(SSC_LAN9252_SYNC0_PIN); }
rt_base_t ssc_dc_sync1_level(void) { return rt_pin_read(SSC_LAN9252_SYNC1_PIN); }
rt_bool_t ssc_dc_sync_is_enabled(void) { return g_enabled; }
rt_uint32_t ssc_dc_sync0_edge_count(void) { return g_sync0_edges; }
rt_uint32_t ssc_dc_sync1_edge_count(void) { return g_sync1_edges; }
rt_uint32_t ssc_dc_sync0_service_count(void) { return g_sync0_services; }
rt_uint32_t ssc_dc_sync1_service_count(void) { return g_sync1_services; }
rt_uint32_t ssc_dc_sync0_pending_count(void) { return g_sync0_pending; }
rt_uint32_t ssc_dc_sync1_pending_count(void) { return g_sync1_pending; }
rt_uint32_t ssc_dc_sync0_max_backlog(void) { return g_sync0_max_backlog; }
rt_uint32_t ssc_dc_sync1_max_backlog(void) { return g_sync1_max_backlog; }
