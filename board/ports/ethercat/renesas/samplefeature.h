/*
 * samplefeature.h
 *
 *  Created on:
 *      Author:
 */

#ifndef ETHERCAT_RENESAS_SAMPLEFEATURE_H_
#define ETHERCAT_RENESAS_SAMPLEFEATURE_H_

#define CIA402_DRIVE 1 // IO+CiA402+FoE
#define SEMI_DEVICE  2 // IO+       FoE+CDP
#define CONFORMANCE  3 // IO+CiA402+FoE+CDP

/*** Select a feature **********************/
/* Switch from CIA402_DRIVE to SEMI_DEVICE for pure I/O slave */
#define ETHERCAT_FEATURE SEMI_DEVICE

/* Disable backup parameter support — no EEPROM emulation functions on this platform */
#define BACKUP_PARAMETER_SUPPORTED 0

/* Match official Renesas ESI Vendor ID (0x766).
   Must be consistent with: ESI <Vendor><Id>, CoE 0x1018:01, EEPROM/SII */
#define VENDOR_ID       0x00000766U
#define PRODUCT_CODE    0x00000912U
#define REVISION_NUMBER 0x00000300U
#define SERIAL_NUMBER   0x00000000U

/* Minimum DC SYNC0 cycle time: 50 μs = 50000 ns = 0xC350
   (ecat_def.h default: 0x7A120 = 500 μs) */
#define MIN_PD_CYCLE_TIME 200000U

#endif /* ETHERCAT_RENESAS_SAMPLEFEATURE_H_ */
