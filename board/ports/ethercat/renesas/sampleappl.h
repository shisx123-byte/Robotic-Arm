/*
* This source file is part of the EtherCAT Slave Stack Code licensed by Beckhoff Automation GmbH & Co KG, 33415 Verl, Germany.
* The corresponding license agreement applies. This hint shall not be removed.
*/

/**
 * \addtogroup SampleAppl Sample Application
 * @{
 */

/**
\file sampleappl.h
\author EthercatSSC@beckhoff.com
\brief Sample application specific objects

\version 5.12

<br>Changes to version V5.11:<br>
V5.12 EOE4: handle 16bit only acceess, move ethernet protocol defines and structures to application header files<br>
<br>Changes to version V5.01:<br>
V5.11 COE1: update invalid end entry in the object dictionaries (error with some compilers)<br>
V5.11 ECAT4: enhance SM/Sync monitoring for input/output only slaves<br>
<br>Changes to version - :<br>
V5.01 : Start file change log
 */

/*-----------------------------------------------------------------------------------------
------
------    Includes
------
-----------------------------------------------------------------------------------------*/
#include "ecat_def.h"
#include "cia402appl.h"
#include "sampleios.h"

#ifndef _SAMPLE_APPL_H_
#define _SAMPLE_APPL_H_


/*-----------------------------------------------------------------------------------------
------
------    Defines and Types
------
-----------------------------------------------------------------------------------------*/
#if (CiA402_SAMPLE_APPLICATION != 1)

/* ========================================================================
 * PDO Mapping — EtherKit Simple I/O (individual BOOL per I/O point)
 *
 * RxPDO 0x1603: 3 entries    TxPDO 0x1A03: 1 entry
 *   [0] 0x7001:00 LED0_RED      [0] 0x6001:00 KEY2
 *   [1] 0x7002:00 LED1_BLUE
 *   [2] 0x7003:00 LED2_GREEN
 * ======================================================================== */

/* --- structure types --- */
typedef struct OBJ_STRUCT_PACKED_START {
   UINT16   u16SubIndex0;
   UINT32   aEntries[3];
} OBJ_STRUCT_PACKED_END TOBJ1603;

typedef struct OBJ_STRUCT_PACKED_START {
   UINT16   u16SubIndex0;
   UINT32   aEntries[1];
} OBJ_STRUCT_PACKED_END TOBJ1A03;

typedef struct OBJ_STRUCT_PACKED_START {
   UINT16   u16SubIndex0;
   UINT16   aEntries[1];
} OBJ_STRUCT_PACKED_END TOBJ1C12;

typedef struct OBJ_STRUCT_PACKED_START {
   UINT16   u16SubIndex0;
   UINT16   aEntries[1];
} OBJ_STRUCT_PACKED_END TOBJ1C13;

/* --- entry descriptions --- */
#ifdef _OBJD_
OBJCONST TSDOINFOENTRYDESC    OBJMEM asPDOAssignEntryDesc[] = {
   {DEFTYPE_UNSIGNED8,  0x08, (ACCESS_READ|ACCESS_WRITE_PREOP)},
   {DEFTYPE_UNSIGNED16, 0x10, (ACCESS_READ|ACCESS_WRITE_PREOP)}};

/* RxPDO 0x1603: 3 mapped entries */
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x1603[] = {
   {DEFTYPE_UNSIGNED8, 0x08, ACCESS_READ | ACCESS_WRITE_PREOP},
   {DEFTYPE_UNSIGNED32, 0x20, ACCESS_READ | ACCESS_WRITE_PREOP},
   {DEFTYPE_UNSIGNED32, 0x20, ACCESS_READ | ACCESS_WRITE_PREOP},
   {DEFTYPE_UNSIGNED32, 0x20, ACCESS_READ | ACCESS_WRITE_PREOP}};
OBJCONST UCHAR OBJMEM aName0x1603[] = "RxPDO-Map\000\377";

