#ifndef SSC_IO_H
#define SSC_IO_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSC_IO_CHANNEL_COUNT 8U

rt_err_t  ssc_io_init(void);
rt_uint8_t ssc_io_read_key(rt_uint8_t index);
rt_uint8_t ssc_io_get_key_mask(void);
void       ssc_io_write_led(rt_uint8_t index, rt_uint8_t on);
void       ssc_io_write_led_mask(rt_uint8_t mask);
rt_uint8_t ssc_io_get_led_mask(void);
void       ssc_io_all_leds_off(void);

#ifdef __cplusplus
}
#endif

#endif /* SSC_IO_H */
