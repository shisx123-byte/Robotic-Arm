#include "dm_gateway.h"

#include <board.h>
#include <rtthread.h>
#include <rthw.h>
#include <string.h>

#if defined(RT_USING_CAN) && defined(BSP_USING_CAN1)
#include <rtdevice.h>
#include <drivers/can.h>
#define DM_CAN_COMPILED 1
#else
#define DM_CAN_COMPILED 0
#endif

#define DM_CAN_DEVICE_NAME       "can1"
#define DM_MOTOR_ID              0x001U
#define DM_FEEDBACK_ID           0x011U
#define DM_COMMAND_TIMEOUT_MS    20U
#define DM_FEEDBACK_TIMEOUT_MS   50U
#define DM_FEEDBACK_POLL_MS      10U
#define DM_WORKER_STACK_SIZE     1536U
#define DM_WORKER_PRIORITY       4U
#define DM_WORKER_TICK           1U

/* Damiao 4340 / 4340P MIT scaling. Change here if the test motor is another model. */
#define DM_P_MAX  12.5f
#define DM_V_MAX   8.0f
#define DM_T_MAX  28.0f
#define DM_KP_MAX 500.0f
#define DM_KD_MAX   5.0f

typedef struct
{
    uint16_t control_word;
    uint16_t mode_flags;
    uint32_t sequence;
    float position;
    float velocity;
    float kp;
    float kd;
    float torque;
} dm_command_t;

typedef struct
{
    uint16_t status_word;
    uint16_t motor_flags;
    uint32_t sequence;
    uint32_t timestamp_us;
    float position;
    float velocity;
    float torque;
    uint16_t temperatures;
    uint16_t can_error;
    uint32_t rx_count;
} dm_feedback_t;

static volatile dm_command_t g_command;
static volatile dm_feedback_t g_feedback;
static volatile rt_tick_t g_command_tick;
static volatile rt_tick_t g_feedback_tick;
static volatile uint8_t g_outputs_active;
static volatile uint8_t g_command_pending;
static volatile uint8_t g_initialized;
static uint8_t g_motor_enabled;

#if DM_CAN_COMPILED
static rt_device_t g_can;
static struct rt_semaphore g_worker_sem;

static float dm_clampf(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

static uint8_t dm_float_valid(float value)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    return (uint8_t)(((raw >> 23U) & 0xFFU) != 0xFFU);
}

static uint32_t dm_float_to_uint(float value, float minimum, float maximum, uint8_t bits)
{
    const float span = maximum - minimum;
    const uint32_t full_scale = (1UL << bits) - 1UL;
    value = dm_clampf(value, minimum, maximum);
    return (uint32_t)(((value - minimum) * (float)full_scale) / span);
}

static float dm_uint_to_float(uint32_t value, float minimum, float maximum, uint8_t bits)
{
    const uint32_t full_scale = (1UL << bits) - 1UL;
    return ((float)value * (maximum - minimum) / (float)full_scale) + minimum;
}

static rt_err_t dm_can_rx_indicate(rt_device_t dev, rt_size_t size)
{
    (void)dev;
    (void)size;
    return rt_sem_release(&g_worker_sem);
}

static rt_err_t dm_can_send(uint16_t id, const uint8_t data[8])
{
    struct rt_can_msg msg;

    memset(&msg, 0, sizeof(msg));
    msg.id = id;
    msg.ide = RT_CAN_STDID;
    msg.rtr = RT_CAN_DTR;
    msg.len = 8;
    memcpy(msg.data, data, 8);

    if (rt_device_write(g_can, 0, &msg, sizeof(msg)) != sizeof(msg))
    {
        g_feedback.status_word |= DM_STATUS_CAN_TX_ERROR;
        return -RT_ERROR;
    }

    g_feedback.status_word &= (uint16_t)~DM_STATUS_CAN_TX_ERROR;
    return RT_EOK;
}

static void dm_send_special(uint8_t command)
{
    uint8_t data[8] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, command};
    (void)dm_can_send(DM_MOTOR_ID, data);
}

