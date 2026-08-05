#ifndef DM_GATEWAY_H
#define DM_GATEWAY_H

#include <stdint.h>

#define DM_RXPDO_WORDS 14U
#define DM_TXPDO_WORDS 16U

/* RxPDO control_word */
#define DM_CONTROL_ENABLE       0x0001U
#define DM_CONTROL_QUICK_STOP   0x0002U
#define DM_CONTROL_CLEAR_ERROR  0x0004U

/* TxPDO status_word */
#define DM_STATUS_CAN_READY         0x0001U
#define DM_STATUS_ECAT_OUTPUTS      0x0002U
#define DM_STATUS_MOTOR_ENABLED     0x0004U
#define DM_STATUS_FEEDBACK_VALID    0x0008U
#define DM_STATUS_COMMAND_TIMEOUT   0x0010U
#define DM_STATUS_FEEDBACK_TIMEOUT  0x0020U
#define DM_STATUS_CAN_TX_ERROR      0x0040U
#define DM_STATUS_BAD_COMMAND       0x0080U
#define DM_STATUS_BOARD_UNCONFIGURED 0x8000U

void dm_gateway_init(void);
void dm_gateway_outputs_start(void);
void dm_gateway_outputs_stop(void);
void dm_gateway_on_rx_pdo(const uint16_t words[DM_RXPDO_WORDS]);
void dm_gateway_fill_tx_pdo(uint16_t words[DM_TXPDO_WORDS]);
void dm_gateway_cycle(void);

#endif
