#ifndef __SSC_DC_SYNC_H__
#define __SSC_DC_SYNC_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Vendor STM32F407 + LAN9252 reference wiring:
 *   SYNC0 -> PC3, active-low pulse
 *   SYNC1 -> PC1, active-low pulse
 * GPIO ISRs only count/queue an event and wake the single SSC owner thread.
 */
rt_err_t ssc_dc_sync_init(void);
rt_err_t ssc_dc_sync_start(void);
void     ssc_dc_sync_stop(void);

rt_bool_t ssc_dc_sync_take_sync0(void);
rt_bool_t ssc_dc_sync_take_sync1(void);
void      ssc_dc_sync_note_sync0_service(void);
void      ssc_dc_sync_note_sync1_service(void);
void      ssc_dc_sync_note_sync0_drop(rt_uint32_t count);
void      ssc_dc_sync_note_sync1_drop(rt_uint32_t count);

const char *ssc_dc_sync0_pin_name(void);
const char *ssc_dc_sync1_pin_name(void);
rt_base_t   ssc_dc_sync0_level(void);
rt_base_t   ssc_dc_sync1_level(void);
rt_bool_t   ssc_dc_sync_is_enabled(void);

rt_uint32_t ssc_dc_sync0_edge_count(void);
rt_uint32_t ssc_dc_sync1_edge_count(void);
rt_uint32_t ssc_dc_sync0_service_count(void);
rt_uint32_t ssc_dc_sync1_service_count(void);
rt_uint32_t ssc_dc_sync0_pending_count(void);
rt_uint32_t ssc_dc_sync1_pending_count(void);
rt_uint32_t ssc_dc_sync0_max_backlog(void);
rt_uint32_t ssc_dc_sync1_max_backlog(void);

#ifdef __cplusplus
}
#endif

#endif /* __SSC_DC_SYNC_H__ */