/* TxPDO 0x1A03: 1 mapped entry */
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x1A03[] = {
   {DEFTYPE_UNSIGNED8, 0x08, ACCESS_READ | ACCESS_WRITE_PREOP},
   {DEFTYPE_UNSIGNED32, 0x20, ACCESS_READ | ACCESS_WRITE_PREOP}};
OBJCONST UCHAR OBJMEM aName0x1A03[] = "TxPDO-Map\000\377";

/* SM assignment names */
OBJCONST UCHAR OBJMEM aName0x1C12[] = "RxPDO assign";
OBJCONST UCHAR OBJMEM aName0x1C13[] = "TxPDO assign";

/* Individual I/O entry descriptors (DEFTYPE_BOOLEAN, 1 bit each) */
OBJCONST TSDOINFOENTRYDESC    OBJMEM EntryDesc0x7001 = {DEFTYPE_BOOLEAN, 0x01, ACCESS_READWRITE | OBJACCESS_RXPDOMAPPING};
OBJCONST TSDOINFOENTRYDESC    OBJMEM EntryDesc0x7002 = {DEFTYPE_BOOLEAN, 0x01, ACCESS_READWRITE | OBJACCESS_RXPDOMAPPING};
OBJCONST TSDOINFOENTRYDESC    OBJMEM EntryDesc0x7003 = {DEFTYPE_BOOLEAN, 0x01, ACCESS_READWRITE | OBJACCESS_RXPDOMAPPING};
OBJCONST TSDOINFOENTRYDESC    OBJMEM EntryDesc0x6001 = {DEFTYPE_BOOLEAN, 0x01, ACCESS_READ | OBJACCESS_TXPDOMAPPING};

/* I/O object names */
OBJCONST UCHAR OBJMEM aName0x7001[] = "LED0_RED\000\377";
OBJCONST UCHAR OBJMEM aName0x7002[] = "LED1_BLUE\000\377";
OBJCONST UCHAR OBJMEM aName0x7003[] = "LED2_GREEN\000\377";
OBJCONST UCHAR OBJMEM aName0x6001[] = "KEY2\000\377";
#endif /* _OBJD_ */

/* --- PROTO macro for extern / non-extern declarations --- */
#ifdef _SAMPLE_APPLICATION_
    #define PROTO
#else
    #define PROTO extern
#endif

/* --- PDO mapping variables --- */
PROTO TOBJ1603 RxPDOMap
#ifdef _SAMPLE_APPLICATION_
 = {3, {0x70010001, 0x70020001, 0x70030001}}
#endif
;

PROTO TOBJ1A03 TxPDOMap
#ifdef _SAMPLE_APPLICATION_
 = {1, {0x60010001}}
#endif
;

/* SM2 -> 0x1603, SM3 -> 0x1A03 */
PROTO TOBJ1C12 sRxPDOassign
#ifdef _SAMPLE_APPLICATION_
= {0x01, {0x1603}}
#endif
;

PROTO TOBJ1C13 sTxPDOassign
#ifdef _SAMPLE_APPLICATION_
= {0x01, {0x1A03}}
#endif
;

/* --- I/O variables (stored as UINT8, 0=OFF/not-pressed, 1=ON/pressed) --- */
PROTO UINT8 LED0_RED
#ifdef _SAMPLE_APPLICATION_
= 0x00
#endif
;

PROTO UINT8 LED1_BLUE
#ifdef _SAMPLE_APPLICATION_
= 0x00
#endif
;

PROTO UINT8 LED2_GREEN
#ifdef _SAMPLE_APPLICATION_
= 0x00
#endif
;

PROTO UINT8 KEY2
#ifdef _SAMPLE_APPLICATION_
= 0x00
#endif
;

