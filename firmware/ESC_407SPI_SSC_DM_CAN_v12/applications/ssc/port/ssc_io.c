#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "ssc_io.h"

/*
 * Vendor board mapping:
 *   LED1..LED5 : PB11..PB15, active high
 *   LED6..LED8 : PF13..PF15, active high
 *   KEY1..KEY3 : PE2..PE4, active low
 *   KEY4       : PA0, active high
 *   KEY5       : PA2, active low
 *   KEY6..KEY8 : PG7..PG9, active low
 *
 * Initialization may use RT-Thread's pin API. The 2 ms cyclic path uses IDR
 * and BSRR directly so eight process-data bits are sampled/updated in batches.
 */

static rt_bool_t g_io_initialized = RT_FALSE;
static volatile rt_uint8_t g_led_mask = 0U;

static inline void write_led_mask_hw(rt_uint8_t mask)
{
    rt_uint32_t pb_all = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                         GPIO_PIN_14 | GPIO_PIN_15;
    rt_uint32_t pf_all = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    rt_uint32_t pb_set = ((rt_uint32_t)(mask & 0x1FU)) << 11U;
    rt_uint32_t pf_set = ((rt_uint32_t)((mask >> 5U) & 0x07U)) << 13U;

    GPIOB->BSRR = pb_set | ((pb_all & ~pb_set) << 16U);
    GPIOF->BSRR = pf_set | ((pf_all & ~pf_set) << 16U);
}

rt_err_t ssc_io_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (g_io_initialized)
    {
        return RT_EOK;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
               GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOG, &gpio);

    gpio.Pull = GPIO_PULLDOWN;
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOA, &gpio);

    g_led_mask = 0U;
    write_led_mask_hw(0U);
    g_io_initialized = RT_TRUE;
    return RT_EOK;
}

rt_uint8_t ssc_io_get_key_mask(void)
{
    rt_uint32_t pe = GPIOE->IDR;
    rt_uint32_t pa = GPIOA->IDR;
    rt_uint32_t pg = GPIOG->IDR;
    rt_uint8_t mask = 0U;

    if ((pe & GPIO_PIN_2) == 0U) mask |= (1U << 0U);
    if ((pe & GPIO_PIN_3) == 0U) mask |= (1U << 1U);
    if ((pe & GPIO_PIN_4) == 0U) mask |= (1U << 2U);
    if ((pa & GPIO_PIN_0) != 0U) mask |= (1U << 3U);
    if ((pa & GPIO_PIN_2) == 0U) mask |= (1U << 4U);
    if ((pg & GPIO_PIN_7) == 0U) mask |= (1U << 5U);
    if ((pg & GPIO_PIN_8) == 0U) mask |= (1U << 6U);
    if ((pg & GPIO_PIN_9) == 0U) mask |= (1U << 7U);

    return mask;
}

rt_uint8_t ssc_io_read_key(rt_uint8_t index)
{
    if (index >= SSC_IO_CHANNEL_COUNT)
    {
        return 0U;
    }

    return (rt_uint8_t)((ssc_io_get_key_mask() >> index) & 0x01U);
}

void ssc_io_write_led_mask(rt_uint8_t mask)
{
    write_led_mask_hw(mask);
    g_led_mask = mask;
}

void ssc_io_write_led(rt_uint8_t index, rt_uint8_t on)
{
    rt_uint8_t mask;

    if (index >= SSC_IO_CHANNEL_COUNT)
    {
        return;
    }

    mask = g_led_mask;
    if (on != 0U)
    {
        mask |= (rt_uint8_t)(1U << index);
    }
    else
    {
        mask &= (rt_uint8_t)~(1U << index);
    }

    ssc_io_write_led_mask(mask);
}

rt_uint8_t ssc_io_get_led_mask(void)
{
    return g_led_mask;
}

void ssc_io_all_leds_off(void)
{
    ssc_io_write_led_mask(0U);
}
