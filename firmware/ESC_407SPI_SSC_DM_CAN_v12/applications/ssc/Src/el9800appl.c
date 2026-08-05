#include "ecat_def.h"

#if EL9800_APPLICATION

#include "applInterface.h"
#include "../../motor/dm_gateway.h"

#define _EVALBOARD_
#include "el9800appl.h"
#undef _EVALBOARD_

void APPL_AckErrorInd(UINT16 stateTrans)
{
    (void)stateTrans;
}

UINT16 APPL_StartMailboxHandler(void)
{
    dm_gateway_init();
    return ALSTATUSCODE_NOERROR;
}

UINT16 APPL_StopMailboxHandler(void)
{
    dm_gateway_outputs_stop();
    return ALSTATUSCODE_NOERROR;
}

UINT16 APPL_StartInputHandler(UINT16 *pIntMask)
{
    (void)pIntMask;
    return ALSTATUSCODE_NOERROR;
}

UINT16 APPL_StopInputHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

UINT16 APPL_StartOutputHandler(void)
{
    dm_gateway_outputs_start();
    return ALSTATUSCODE_NOERROR;
}

UINT16 APPL_StopOutputHandler(void)
{
    dm_gateway_outputs_stop();
    return ALSTATUSCODE_NOERROR;
}

UINT16 APPL_GenerateMapping(UINT16 *pInputSize, UINT16 *pOutputSize)
{
    *pInputSize = 32U;
    *pOutputSize = 28U;
    return ALSTATUSCODE_NOERROR;
}

void APPL_InputMapping(UINT16 *pData)
{
    UINT16 words[DM_TXPDO_WORDS];
    UINT16 index;

    dm_gateway_fill_tx_pdo(words);
    for (index = 0U; index < DM_TXPDO_WORDS; index++)
    {
        pData[index] = words[index];
        sDMFeedback.words[index] = words[index];
    }
}

void APPL_OutputMapping(UINT16 *pData)
{
    UINT16 words[DM_RXPDO_WORDS];
    UINT16 index;

    for (index = 0U; index < DM_RXPDO_WORDS; index++)
    {
        words[index] = pData[index];
        sDMCommand.words[index] = words[index];
    }
    dm_gateway_on_rx_pdo(words);
}

void APPL_Application(void)
{
    dm_gateway_cycle();
}

#if EXPLICIT_DEVICE_ID
UINT16 APPL_GetDeviceID(void)
{
    return 1U;
}
#endif

#endif
