#include "lan9252.h"
#include <board.h>

/*
 * LAN9252 dedicated SPI1 driver.
 *
 * Real-time rules used here:
 *   1. SPI1 has a single owner: the high-priority SSC thread.
 *   2. The hot path uses STM32 registers directly; no HAL transfer state
 *      machine, RT-Thread SPI bus mutex, dynamic memory, or scheduler wait.
 *   3. Process RAM is transferred with one LAN9252 PRAM command per complete
 *      PDO/mailbox block and FIFO burst access.  Do not split PRAM into a new
 *      abort/address/start command for every four bytes.
 *   4. SPI clock is 42 MHz (/2 from the 84 MHz APB2 peripheral clock).
 *      LAN9252 FASTREAD and WRITE support this rate; board-level validation
 *      is still required after changing from the vendor's 21 MHz setting.
 */

#define LAN9252_CS_GPIO_PORT          GPIOA
#define LAN9252_CS_GPIO_PIN           GPIO_PIN_8

#define LAN9252_SPI_FAST_READ_CMD     0x0BU
#define LAN9252_SPI_WRITE_CMD         0x02U
#define LAN9252_SPI_FAST_DUMMY        0x00U

/* 84 MHz APB2 / 2 = 42 MHz (BR[2:0] = 000). */
#ifndef LAN9252_SPI_BAUD_BITS
#define LAN9252_SPI_BAUD_BITS         0U
#endif

#define LAN9252_ESC_CSR_DATA_REG      0x0300U
#define LAN9252_ESC_CSR_CMD_REG       0x0304U
#define LAN9252_ESC_CSR_BUSY          0x80000000UL
#define LAN9252_ESC_CSR_READ          0x40000000UL

#define LAN9252_PRAM_RD_FIFO_REG      0x0004U
#define LAN9252_PRAM_WR_FIFO_REG      0x0020U
#define LAN9252_PRAM_RD_ADDR_LEN_REG  0x0308U
#define LAN9252_PRAM_RD_CMD_REG       0x030CU
#define LAN9252_PRAM_WR_ADDR_LEN_REG  0x0310U
#define LAN9252_PRAM_WR_CMD_REG       0x0314U
#define LAN9252_PRAM_BUSY             0x80000000UL
#define LAN9252_PRAM_ABORT            0x40000000UL
#define LAN9252_PRAM_AVAIL            0x00000001UL

/* These loops are only fault bounds. Normal completion takes a few SPI polls. */
#define LAN9252_SPI_FLAG_RETRY        100000U
#define LAN9252_COMMAND_RETRY         512U

static rt_bool_t g_lan9252_initialized = RT_FALSE;

/* Debugger-visible fault latches. They are never printed from the cyclic path. */
volatile rt_uint32_t g_lan9252_spi_fault_count = 0U;
volatile rt_uint32_t g_lan9252_spi_last_sr = 0U;
volatile rt_uint32_t g_lan9252_command_timeout_count = 0U;

static rt_uint32_t bytes_to_u32(const rt_uint8_t *data, rt_size_t length)
{
    rt_uint32_t value = 0U;
    rt_size_t i;

    for (i = 0U; i < length; i++)
    {
        value |= ((rt_uint32_t)data[i]) << (8U * i);
    }

    return value;
}

static void u32_to_bytes(rt_uint32_t value, rt_uint8_t *data, rt_size_t length)
{
    rt_size_t i;

    for (i = 0U; i < length; i++)
    {
        data[i] = (rt_uint8_t)(value >> (8U * i));
    }
}

static inline void lan9252_cs_low(void)
{
    LAN9252_CS_GPIO_PORT->BSRR = ((rt_uint32_t)LAN9252_CS_GPIO_PIN << 16U);
}

static inline void lan9252_cs_high(void)
{
    LAN9252_CS_GPIO_PORT->BSRR = (rt_uint32_t)LAN9252_CS_GPIO_PIN;
}

static rt_err_t spi_wait_flag(rt_uint32_t flag, rt_bool_t set)
{
    rt_uint32_t retry = LAN9252_SPI_FLAG_RETRY;

    while (retry-- > 0U)
    {
        rt_bool_t state = ((SPI1->SR & flag) != 0U) ? RT_TRUE : RT_FALSE;
        if (state == set)
        {
            return RT_EOK;
        }
    }

    g_lan9252_spi_fault_count++;
    g_lan9252_spi_last_sr = SPI1->SR;
    return -RT_ETIMEOUT;
}

