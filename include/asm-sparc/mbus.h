/* $Id: mbus.h,v 1.9 1997/06/24 15:48:12 jj Exp $
 * mbus.h:  Various defines for MBUS modules.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_MBUS_H
#define _SPARC_MBUS_H

#include <asm/ross.h>    /* HyperSparc stuff */
#include <asm/cypress.h> /* Cypress Chips */
#include <asm/viking.h>  /* Ugh, bug city... */

enum mbus_module {
	HyperSparc        = 0,
	Cypress           = 1,
	Cypress_vE        = 2,
	Cypress_vD        = 3,
	Swift_ok          = 4,
	Swift_bad_c       = 5,
	Swift_lots_o_bugs = 6,
	Tsunami           = 7,
	Viking_12         = 8,
	Viking_2x         = 9,
	Viking_30         = 10,
	Viking_35         = 11,
	Viking_new        = 12,
	TurboSparc	  = 13,
	SRMMU_INVAL_MOD   = 14,
};

extern enum mbus_module srmmu_modtype;
extern unsigned int viking_rev, swift_rev, cypress_rev;

/* HW Mbus module bugs we have to deal with */
#define HWBUG_COPYBACK_BROKEN        0x00000001
#define HWBUG_ASIFLUSH_BROKEN        0x00000002
#define HWBUG_VACFLUSH_BITROT        0x00000004
#define HWBUG_KERN_ACCBROKEN         0x00000008
#define HWBUG_KERN_CBITBROKEN        0x00000010
#define HWBUG_MODIFIED_BITROT        0x00000020
#define HWBUG_PC_BADFAULT_ADDR       0x00000040
#define HWBUG_SUPERSCALAR_BAD        0x00000080
#define HWBUG_PACINIT_BITROT         0x00000100

extern unsigned int hwbug_bitmask;

/* First the module type values. To find out which you have, just load
 * the mmu control register from ASI_M_MMUREG alternate address space and
 * shift the value right 28 bits.
 */
/* IMPL field means the company which produced the chip. */
#define MBUS_VIKING        0x4   /* bleech, Texas Instruments Module */
#define MBUS_LSI           0x3   /* LSI Logics */
#define MBUS_ROSS          0x1   /* Ross is nice */
#define MBUS_FMI           0x0   /* Fujitsu Microelectronics/Swift */

/* Ross Module versions */
#define ROSS_604_REV_CDE        0x0   /* revisions c, d, and e */
#define ROSS_604_REV_F          0x1   /* revision f */
#define ROSS_605                0xf   /* revision a, a.1, and a.2 */
#define ROSS_605_REV_B          0xe   /* revision b */

/* TI Viking Module versions */
#define VIKING_REV_12           0x1   /* Version 1.2 or SPARCclassic's CPU */
#define VIKING_REV_2            0x2   /* Version 2.1, 2.2, 2.3, and 2.4 */
#define VIKING_REV_30           0x3   /* Version 3.0 */
#define VIKING_REV_35           0x4   /* Version 3.5 */

/* LSI Logics. */
#define LSI_L64815		0x0

/* Fujitsu */
#define FMI_AURORA		0x4   /* MB8690x, a Swift module... */
#define FMI_TURBO		0x5   /* MB86907, a TurboSparc module... */

/* For multiprocessor support we need to be able to obtain the CPU id and
 * the MBUS Module id.
 */

/* The CPU ID is encoded in the trap base register, 20 bits to the left of
 * bit zero, with 2 bits being significant.
 */
#define TBR_ID_SHIFT            20

extern __inline__ int get_cpuid(void)
{
	register int retval;
	__asm__ __volatile__("rd %%tbr, %0\n\t"
			     "srl %0, %1, %0\n\t" :
			     "=r" (retval) :
			     "i" (TBR_ID_SHIFT));
	return (retval & 3);
}

extern __inline__ int get_modid(void)
{
	return (get_cpuid() | 0x8);
}

	
#endif /* !(_SPARC_MBUS_H) */