static void dm_request_feedback(void)
{
    uint8_t data[8] =
    {
        (uint8_t)DM_MOTOR_ID, (uint8_t)(DM_MOTOR_ID >> 8U),
        0xCCU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    (void)dm_can_send(0x7FFU, data);
}

static uint8_t dm_command_is_valid(const dm_command_t *command)
{
    return (uint8_t)(dm_float_valid(command->position) &&
                     dm_float_valid(command->velocity) &&
                     dm_float_valid(command->kp) &&
                     dm_float_valid(command->kd) &&
                     dm_float_valid(command->torque) &&
                     command->kp >= 0.0f && command->kp <= DM_KP_MAX &&
                     command->kd >= 0.0f && command->kd <= DM_KD_MAX);
}

static void dm_send_mit(const dm_command_t *command)
{
    uint8_t data[8];
    uint32_t p;
    uint32_t v;
    uint32_t kp;
    uint32_t kd;
    uint32_t t;

    p = dm_float_to_uint(command->position, -DM_P_MAX, DM_P_MAX, 16);
    v = dm_float_to_uint(command->velocity, -DM_V_MAX, DM_V_MAX, 12);
    kp = dm_float_to_uint(command->kp, 0.0f, DM_KP_MAX, 12);
    kd = dm_float_to_uint(command->kd, 0.0f, DM_KD_MAX, 12);
    t = dm_float_to_uint(command->torque, -DM_T_MAX, DM_T_MAX, 12);

    data[0] = (uint8_t)(p >> 8U);
    data[1] = (uint8_t)p;
    data[2] = (uint8_t)(v >> 4U);
    data[3] = (uint8_t)(((v & 0x0FU) << 4U) | (kp >> 8U));
    data[4] = (uint8_t)kp;
    data[5] = (uint8_t)(kd >> 4U);
    data[6] = (uint8_t)(((kd & 0x0FU) << 4U) | (t >> 8U));
    data[7] = (uint8_t)t;
    (void)dm_can_send(DM_MOTOR_ID, data);
}

static void dm_parse_feedback(const struct rt_can_msg *msg)
{
    uint32_t level;
    rt_base_t irq_level;
    dm_feedback_t feedback;

    if (msg->ide != RT_CAN_STDID || msg->rtr != RT_CAN_DTR || msg->len < 8U)
    {
        return;
    }
    if (msg->id != DM_FEEDBACK_ID || (msg->data[0] & 0x0FU) != DM_MOTOR_ID)
    {
        return;
    }

    irq_level = rt_hw_interrupt_disable();
    feedback = g_feedback;
    rt_hw_interrupt_enable(irq_level);

    level = ((uint32_t)msg->data[1] << 8U) | msg->data[2];
    feedback.position = dm_uint_to_float(level, -DM_P_MAX, DM_P_MAX, 16);
    level = ((uint32_t)msg->data[3] << 4U) | (msg->data[4] >> 4U);
    feedback.velocity = dm_uint_to_float(level, -DM_V_MAX, DM_V_MAX, 12);
    level = (((uint32_t)msg->data[4] & 0x0FU) << 8U) | msg->data[5];
    feedback.torque = dm_uint_to_float(level, -DM_T_MAX, DM_T_MAX, 12);
    feedback.motor_flags = (uint16_t)(msg->data[0] >> 4U);
    feedback.temperatures = (uint16_t)msg->data[6] | ((uint16_t)msg->data[7] << 8U);
    feedback.timestamp_us = (uint32_t)rt_tick_get() * (1000000U / RT_TICK_PER_SECOND);
    feedback.rx_count++;
    feedback.status_word |= DM_STATUS_FEEDBACK_VALID;
    feedback.status_word &= (uint16_t)~DM_STATUS_FEEDBACK_TIMEOUT;

    irq_level = rt_hw_interrupt_disable();
    g_feedback = feedback;
    g_feedback_tick = rt_tick_get();
    rt_hw_interrupt_enable(irq_level);
}

static void dm_drain_rx(void)
{
    struct rt_can_msg msg;
    while (rt_device_read(g_can, 0, &msg, sizeof(msg)) == sizeof(msg))
    {
        dm_parse_feedback(&msg);
    }
}

static void dm_disable_if_needed(void)
{
    if (g_motor_enabled)
    {
        dm_send_special(0xFDU);
        g_motor_enabled = 0U;
        g_feedback.status_word &= (uint16_t)~DM_STATUS_MOTOR_ENABLED;
    }
}

static void dm_worker_entry(void *parameter)
{
    rt_tick_t last_feedback_poll = 0U;
    (void)parameter;

    while (1)
    {
        dm_command_t command;
        rt_tick_t now;
        rt_tick_t command_tick;
        rt_tick_t feedback_tick;
        uint8_t outputs_active;
        uint8_t command_pending;
        rt_base_t irq_level;

        (void)rt_sem_take(&g_worker_sem, DM_WORKER_TICK);
        dm_drain_rx();

        irq_level = rt_hw_interrupt_disable();
        command = g_command;
        command_tick = g_command_tick;
        feedback_tick = g_feedback_tick;
        outputs_active = g_outputs_active;
        command_pending = g_command_pending;
        g_command_pending = 0U;
        rt_hw_interrupt_enable(irq_level);

        now = rt_tick_get();
        if ((now - feedback_tick) > rt_tick_from_millisecond(DM_FEEDBACK_TIMEOUT_MS))
        {
            g_feedback.status_word |= DM_STATUS_FEEDBACK_TIMEOUT;
            g_feedback.status_word &= (uint16_t)~DM_STATUS_FEEDBACK_VALID;
        }

        if (!outputs_active ||
            (now - command_tick) > rt_tick_from_millisecond(DM_COMMAND_TIMEOUT_MS) ||
            (command.control_word & DM_CONTROL_QUICK_STOP) != 0U ||
            (command.control_word & DM_CONTROL_ENABLE) == 0U)
        {
            if ((now - command_tick) > rt_tick_from_millisecond(DM_COMMAND_TIMEOUT_MS))
            {
                g_feedback.status_word |= DM_STATUS_COMMAND_TIMEOUT;
            }
            dm_disable_if_needed();
            if ((now - last_feedback_poll) >= rt_tick_from_millisecond(DM_FEEDBACK_POLL_MS))
            {
                dm_request_feedback();
                last_feedback_poll = now;
            }
            continue;
        }

        g_feedback.status_word &= (uint16_t)~DM_STATUS_COMMAND_TIMEOUT;
        if (!dm_command_is_valid(&command))
        {
            g_feedback.status_word |= DM_STATUS_BAD_COMMAND;
            dm_disable_if_needed();
            continue;
        }

        g_feedback.status_word &= (uint16_t)~DM_STATUS_BAD_COMMAND;
        if (!g_motor_enabled)
        {
            dm_send_special(0xFCU);
            g_motor_enabled = 1U;
            g_feedback.status_word |= DM_STATUS_MOTOR_ENABLED;
        }
        if (command_pending)
        {
            dm_send_mit(&command);
            g_feedback.sequence = command.sequence;
        }
    }
}
#endif

static uint32_t dm_words_to_u32(const uint16_t *words)
{
    return (uint32_t)words[0] | ((uint32_t)words[1] << 16U);
}

static void dm_u32_to_words(uint32_t value, uint16_t *words)
{
    words[0] = (uint16_t)value;
    words[1] = (uint16_t)(value >> 16U);
}

static float dm_words_to_float(const uint16_t *words)
{
    uint32_t raw = dm_words_to_u32(words);
    float value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static void dm_float_to_words(float value, uint16_t *words)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    dm_u32_to_words(raw, words);
}

void dm_gateway_init(void)
{
    rt_base_t irq_level;

    if (g_initialized)
    {
        return;
    }

    irq_level = rt_hw_interrupt_disable();
    memset((void *)&g_command, 0, sizeof(g_command));
    memset((void *)&g_feedback, 0, sizeof(g_feedback));
    g_feedback.status_word = DM_STATUS_BOARD_UNCONFIGURED;
    g_command_tick = rt_tick_get();
    g_feedback_tick = rt_tick_get();
    g_outputs_active = 0U;
    g_command_pending = 0U;
    g_motor_enabled = 0U;
    rt_hw_interrupt_enable(irq_level);

#if DM_CAN_COMPILED
    g_can = rt_device_find(DM_CAN_DEVICE_NAME);
    if (g_can != RT_NULL)
    {
        rt_thread_t worker;

        (void)rt_sem_init(&g_worker_sem, "dmcan", 0, RT_IPC_FLAG_FIFO);
        if (rt_device_control(g_can, RT_CAN_CMD_SET_BAUD, (void *)CAN500kBaud) == RT_EOK &&
            rt_device_open(g_can, RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_INT_TX) == RT_EOK)
        {
            (void)rt_device_set_rx_indicate(g_can, dm_can_rx_indicate);
            g_feedback.status_word = DM_STATUS_CAN_READY;
            worker = rt_thread_create("dm_can", dm_worker_entry, RT_NULL,
                                      DM_WORKER_STACK_SIZE, DM_WORKER_PRIORITY, 1);
            if (worker != RT_NULL)
            {
                rt_thread_startup(worker);
            }
        }
    }
#endif
    g_initialized = 1U;
}

void dm_gateway_outputs_start(void)
{
    g_outputs_active = 1U;
    g_feedback.status_word |= DM_STATUS_ECAT_OUTPUTS;
}

void dm_gateway_outputs_stop(void)
{
    g_outputs_active = 0U;
    g_feedback.status_word &= (uint16_t)~DM_STATUS_ECAT_OUTPUTS;
#if DM_CAN_COMPILED
    if (g_can != RT_NULL)
    {
        (void)rt_sem_release(&g_worker_sem);
    }
#endif
}

void dm_gateway_on_rx_pdo(const uint16_t words[DM_RXPDO_WORDS])
{
    dm_command_t command;
    rt_base_t irq_level;

    command.control_word = words[0];
    command.mode_flags = words[1];
    command.sequence = dm_words_to_u32(&words[2]);
    command.position = dm_words_to_float(&words[4]);
    command.velocity = dm_words_to_float(&words[6]);
    command.kp = dm_words_to_float(&words[8]);
    command.kd = dm_words_to_float(&words[10]);
    command.torque = dm_words_to_float(&words[12]);

    irq_level = rt_hw_interrupt_disable();
    g_command = command;
    g_command_tick = rt_tick_get();
    g_command_pending = 1U;
    rt_hw_interrupt_enable(irq_level);
}

void dm_gateway_fill_tx_pdo(uint16_t words[DM_TXPDO_WORDS])
{
    dm_feedback_t feedback;
    rt_base_t irq_level;

    irq_level = rt_hw_interrupt_disable();
    feedback = g_feedback;
    rt_hw_interrupt_enable(irq_level);

    words[0] = feedback.status_word;
    words[1] = feedback.motor_flags;
    dm_u32_to_words(feedback.sequence, &words[2]);
    dm_u32_to_words(feedback.timestamp_us, &words[4]);
    dm_float_to_words(feedback.position, &words[6]);
    dm_float_to_words(feedback.velocity, &words[8]);
    dm_float_to_words(feedback.torque, &words[10]);
    words[12] = feedback.temperatures;
    words[13] = feedback.can_error;
    dm_u32_to_words(feedback.rx_count, &words[14]);
}

void dm_gateway_cycle(void)
{
    if (!g_initialized)
    {
        dm_gateway_init();
    }
#if DM_CAN_COMPILED
    if (g_can != RT_NULL)
    {
        (void)rt_sem_release(&g_worker_sem);
    }
#endif
}
