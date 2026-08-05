#ifndef __LAN9252_H__
#define __LAN9252_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAN9252_REG_ID_REV         0x0050U
#define LAN9252_REG_IRQ_CFG        0x0054U
#define LAN9252_REG_INT_STS        0x0058U
#define LAN9252_REG_INT_EN         0x005CU
#define LAN9252_REG_BYTE_TEST      0x0064U
#define LAN9252_REG_HW_CFG         0x0074U

/* Same interrupt configuration used by the vendor STM32F407 reference:
 * IRQ output enabled, active low, push-pull; EtherCAT interrupt source enabled. */
#define LAN9252_IRQ_CFG_ACTIVE_LOW 0x00000101UL
#define LAN9252_INT_EN_ECAT        0x00000001UL
#define LAN9252_BYTE_TEST_VALUE    0x87654321UL
#define LAN9252_ID_REV_VALUE       0x92520001UL
#define LAN9252_HW_CFG_READY       (1UL << 27)

#define LAN9252_ESC_PRAM_START     0x1000U
#define LAN9252_ESC_PRAM_END       0x1FFFU

rt_err_t lan9252_init(void);
rt_bool_t lan9252_is_ready(void);

rt_err_t lan9252_reg_read(rt_uint16_t address, rt_uint8_t *buffer, rt_size_t length);
rt_err_t lan9252_reg_write(rt_uint16_t address, const rt_uint8_t *buffer, rt_size_t length);
rt_uint32_t lan9252_reg_read32(rt_uint16_t address);
rt_err_t lan9252_reg_write32(rt_uint16_t address, rt_uint32_t value);

rt_err_t lan9252_csr_read(rt_uint16_t esc_address, rt_uint8_t *buffer, rt_size_t length);
rt_err_t lan9252_csr_write(rt_uint16_t esc_address, const rt_uint8_t *buffer, rt_size_t length);

rt_err_t lan9252_pram_read(rt_uint16_t esc_address, rt_uint8_t *buffer, rt_size_t length);
rt_err_t lan9252_pram_write(rt_uint16_t esc_address, const rt_uint8_t *buffer, rt_size_t length);

rt_err_t lan9252_esc_read(rt_uint16_t esc_address, rt_uint8_t *buffer, rt_size_t length);
rt_err_t lan9252_esc_write(rt_uint16_t esc_address, const rt_uint8_t *buffer, rt_size_t length);

#ifdef __cplusplus
}
#endif

#endif
