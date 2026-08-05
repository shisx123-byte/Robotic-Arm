/**
 * RT-Thread hardware adaptation for SSC 5.11 + LAN9252 SPI PDI.
 *
 * The file name is retained because the generated SSC project selects
 * EL9800_HW. The implementation is not tied to the EL9800 board.
 */
#ifndef _EL9800HW_H_
#define _EL9800HW_H_

#include <rtthread.h>
#include "esc.h"
#include "ssc_io.h"
#include "ssc_irq.h"
#include "ssc_dc_sync.h"

/* One RT-Thread tick is 1 ms in this project. */
#define ECAT_TIMER_INC_P_MS       1U

UINT32 ssc_hw_get_timer(void);
void   ssc_hw_clear_timer(void);
UINT32 ssc_hw_get_error_count(void);

#define HW_GetTimer()             ssc_hw_get_timer()
#define HW_ClearTimer()           ssc_hw_clear_timer()

/* The GPIO interrupt is deferred: the real ISR only releases a semaphore and
 * all SSC/SPI work runs in the single SSC thread. Therefore there is no stack
 * concurrency to mask here. Keeping these macros as no-ops also avoids losing
 * an active-low, edge-triggered IRQ while the STM32 EXTI line is reconfigured. */
#ifndef DISABLE_ESC_INT
#define DISABLE_ESC_INT()         do { } while (0)
#endif
#ifndef ENABLE_ESC_INT
#define ENABLE_ESC_INT()          do { } while (0)
#endif

/* Physical GPIO process data interface. Key values are normalized so that
 * pressed=1 regardless of the electrical active level. */
extern UINT16 uhADCxConvertedValue;

#define SWITCH_1                  ssc_io_read_key(0U)
#define SWITCH_2                  ssc_io_read_key(1U)
#define SWITCH_3                  ssc_io_read_key(2U)
#define SWITCH_4                  ssc_io_read_key(3U)
#define SWITCH_5                  ssc_io_read_key(4U)
#define SWITCH_6                  ssc_io_read_key(5U)
#define SWITCH_7                  ssc_io_read_key(6U)
#define SWITCH_8                  ssc_io_read_key(7U)

#define HW_EscReadByte(ByteValue, Address)       HW_EscRead((MEM_ADDR *)&(ByteValue), (UINT16)(Address), 1U)
#define HW_EscReadWord(WordValue, Address)       HW_EscRead((MEM_ADDR *)&(WordValue), (UINT16)(Address), 2U)
#define HW_EscReadDWord(DWordValue, Address)     HW_EscRead((MEM_ADDR *)&(DWordValue), (UINT16)(Address), 4U)
#define HW_EscReadMbxMem(pData, Address, Len)    HW_EscRead((MEM_ADDR *)(pData), (UINT16)(Address), (UINT16)(Len))

#define HW_EscReadByteIsr(ByteValue, Address)    HW_EscReadIsr((MEM_ADDR *)&(ByteValue), (UINT16)(Address), 1U)
#define HW_EscReadWordIsr(WordValue, Address)    HW_EscReadIsr((MEM_ADDR *)&(WordValue), (UINT16)(Address), 2U)
#define HW_EscReadDWordIsr(DWordValue, Address)  HW_EscReadIsr((MEM_ADDR *)&(DWordValue), (UINT16)(Address), 4U)

#define HW_EscWriteByte(ByteValue, Address)      HW_EscWrite((MEM_ADDR *)&(ByteValue), (UINT16)(Address), 1U)
#define HW_EscWriteWord(WordValue, Address)      HW_EscWrite((MEM_ADDR *)&(WordValue), (UINT16)(Address), 2U)
#define HW_EscWriteDWord(DWordValue, Address)    HW_EscWrite((MEM_ADDR *)&(DWordValue), (UINT16)(Address), 4U)
#define HW_EscWriteMbxMem(pData, Address, Len)   HW_EscWrite((MEM_ADDR *)(pData), (UINT16)(Address), (UINT16)(Len))

#define HW_EscWriteByteIsr(ByteValue, Address)   HW_EscWriteIsr((MEM_ADDR *)&(ByteValue), (UINT16)(Address), 1U)
#define HW_EscWriteWordIsr(WordValue, Address)   HW_EscWriteIsr((MEM_ADDR *)&(WordValue), (UINT16)(Address), 2U)
#define HW_EscWriteDWordIsr(DWordValue, Address) HW_EscWriteIsr((MEM_ADDR *)&(DWordValue), (UINT16)(Address), 4U)

#if _EL9800HW_
#define PROTO
#else
#define PROTO extern
#endif

PROTO UINT8  HW_Init(void);
PROTO void   HW_Release(void);
PROTO UINT16 HW_GetALEventRegister(void);
PROTO UINT16 HW_GetALEventRegister_Isr(void);
PROTO void   HW_SetLed(UINT8 RunLed, UINT8 ErrLed);
PROTO void   HW_EscRead(MEM_ADDR *pData, UINT16 Address, UINT16 Len);
PROTO void   HW_EscReadIsr(MEM_ADDR *pData, UINT16 Address, UINT16 Len);
PROTO void   HW_EscWrite(MEM_ADDR *pData, UINT16 Address, UINT16 Len);
PROTO void   HW_EscWriteIsr(MEM_ADDR *pData, UINT16 Address, UINT16 Len);

#undef PROTO
#endif
