#ifndef __SSC_IRQ_H__
#define __SSC_IRQ_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SSC_IRQ_EVENT_NONE  = 0,
    SSC_IRQ_EVENT_AL    = 1,
    SSC_IRQ_EVENT_SYNC0 = 2,
    SSC_IRQ_EVENT_SYNC1 = 3
} ssc_irq_event_type_t;

typedef struct
{
    rt_uint32_t al_count;
    rt_uint32_t sync0_count;
    rt_uint32_t sync1_count;
    rt_uint32_t al_sequence;
    rt_uint32_t sync0_sequence;
    rt_uint32_t sync1_sequence;
    rt_uint32_t al_timestamp_us;
    rt_uint32_t sync0_timestamp_us;
    rt_uint32_t sync1_timestamp_us;
} ssc_irq_snapshot_t;

rt_err_t ssc_irq_init(void);
rt_err_t ssc_irq_start(void);
void     ssc_irq_stop(void);
rt_err_t ssc_irq_wait(rt_int32_t timeout);

/* ISR-side latest-state aggregation. No SPI access or event allocation. */
rt_bool_t ssc_irq_queue_event(ssc_irq_event_type_t type);
rt_bool_t ssc_irq_take_snapshot(ssc_irq_snapshot_t *snapshot);
rt_bool_t ssc_irq_events_pending(void);

rt_bool_t ssc_irq_is_active(void);
rt_base_t ssc_irq_get_level(void);
const char *ssc_irq_get_pin_name(void);

void ssc_irq_note_pdi_service(void);
void ssc_irq_note_active_after_service(void);

rt_bool_t   ssc_irq_is_enabled(void);
rt_uint32_t ssc_irq_get_edge_count(void);
rt_uint32_t ssc_irq_get_wakeup_count(void);
rt_uint32_t ssc_irq_get_pdi_service_count(void);
rt_uint32_t ssc_irq_get_active_after_service_count(void);
rt_uint32_t ssc_irq_get_pending_count(void);
rt_uint32_t ssc_irq_get_pending_max(void);

#ifdef __cplusplus
}
#endif

#endif /* __SSC_IRQ_H__ */