/* --- Object Dictionary --- */
#ifdef _OBJD_
TOBJECT    OBJMEM ApplicationObjDic[] = {
   /* PDO mapping objects */
   {NULL,NULL, 0x1603, {DEFTYPE_PDOMAPPING, 4 | (OBJCODE_REC << 8)}, asEntryDesc0x1603, aName0x1603, &RxPDOMap, NULL, NULL, 0x0000},
   {NULL,NULL, 0x1A03, {DEFTYPE_PDOMAPPING, 2 | (OBJCODE_REC << 8)}, asEntryDesc0x1A03, aName0x1A03, &TxPDOMap, NULL, NULL, 0x0000},
   /* SM PDO assignment */
   {NULL,NULL, 0x1C12, {DEFTYPE_UNSIGNED16, 2 | (OBJCODE_ARR << 8)}, asPDOAssignEntryDesc, aName0x1C12, &sRxPDOassign, NULL, NULL, 0x0000},
   {NULL,NULL, 0x1C13, {DEFTYPE_UNSIGNED16, 2 | (OBJCODE_ARR << 8)}, asPDOAssignEntryDesc, aName0x1C13, &sTxPDOassign, NULL, NULL, 0x0000},
   /* Outputs — RxPDO mapped, each 1-bit BOOL */
   {NULL,NULL, 0x7001, {DEFTYPE_BOOLEAN, 0 | (OBJCODE_VAR << 8)}, &EntryDesc0x7001, aName0x7001, &LED0_RED,   NULL, NULL, 0x0000},
   {NULL,NULL, 0x7002, {DEFTYPE_BOOLEAN, 0 | (OBJCODE_VAR << 8)}, &EntryDesc0x7002, aName0x7002, &LED1_BLUE,  NULL, NULL, 0x0000},
   {NULL,NULL, 0x7003, {DEFTYPE_BOOLEAN, 0 | (OBJCODE_VAR << 8)}, &EntryDesc0x7003, aName0x7003, &LED2_GREEN, NULL, NULL, 0x0000},
   /* Input — TxPDO mapped, 1-bit BOOL */
   {NULL,NULL, 0x6001, {DEFTYPE_BOOLEAN, 0 | (OBJCODE_VAR << 8)}, &EntryDesc0x6001, aName0x6001, &KEY2,       NULL, NULL, 0x0000},
   /* Terminator */
   {NULL,NULL, 0xFFFF, {0, 0}, NULL, NULL, NULL, NULL, NULL, 0x000}};
#endif

#undef PROTO

#endif // #if (CiA402_SAMPLE_APPLICATION != 1)

#endif //_SAMPLE_APPL_H_

/*-----------------------------------------------------------------------------------------
------
------    Prototype functions
------
-----------------------------------------------------------------------------------------*/

#define PROTO extern

PROTO void APPL_Application(void);
PROTO void APPL_LowPriorityIo(void);
PROTO UINT16 APPL_GetDeviceID(void);
PROTO UINT16 APPL_GetDipSw(void);
PROTO void APPL_SetLed(UINT16 value);

PROTO void   APPL_AckErrorInd(UINT16 stateTrans);
PROTO UINT16 APPL_StartMailboxHandler(void);
PROTO UINT16 APPL_StopMailboxHandler(void);
PROTO UINT16 APPL_StartInputHandler(UINT16 *pIntMask);
PROTO UINT16 APPL_StopInputHandler(void);
PROTO UINT16 APPL_StartOutputHandler(void);
PROTO UINT16 APPL_StopOutputHandler(void);

PROTO UINT16 APPL_GenerateMapping(UINT16 *pInputSize,UINT16 *pOutputSize);
PROTO void APPL_InputMapping(UINT16* pData);
PROTO void APPL_OutputMapping(UINT16* pData);

#if (CiA402_SAMPLE_APPLICATION == 1)
PROTO void CiA402_Application(TCiA402Axis *pCiA402Axis, UINT16 i);
#endif

#undef PROTO
/** @}*/
