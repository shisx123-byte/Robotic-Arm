#include <board.h>

#if defined(BSP_USING_CAN1) && \
    (defined(DM_CAN1_USE_PA11_PA12) || defined(DM_CAN1_USE_PB8_PB9))

/*
 * Select exactly one verified pin pair in rtconfig.h:
 *   DM_CAN1_USE_PA11_PA12  -> RX PA11, TX PA12
 *   DM_CAN1_USE_PB8_PB9    -> RX PB8,  TX PB9
 *
 * A transceiver EN/STB GPIO, if present on the PCB, must also be configured
 * before enabling the motor.
 */
void HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
    GPIO_InitTypeDef gpio = {0};

    if (hcan->Instance != CAN1)
    {
        return;
    }

    __HAL_RCC_CAN1_CLK_ENABLE();

#if defined(DM_CAN1_USE_PA11_PA12)
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOA, &gpio);
#elif defined(DM_CAN1_USE_PB8_PB9)
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &gpio);
#endif
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance != CAN1)
    {
        return;
    }

    __HAL_RCC_CAN1_CLK_DISABLE();
#if defined(DM_CAN1_USE_PA11_PA12)
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
#elif defined(DM_CAN1_USE_PB8_PB9)
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8 | GPIO_PIN_9);
#endif
}
#endif