static inline rt_err_t spi_transfer_byte(rt_uint8_t tx, rt_uint8_t *rx)
{
    rt_err_t result;

    result = spi_wait_flag(SPI_SR_TXE, RT_TRUE);
    if (result != RT_EOK)
    {
        return result;
    }

    *(__IO rt_uint8_t *)&SPI1->DR = tx;

    result = spi_wait_flag(SPI_SR_RXNE, RT_TRUE);
    if (result != RT_EOK)
    {
        return result;
    }

    tx = *(__IO rt_uint8_t *)&SPI1->DR;
    if (rx != RT_NULL)
    {
        *rx = tx;
    }

    return RT_EOK;
}

static rt_err_t spi_finish(void)
{
    rt_err_t result = spi_wait_flag(SPI_SR_BSY, RT_FALSE);

    /* Clear a possible overrun without invoking HAL error handling. */
    if ((SPI1->SR & SPI_SR_OVR) != 0U)
    {
        volatile rt_uint32_t clear;
        clear = SPI1->DR;
        clear = SPI1->SR;
        RT_UNUSED(clear);
    }

    lan9252_cs_high();
    return result;
}

static rt_err_t spi_send_header(rt_uint8_t command, rt_uint16_t address)
{
    rt_err_t result;

    result = spi_transfer_byte(command, RT_NULL);
    if (result != RT_EOK) return result;
    result = spi_transfer_byte((rt_uint8_t)(address >> 8), RT_NULL);
    if (result != RT_EOK) return result;
    return spi_transfer_byte((rt_uint8_t)address, RT_NULL);
}

static rt_err_t fifo_read_burst(rt_uint16_t address,
                                rt_uint8_t *buffer,
                                rt_size_t length)
{
    rt_err_t result;
    rt_uint8_t word[4];
    rt_size_t copy_length;
    rt_size_t i;

    if ((buffer == RT_NULL) || (length == 0U))
    {
        return -RT_EINVAL;
    }

    lan9252_cs_low();
    result = spi_send_header(LAN9252_SPI_FAST_READ_CMD, address);
    if (result == RT_EOK)
    {
        result = spi_transfer_byte(LAN9252_SPI_FAST_DUMMY, RT_NULL);
    }

    while ((result == RT_EOK) && (length > 0U))
    {
        for (i = 0U; i < 4U; i++)
        {
            result = spi_transfer_byte(0U, &word[i]);
            if (result != RT_EOK) break;
        }

        if (result != RT_EOK) break;

        copy_length = (length > 4U) ? 4U : length;
        rt_memcpy(buffer, word, copy_length);
        buffer += copy_length;
        length -= copy_length;
    }

    {
        rt_err_t finish_result = spi_finish();
        if (result == RT_EOK) result = finish_result;
    }

    return result;
}

static rt_err_t fifo_write_burst(rt_uint16_t address,
                                 const rt_uint8_t *buffer,
                                 rt_size_t length)
{
    rt_err_t result;
    rt_uint8_t word[4];
    rt_size_t copy_length;
    rt_size_t i;

    if ((buffer == RT_NULL) || (length == 0U))
    {
        return -RT_EINVAL;
    }

    lan9252_cs_low();
    result = spi_send_header(LAN9252_SPI_WRITE_CMD, address);

    while ((result == RT_EOK) && (length > 0U))
    {
        copy_length = (length > 4U) ? 4U : length;
        word[0] = 0U;
        word[1] = 0U;
        word[2] = 0U;
        word[3] = 0U;
        rt_memcpy(word, buffer, copy_length);

        for (i = 0U; i < 4U; i++)
        {
            result = spi_transfer_byte(word[i], RT_NULL);
            if (result != RT_EOK) break;
        }

        buffer += copy_length;
        length -= copy_length;
    }

    {
        rt_err_t finish_result = spi_finish();
        if (result == RT_EOK) result = finish_result;
    }

    return result;
}

