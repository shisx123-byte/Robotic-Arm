#include "ecat_def.h"

#if EL9800_HW
#include <rtthread.h>
#include "lan9252.h"
#include "ecatslv.h"
#include "ssc_dc_sync.h"

#define _EL9800HW_ 1
#include "el9800hw.h"
#undef _EL9800HW_

#define SSC_ESC_AL_EVENT_MASK_ADDR      0x0204U
#define SSC_ESC_AL_EVENT_ADDR           0x0220U
#define SSC_PDI_TEST_MASK               0x00000093UL
#define SSC_READY_RETRY                 200U
#define SSC_READY_DELAY_MS              10U

UINT16 uhADCxConvertedValue = 0;

static rt_tick_t g_timer_base;
static volatile UINT32 g_hw_error_count;
volatile UINT16 g_hw_last_error_address;
volatile UINT16 g_hw_last_error_length;
volatile rt_err_t g_hw_last_error_code;

static void ssc_hw_record_error(const char *operation,
                                UINT16 address,
                                UINT16 length,
                                rt_err_t error)
{
    RT_UNUSED(operation);
    g_hw_error_count++;
    g_hw_last_error_address = address;
    g_hw_last_error_length = length;
    g_hw_last_error_code = error;
}

/* LAN9252 CSR accesses must not cross a 32-bit CSR data boundary. */
static UINT16 ssc_get_access_length(UINT16 address, UINT16 remaining)
{
    UINT16 length;

    if (address >= LAN9252_ESC_PRAM_START)
    {
        return remaining;
    }

    length = (remaining > 4U) ? 4U : remaining;

    if ((address & 0x0001U) != 0U)
    {
        length = 1U;
    }
    else if ((address & 0x0002U) != 0U)
    {
        length = ((length & 0x0001U) != 0U) ? 1U : 2U;
    }
    else if (length == 3U)
    {
        length = 1U;
    }

    return length;
}

UINT32 ssc_hw_get_timer(void)
{
    return (UINT32)(rt_tick_get() - g_timer_base);
}

void ssc_hw_clear_timer(void)
{
    g_timer_base = rt_tick_get();
}

UINT32 ssc_hw_get_error_count(void)
{
    return g_hw_error_count;
}

UINT8 HW_Init(void)
{
    UINT32 write_mask = SSC_PDI_TEST_MASK;
    UINT32 read_mask = 0;
    UINT32 retry;
    rt_err_t result;

    result = ssc_io_init();
    if (result != RT_EOK)
    {
        rt_kprintf("[SSC] GPIO initialization failed: %d\r\n", result);
        return 1U;
    }

    result = ssc_irq_init();
    if (result != RT_EOK)
    {
        rt_kprintf("[SSC] IRQ GPIO initialization failed: %d\r\n", result);
        return 2U;
    }

    result = ssc_dc_sync_init();
    if (result != RT_EOK)
    {
        rt_kprintf("[SSC] SYNC0/SYNC1 GPIO initialization failed: %d\r\n", result);
        return 3U;
    }

    result = lan9252_init();
    if (result != RT_EOK)
    {
        rt_kprintf("[SSC] lan9252_init failed: %d\r\n", result);
        return 4U;
    }

    for (retry = 0; retry < SSC_READY_RETRY; retry++)
    {
        if (lan9252_is_ready())
        {
            break;
        }
        rt_thread_mdelay(SSC_READY_DELAY_MS);
    }

    if (!lan9252_is_ready())
    {
        rt_kprintf("[SSC] LAN9252 is not ready\r\n");
        return 5U;
    }

    /* Check that the PDI can write and read the ESC AL event mask. */
    result = lan9252_esc_write(SSC_ESC_AL_EVENT_MASK_ADDR,
                               (const rt_uint8_t *)&write_mask,
                               sizeof(write_mask));
    if (result != RT_EOK)
    {
        ssc_hw_record_error("PDI mask write",
                            SSC_ESC_AL_EVENT_MASK_ADDR,
                            sizeof(write_mask),
                            result);
        return 6U;
    }

    result = lan9252_esc_read(SSC_ESC_AL_EVENT_MASK_ADDR,
                              (rt_uint8_t *)&read_mask,
                              sizeof(read_mask));
    if ((result != RT_EOK) || (read_mask != write_mask))
    {
        if (result == RT_EOK)
        {
            result = -RT_ERROR;
        }
        ssc_hw_record_error("PDI mask readback",
                            SSC_ESC_AL_EVENT_MASK_ADDR,
                            sizeof(read_mask),
                            result);
        return 7U;
    }

    write_mask = 0U;
    (void)lan9252_esc_write(SSC_ESC_AL_EVENT_MASK_ADDR,
                            (const rt_uint8_t *)&write_mask,
                            sizeof(write_mask));

    /* Enable the LAN9252 EtherCAT IRQ output. The vendor reference connects
     * this active-low signal to STM32 PC0. */
    result = lan9252_reg_write32(LAN9252_REG_IRQ_CFG,
                                 LAN9252_IRQ_CFG_ACTIVE_LOW);
    if (result != RT_EOK)
    {
        rt_kprintf("[SSC] LAN9252 IRQ_CFG write failed: %d\r\n", result);
        return 8U;
    }

    result = lan9252_reg_write32(LAN9252_REG_INT_EN,
                                 LAN9252_INT_EN_ECAT);
    if (result != RT_EOK)
    {
        rt_kprintf("[SSC] LAN9252 INT_EN write failed: %d\r\n", result);
        return 9U;
    }

    /* Read once for diagnostics and to observe any already-latched source. */
    (void)lan9252_reg_read32(LAN9252_REG_INT_STS);

    result = ssc_irq_start();
    if (result != RT_EOK)
    {
        (void)lan9252_reg_write32(LAN9252_REG_INT_EN, 0U);
        rt_kprintf("[SSC] IRQ enable failed: %d\r\n", result);
        return 10U;
    }

    result = ssc_dc_sync_start();
    if (result != RT_EOK)
    {
        ssc_irq_stop();
        (void)lan9252_reg_write32(LAN9252_REG_INT_EN, 0U);
        rt_kprintf("[SSC] SYNC0/SYNC1 IRQ enable failed: %d\r\n", result);
        return 11U;
    }

    g_hw_error_count = 0U;
    ssc_hw_clear_timer();

    return 0U;
}

