/* $Id: ecc.h,v 1.3 1996/04/25 06:12:57 davem Exp $
 * ecc.h: Definitions and defines for the external cache/memory
 *        controller on the sun4m.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_ECC_H
#define _SPARC_ECC_H

/* These registers are accessed through the SRMMU passthrough ASI 0x20 */
#define ECC_ENABLE     0x00000000       /* ECC enable register */
#define ECC_FSTATUS    0x00000008       /* ECC fault status register */
#define ECC_FADDR      0x00000010       /* ECC fault address register */
#define ECC_DIGNOSTIC  0x00000018       /* ECC diagnostics register */
#define ECC_MBAENAB    0x00000020       /* MBus arbiter enable register */
#define ECC_DMESG      0x00001000       /* Diagnostic message passing area */

/* ECC MBus Arbiter Enable register:
 *
 * ----------------------------------------
 * |              |SBUS|MOD3|MOD2|MOD1|RSV|
 * ----------------------------------------
 *  31           5   4   3    2    1    0
 *
 * SBUS: Enable MBus Arbiter on the SBus 0=off 1=on
 * MOD3: Enable MBus Arbiter on MBus module 3  0=off 1=on
 * MOD2: Enable MBus Arbiter on MBus module 2  0=off 1=on
 * MOD1: Enable MBus Arbiter on MBus module 1  0=off 1=on
 */

#define ECC_MBAE_SBUS     0x00000010
#define ECC_MBAE_MOD3     0x00000008
#define ECC_MBAE_MOD2     0x00000004
#define ECC_MBAE_MOD1     0x00000002 

/* ECC Fault Control Register layout:
 *
 * -----------------------------
 * |    RESV   | ECHECK | EINT |
 * -----------------------------
 *  31        2     1       0
 *
 * ECHECK:  Enable ECC checking.  0=off 1=on
 * EINT:  Enable Interrupts for correctable errors. 0=off 1=on
 */ 
#define ECC_FCR_CHECK    0x00000002
#define ECC_FCR_INTENAB  0x00000001

/* ECC Fault Address Register Zero layout:
 *
 * -----------------------------------------------------
 * | MID | S | RSV |  VA   | BM |AT| C| SZ |TYP| PADDR |
 * -----------------------------------------------------
 *  31-28  27 26-22  21-14   13  12 11 10-8 7-4   3-0
 *
 * MID: ModuleID of the faulting processor. ie. who did it?
 * S: Supervisor/Privileged access? 0=no 1=yes
 * VA: Bits 19-12 of the virtual faulting address, these are the
 *     superset bits in the virtual cache and can be used for
 *     a flush operation if necessary.
 * BM: Boot mode? 0=no 1=yes  This is just like the SRMMU boot
 *     mode bit.
 * AT: Did this fault happen during an atomic instruction? 0=no
 *     1=yes.  This means either an 'ldstub' or 'swap' instruction
 *     was in progress (but not finished) when this fault happened.
 *     This indicated whether the bus was locked when the fault
 *     occurred.
 * C: Did the pte for this access indicate that it was cacheable?
 *    0=no 1=yes
 * SZ: The size of the transaction.
 * TYP: The transaction type.
 * PADDR: Bits 35-32 of the physical address for the fault.
 */
#define ECC_FADDR0_MIDMASK   0xf0000000
#define ECC_FADDR0_S         0x08000000
#define ECC_FADDR0_VADDR     0x003fc000
#define ECC_FADDR0_BMODE     0x00002000
#define ECC_FADDR0_ATOMIC    0x00001000
#define ECC_FADDR0_CACHE     0x00000800
#define ECC_FADDR0_SIZE      0x00000700
#define ECC_FADDR0_TYPE      0x000000f0
#define ECC_FADDR0_PADDR     0x0000000f

/* ECC Fault Address Register One layout:
 *
 * -------------------------------------
 * |          Physical Address 31-0    |
 * -------------------------------------
 *  31                               0
 *
 * You get the upper 4 bits of the physical address from the
 * PADDR field in ECC Fault Address Zero register.
 */

/* ECC Fault Status Register layout:
 *
 * ----------------------------------------------
 * | RESV|C2E|MULT|SYNDROME|DWORD|UNC|TIMEO|BS|C|
 * ----------------------------------------------
 *  31-18  17  16    15-8    7-4   3    2    1 0
 *
 * C2E: A C2 graphics error occurred. 0=no 1=yes (SS10 only)
 * MULT: Multiple errors occurred ;-O 0=no 1=prom_panic(yes)
 * SYNDROME: Controller is mentally unstable.
 * DWORD:
 * UNC: Uncorrectable error.  0=no 1=yes
 * TIMEO: Timeout occurred. 0=no 1=yes
 * BS: C2 graphics bad slot access. 0=no 1=yes (SS10 only)
 * C: Correctable error? 0=no 1=yes
 */

#define ECC_FSR_C2ERR    0x00020000
#define ECC_FSR_MULT     0x00010000
#define ECC_FSR_SYND     0x0000ff00
#define ECC_FSR_DWORD    0x000000f0
#define ECC_FSR_UNC      0x00000008
#define ECC_FSR_TIMEO    0x00000004
#define ECC_FSR_BADSLOT  0x00000002
#define ECC_FSR_C        0x00000001

#endif /* !(_SPARC_ECC_H) */