rt_err_t lan9252_reg_read(rt_uint16_t address,
                          rt_uint8_t *buffer,
                          rt_size_t length)
{
    rt_err_t result;
    rt_size_t i;

    if (!g_lan9252_initialized || (buffer == RT_NULL) ||
        (length == 0U) || (length > 4U))
    {
        return -RT_EINVAL;
    }

    lan9252_cs_low();
    result = spi_send_header(LAN9252_SPI_FAST_READ_CMD, address);
    if (result == RT_EOK)
    {
        result = spi_transfer_byte(LAN9252_SPI_FAST_DUMMY, RT_NULL);
    }

    for (i = 0U; (result == RT_EOK) && (i < length); i++)
    {
        result = spi_transfer_byte(0U, &buffer[i]);
    }

    {
        rt_err_t finish_result = spi_finish();
        if (result == RT_EOK) result = finish_result;
    }

    return result;
}

rt_err_t lan9252_reg_write(rt_uint16_t address,
                           const rt_uint8_t *buffer,
                           rt_size_t length)
{
    rt_err_t result;
    rt_size_t i;

    if (!g_lan9252_initialized || (buffer == RT_NULL) ||
        (length == 0U) || (length > 4U))
    {
        return -RT_EINVAL;
    }

    lan9252_cs_low();
    result = spi_send_header(LAN9252_SPI_WRITE_CMD, address);

    for (i = 0U; (result == RT_EOK) && (i < length); i++)
    {
        result = spi_transfer_byte(buffer[i], RT_NULL);
    }

    {
        rt_err_t finish_result = spi_finish();
        if (result == RT_EOK) result = finish_result;
    }

    return result;
}

rt_uint32_t lan9252_reg_read32(rt_uint16_t address)
{
    rt_uint8_t data[4];

    if (lan9252_reg_read(address, data, sizeof(data)) != RT_EOK)
    {
        return 0xFFFFFFFFUL;
    }

    return bytes_to_u32(data, sizeof(data));
}

rt_err_t lan9252_reg_write32(rt_uint16_t address, rt_uint32_t value)
{
    rt_uint8_t data[4];

    u32_to_bytes(value, data, sizeof(data));
    return lan9252_reg_write(address, data, sizeof(data));
}