void HW_Release(void)
{
    ssc_dc_sync_stop();
    ssc_irq_stop();
    (void)lan9252_reg_write32(LAN9252_REG_INT_EN, 0U);
    ssc_io_all_leds_off();
}

UINT16 HW_GetALEventRegister(void)
{
    UINT16 event = 0U;
    HW_EscRead((MEM_ADDR *)&event, SSC_ESC_AL_EVENT_ADDR, sizeof(event));
    return event;
}

UINT16 HW_GetALEventRegister_Isr(void)
{
    UINT16 event = 0U;
    HW_EscReadIsr((MEM_ADDR *)&event, SSC_ESC_AL_EVENT_ADDR, sizeof(event));
    return event;
}

void HW_SetLed(UINT8 RunLed, UINT8 ErrLed)
{
    RT_UNUSED(RunLed);
    RT_UNUSED(ErrLed);
}

void HW_EscRead(MEM_ADDR *pData, UINT16 Address, UINT16 Len)
{
    UINT8 *buffer = (UINT8 *)pData;

    while (Len > 0U)
    {
        UINT16 access_len = ssc_get_access_length(Address, Len);
        rt_err_t result = lan9252_esc_read(Address,
                                           (rt_uint8_t *)buffer,
                                           access_len);
        if (result != RT_EOK)
        {
            rt_memset(buffer, 0, access_len);
            ssc_hw_record_error("ESC read", Address, access_len, result);
        }

        Address = (UINT16)(Address + access_len);
        buffer += access_len;
        Len = (UINT16)(Len - access_len);
    }
}

void HW_EscReadIsr(MEM_ADDR *pData, UINT16 Address, UINT16 Len)
{
    HW_EscRead(pData, Address, Len);
}

void HW_EscWrite(MEM_ADDR *pData, UINT16 Address, UINT16 Len)
{
    UINT8 *buffer = (UINT8 *)pData;

    while (Len > 0U)
    {
        UINT16 access_len = ssc_get_access_length(Address, Len);
        rt_err_t result = lan9252_esc_write(Address,
                                            (const rt_uint8_t *)buffer,
                                            access_len);
        if (result != RT_EOK)
        {
            ssc_hw_record_error("ESC write", Address, access_len, result);
        }

        Address = (UINT16)(Address + access_len);
        buffer += access_len;
        Len = (UINT16)(Len - access_len);
    }
}

void HW_EscWriteIsr(MEM_ADDR *pData, UINT16 Address, UINT16 Len)
{
    HW_EscWrite(pData, Address, Len);
}

#endif /* EL9800_HW */