rt_err_t lan9252_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (g_lan9252_initialized)
    {
        return RT_EOK;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();

    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = LAN9252_CS_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(LAN9252_CS_GPIO_PORT, &gpio);
    lan9252_cs_high();

    SPI1->CR1 = 0U;
    SPI1->CR2 = 0U;
    SPI1->I2SCFGR &= ~SPI_I2SCFGR_I2SMOD;
    SPI1->CR1 = SPI_CR1_MSTR |
                SPI_CR1_SSM |
                SPI_CR1_SSI |
                LAN9252_SPI_BAUD_BITS;
    SPI1->CR1 |= SPI_CR1_SPE;

    g_lan9252_initialized = RT_TRUE;
    rt_thread_mdelay(100);

    if (lan9252_reg_read32(LAN9252_REG_BYTE_TEST) != LAN9252_BYTE_TEST_VALUE)
    {
        g_lan9252_initialized = RT_FALSE;
        SPI1->CR1 &= ~SPI_CR1_SPE;
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_bool_t lan9252_is_ready(void)
{
    rt_uint32_t byte_test;
    rt_uint32_t id_rev;
    rt_uint32_t hw_cfg;

    if (!g_lan9252_initialized)
    {
        return RT_FALSE;
    }

    byte_test = lan9252_reg_read32(LAN9252_REG_BYTE_TEST);
    id_rev = lan9252_reg_read32(LAN9252_REG_ID_REV);
    hw_cfg = lan9252_reg_read32(LAN9252_REG_HW_CFG);

    return (byte_test == LAN9252_BYTE_TEST_VALUE) &&
           ((id_rev & 0xFFFF0000UL) == 0x92520000UL) &&
           ((hw_cfg & LAN9252_HW_CFG_READY) != 0U);
}

static rt_err_t command_wait(rt_uint16_t command_reg,
                             rt_uint32_t mask,
                             rt_bool_t set)
{
    rt_uint32_t retry;

    for (retry = 0U; retry < LAN9252_COMMAND_RETRY; retry++)
    {
        rt_uint32_t command = lan9252_reg_read32(command_reg);
        rt_bool_t state;

        if (command == 0xFFFFFFFFUL)
        {
            return -RT_ERROR;
        }

        state = ((command & mask) != 0U) ? RT_TRUE : RT_FALSE;
        if (state == set)
        {
            return RT_EOK;
        }
    }

    g_lan9252_command_timeout_count++;
    return -RT_ETIMEOUT;
}

static rt_err_t csr_read_chunk(rt_uint16_t esc_address,
                               rt_uint8_t *buffer,
                               rt_uint8_t length)
{
    rt_uint32_t command;
    rt_uint32_t data;
    rt_err_t result;

    if ((buffer == RT_NULL) || (length == 0U) || (length > 4U))
    {
        return -RT_EINVAL;
    }

    command = LAN9252_ESC_CSR_BUSY |
              LAN9252_ESC_CSR_READ |
              ((rt_uint32_t)length << 16U) |
              esc_address;

    result = lan9252_reg_write32(LAN9252_ESC_CSR_CMD_REG, command);
    if (result != RT_EOK) return result;

    result = command_wait(LAN9252_ESC_CSR_CMD_REG,
                          LAN9252_ESC_CSR_BUSY,
                          RT_FALSE);
    if (result != RT_EOK) return result;

    data = lan9252_reg_read32(LAN9252_ESC_CSR_DATA_REG);
    if (data == 0xFFFFFFFFUL) return -RT_ERROR;

    u32_to_bytes(data, buffer, length);
    return RT_EOK;
}

static rt_err_t csr_write_chunk(rt_uint16_t esc_address,
                                const rt_uint8_t *buffer,
                                rt_uint8_t length)
{
    rt_uint32_t command;
    rt_err_t result;

    if ((buffer == RT_NULL) || (length == 0U) || (length > 4U))
    {
        return -RT_EINVAL;
    }

    result = lan9252_reg_write32(LAN9252_ESC_CSR_DATA_REG,
                                 bytes_to_u32(buffer, length));
    if (result != RT_EOK) return result;

    command = LAN9252_ESC_CSR_BUSY |
              ((rt_uint32_t)length << 16U) |
              esc_address;

    result = lan9252_reg_write32(LAN9252_ESC_CSR_CMD_REG, command);
    if (result != RT_EOK) return result;

    return command_wait(LAN9252_ESC_CSR_CMD_REG,
                        LAN9252_ESC_CSR_BUSY,
                        RT_FALSE);
}

rt_err_t lan9252_csr_read(rt_uint16_t esc_address,
                          rt_uint8_t *buffer,
                          rt_size_t length)
{
    rt_err_t result;

    if ((buffer == RT_NULL) || (length == 0U))
    {
        return -RT_EINVAL;
    }

    while (length > 0U)
    {
        rt_size_t chunk = (length > 4U) ? 4U : length;

        result = csr_read_chunk(esc_address, buffer, (rt_uint8_t)chunk);
        if (result != RT_EOK) return result;

        esc_address = (rt_uint16_t)(esc_address + chunk);
        buffer += chunk;
        length -= chunk;
    }

    return RT_EOK;
}

rt_err_t lan9252_csr_write(rt_uint16_t esc_address,
                           const rt_uint8_t *buffer,
                           rt_size_t length)
{
    rt_err_t result;

    if ((buffer == RT_NULL) || (length == 0U))
    {
        return -RT_EINVAL;
    }

    while (length > 0U)
    {
        rt_size_t chunk = (length > 4U) ? 4U : length;

        result = csr_write_chunk(esc_address, buffer, (rt_uint8_t)chunk);
        if (result != RT_EOK) return result;

        esc_address = (rt_uint16_t)(esc_address + chunk);
        buffer += chunk;
        length -= chunk;
    }

    return RT_EOK;
}

static rt_err_t pram_begin(rt_uint16_t command_reg,
                           rt_uint16_t address_length_reg,
                           rt_uint16_t esc_address,
                           rt_size_t length)
{
    rt_uint32_t address_length;
    rt_err_t result;

    result = lan9252_reg_write32(command_reg, LAN9252_PRAM_ABORT);
    if (result != RT_EOK) return result;

    result = command_wait(command_reg, LAN9252_PRAM_BUSY, RT_FALSE);
    if (result != RT_EOK) return result;

    address_length = ((rt_uint32_t)length << 16U) | esc_address;
    result = lan9252_reg_write32(address_length_reg, address_length);
    if (result != RT_EOK) return result;

    result = lan9252_reg_write32(command_reg, LAN9252_PRAM_BUSY);
    if (result != RT_EOK) return result;

    return command_wait(command_reg, LAN9252_PRAM_AVAIL, RT_TRUE);
}

rt_err_t lan9252_pram_read(rt_uint16_t esc_address,
                           rt_uint8_t *buffer,
                           rt_size_t length)
{
    rt_uint8_t first_word[4];
    rt_uint8_t offset;
    rt_size_t first_length;
    rt_err_t result;

    if ((buffer == RT_NULL) || (length == 0U) ||
        (esc_address < LAN9252_ESC_PRAM_START) ||
        (((rt_uint32_t)esc_address + length - 1U) > LAN9252_ESC_PRAM_END) ||
        (length > 0xFFFFU))
    {
        return -RT_EINVAL;
    }

    result = pram_begin(LAN9252_PRAM_RD_CMD_REG,
                        LAN9252_PRAM_RD_ADDR_LEN_REG,
                        esc_address,
                        length);
    if (result != RT_EOK) return result;

    result = lan9252_reg_read(LAN9252_PRAM_RD_FIFO_REG,
                              first_word,
                              sizeof(first_word));
    if (result != RT_EOK) return result;

    offset = (rt_uint8_t)(esc_address & 0x0003U);
    first_length = 4U - offset;
    if (first_length > length) first_length = length;

    rt_memcpy(buffer, &first_word[offset], first_length);
    buffer += first_length;
    length -= first_length;

    if (length > 0U)
    {
        result = fifo_read_burst(LAN9252_PRAM_RD_FIFO_REG, buffer, length);
        if (result != RT_EOK) return result;
    }

    /* The requested bytes have already left the FIFO. Matching the vendor
     * path, do not add another command-register SPI poll to the cyclic read.
     * The next PRAM command begins with ABORT + busy-clear synchronization. */
    return RT_EOK;
}

rt_err_t lan9252_pram_write(rt_uint16_t esc_address,
                            const rt_uint8_t *buffer,
                            rt_size_t length)
{
    rt_uint8_t first_word[4] = {0U, 0U, 0U, 0U};
    rt_uint8_t offset;
    rt_size_t first_length;
    rt_err_t result;

    if ((buffer == RT_NULL) || (length == 0U) ||
        (esc_address < LAN9252_ESC_PRAM_START) ||
        (((rt_uint32_t)esc_address + length - 1U) > LAN9252_ESC_PRAM_END) ||
        (length > 0xFFFFU))
    {
        return -RT_EINVAL;
    }

    result = pram_begin(LAN9252_PRAM_WR_CMD_REG,
                        LAN9252_PRAM_WR_ADDR_LEN_REG,
                        esc_address,
                        length);
    if (result != RT_EOK) return result;

    offset = (rt_uint8_t)(esc_address & 0x0003U);
    first_length = 4U - offset;
    if (first_length > length) first_length = length;

    rt_memcpy(&first_word[offset], buffer, first_length);
    result = lan9252_reg_write(LAN9252_PRAM_WR_FIFO_REG,
                               first_word,
                               sizeof(first_word));
    if (result != RT_EOK) return result;

    buffer += first_length;
    length -= first_length;

    if (length > 0U)
    {
        result = fifo_write_burst(LAN9252_PRAM_WR_FIFO_REG, buffer, length);
        if (result != RT_EOK) return result;
    }

    /* Do not wait for the final busy-clear here. The FIFO payload has been
     * accepted and the next PRAM command synchronizes with ABORT first. */
    return RT_EOK;
}

rt_err_t lan9252_esc_read(rt_uint16_t esc_address,
                          rt_uint8_t *buffer,
                          rt_size_t length)
{
    if ((buffer == RT_NULL) || (length == 0U))
    {
        return -RT_EINVAL;
    }

    if ((esc_address >= LAN9252_ESC_PRAM_START) &&
        (((rt_uint32_t)esc_address + length - 1U) <= LAN9252_ESC_PRAM_END))
    {
        return lan9252_pram_read(esc_address, buffer, length);
    }

    return lan9252_csr_read(esc_address, buffer, length);
}

rt_err_t lan9252_esc_write(rt_uint16_t esc_address,
                           const rt_uint8_t *buffer,
                           rt_size_t length)
{
    if ((buffer == RT_NULL) || (length == 0U))
    {
        return -RT_EINVAL;
    }

    if ((esc_address >= LAN9252_ESC_PRAM_START) &&
        (((rt_uint32_t)esc_address + length - 1U) <= LAN9252_ESC_PRAM_END))
    {
        return lan9252_pram_write(esc_address, buffer, length);
    }

    return lan9252_csr_write(esc_address, buffer, length);
}
