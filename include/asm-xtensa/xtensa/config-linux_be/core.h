/*
 * xtensa/config/core.h -- HAL definitions that are dependent on CORE configuration
 *
 *  This header file is sometimes referred to as the "compile-time HAL" or CHAL.
 *  It was generated for a specific Xtensa processor configuration.
 *
 *  Source for configuration-independent binaries (which link in a
 *  configuration-specific HAL library) must NEVER include this file.
 *  It is perfectly normal, however, for the HAL source itself to include this file.
 */

/*
 * Copyright (c) 2003 Tensilica, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2.1 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307,
 * USA.
 */


#ifndef XTENSA_CONFIG_CORE_H
#define XTENSA_CONFIG_CORE_H

#include <xtensa/hal.h>


/*----------------------------------------------------------------------
				GENERAL
  ----------------------------------------------------------------------*/

/*
 *  Separators for macros that expand into arrays.
 *  These can be predefined by files that #include this one,
 *  when different separators are required.
 */
/*  Element separator for macros that expand into 1-dimensional arrays:  */
#ifndef XCHAL_SEP
#define XCHAL_SEP			,
#endif
/*  Array separator for macros that expand into 2-dimensional arrays:  */
#ifndef XCHAL_SEP2
#define XCHAL_SEP2			},{
#endif


/*----------------------------------------------------------------------
				ENDIANNESS
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_BE			1
#define XCHAL_HAVE_LE			0
#define XCHAL_MEMORY_ORDER		XTHAL_BIGENDIAN


/*----------------------------------------------------------------------
				REGISTER WINDOWS
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_WINDOWED		1	/* 1 if windowed registers option configured, 0 otherwise */
#define XCHAL_NUM_AREGS			64	/* number of physical address regs */
#define XCHAL_NUM_AREGS_LOG2		6	/* log2(XCHAL_NUM_AREGS) */


/*----------------------------------------------------------------------
				ADDRESS ALIGNMENT
  ----------------------------------------------------------------------*/

/*  These apply to a selected set of core load and store instructions only (see ISA):  */
#define XCHAL_UNALIGNED_LOAD_EXCEPTION	1	/* 1 if unaligned loads cause an exception, 0 otherwise */
#define XCHAL_UNALIGNED_STORE_EXCEPTION	1	/* 1 if unaligned stores cause an exception, 0 otherwise */


/*----------------------------------------------------------------------
				INTERRUPTS
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_INTERRUPTS		1	/* 1 if interrupt option configured, 0 otherwise */
#define XCHAL_HAVE_HIGHPRI_INTERRUPTS	1	/* 1 if high-priority interrupt option configured, 0 otherwise */
#define XCHAL_HAVE_HIGHLEVEL_INTERRUPTS	XCHAL_HAVE_HIGHPRI_INTERRUPTS
#define XCHAL_HAVE_NMI			0	/* 1 if NMI option configured, 0 otherwise */
#define XCHAL_NUM_INTERRUPTS		17	/* number of interrupts */
#define XCHAL_NUM_INTERRUPTS_LOG2	5	/* number of bits to hold an interrupt number: roundup(log2(number of interrupts)) */
#define XCHAL_NUM_EXTINTERRUPTS		10	/* number of external interrupts */
#define XCHAL_NUM_INTLEVELS		4	/* number of interrupt levels (not including level zero!) */
#define XCHAL_NUM_LOWPRI_LEVELS		1			/* number of low-priority interrupt levels (always 1) */
#define XCHAL_FIRST_HIGHPRI_LEVEL	(XCHAL_NUM_LOWPRI_LEVELS+1)	/* level of first high-priority interrupt (always 2) */
#define XCHAL_EXCM_LEVEL		1			/* level of interrupts masked by PS.EXCM (XEA2 only; always 1 in T10xx);
								   for XEA1, where there is no PS.EXCM, this is always 1;
								   interrupts at levels FIRST_HIGHPRI <= n <= EXCM_LEVEL, if any,
								   are termed "medium priority" interrupts (post T10xx only) */
/*  Note:  1 <= LOWPRI_LEVELS <= EXCM_LEVEL < DEBUGLEVEL <= NUM_INTLEVELS < NMILEVEL <= 15  */

/*  Masks of interrupts at each interrupt level:  */
#define XCHAL_INTLEVEL0_MASK		0x00000000
#define XCHAL_INTLEVEL1_MASK		0x000064F9
#define XCHAL_INTLEVEL2_MASK		0x00008902
#define XCHAL_INTLEVEL3_MASK		0x00011204
#define XCHAL_INTLEVEL4_MASK		0x00000000
#define XCHAL_INTLEVEL5_MASK		0x00000000
#define XCHAL_INTLEVEL6_MASK		0x00000000
#define XCHAL_INTLEVEL7_MASK		0x00000000
#define XCHAL_INTLEVEL8_MASK		0x00000000
#define XCHAL_INTLEVEL9_MASK		0x00000000
#define XCHAL_INTLEVEL10_MASK		0x00000000
#define XCHAL_INTLEVEL11_MASK		0x00000000
#define XCHAL_INTLEVEL12_MASK		0x00000000
#define XCHAL_INTLEVEL13_MASK		0x00000000
#define XCHAL_INTLEVEL14_MASK		0x00000000
#define XCHAL_INTLEVEL15_MASK		0x00000000
/*  As an array of entries (eg. for C constant arrays):  */
#define XCHAL_INTLEVEL_MASKS		0x00000000	XCHAL_SEP \
					0x000064F9	XCHAL_SEP \
					0x00008902	XCHAL_SEP \
					0x00011204	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000	XCHAL_SEP \
					0x00000000

/*  Masks of interrupts at each range 1..n of interrupt levels:  */
#define XCHAL_INTLEVEL0_ANDBELOW_MASK	0x00000000
#define XCHAL_INTLEVEL1_ANDBELOW_MASK	0x000064F9
#define XCHAL_INTLEVEL2_ANDBELOW_MASK	0x0000EDFB
#define XCHAL_INTLEVEL3_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL4_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL5_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL6_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL7_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL8_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL9_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL10_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL11_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL12_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL13_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL14_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_INTLEVEL15_ANDBELOW_MASK	0x0001FFFF
#define XCHAL_LOWPRI_MASK		XCHAL_INTLEVEL1_ANDBELOW_MASK	/* mask of all low-priority interrupts */
#define XCHAL_EXCM_MASK			XCHAL_INTLEVEL1_ANDBELOW_MASK	/* mask of all interrupts masked by PS.EXCM (or CEXCM) */
/*  As an array of entries (eg. for C constant arrays):  */
#define XCHAL_INTLEVEL_ANDBELOW_MASKS	0x00000000	XCHAL_SEP \
					0x000064F9	XCHAL_SEP \
					0x0000EDFB	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF	XCHAL_SEP \
					0x0001FFFF

/*  Interrupt numbers for each interrupt level at which only one interrupt was configured:  */
/*#define XCHAL_INTLEVEL1_NUM		...more than one interrupt at this level...*/
/*#define XCHAL_INTLEVEL2_NUM		...more than one interrupt at this level...*/
/*#define XCHAL_INTLEVEL3_NUM		...more than one interrupt at this level...*/

/*  Level of each interrupt:  */
#define XCHAL_INT0_LEVEL		1
#define XCHAL_INT1_LEVEL		2
#define XCHAL_INT2_LEVEL		3
#define XCHAL_INT3_LEVEL		1
#define XCHAL_INT4_LEVEL		1
#define XCHAL_INT5_LEVEL		1
#define XCHAL_INT6_LEVEL		1
#define XCHAL_INT7_LEVEL		1
#define XCHAL_INT8_LEVEL		2
#define XCHAL_INT9_LEVEL		3
#define XCHAL_INT10_LEVEL		1
#define XCHAL_INT11_LEVEL		2
#define XCHAL_INT12_LEVEL		3
#define XCHAL_INT13_LEVEL		1
#define XCHAL_INT14_LEVEL		1
#define XCHAL_INT15_LEVEL		2
#define XCHAL_INT16_LEVEL		3
#define XCHAL_INT17_LEVEL		0
#define XCHAL_INT18_LEVEL		0
#define XCHAL_INT19_LEVEL		0
#define XCHAL_INT20_LEVEL		0
#define XCHAL_INT21_LEVEL		0
#define XCHAL_INT22_LEVEL		0
#define XCHAL_INT23_LEVEL		0
#define XCHAL_INT24_LEVEL		0
#define XCHAL_INT25_LEVEL		0
#define XCHAL_INT26_LEVEL		0
#define XCHAL_INT27_LEVEL		0
#define XCHAL_INT28_LEVEL		0
#define XCHAL_INT29_LEVEL		0
#define XCHAL_INT30_LEVEL		0
#define XCHAL_INT31_LEVEL		0
/*  As an array of entries (eg. for C constant arrays):  */
#define XCHAL_INT_LEVELS		1	XCHAL_SEP \
					2	XCHAL_SEP \
					3	XCHAL_SEP \
					1	XCHAL_SEP \
					1	XCHAL_SEP \
					1	XCHAL_SEP \
					1	XCHAL_SEP \
					1	XCHAL_SEP \
					2	XCHAL_SEP \
					3	XCHAL_SEP \
					1	XCHAL_SEP \
					2	XCHAL_SEP \
					3	XCHAL_SEP \
					1	XCHAL_SEP \
					1	XCHAL_SEP \
					2	XCHAL_SEP \
					3	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0	XCHAL_SEP \
					0

/*  Type of each interrupt:  */
#define XCHAL_INT0_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT1_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT2_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT3_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT4_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT5_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT6_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT7_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT8_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT9_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT10_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT11_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT12_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT13_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT14_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT15_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT16_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT17_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT18_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT19_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT20_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT21_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT22_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT23_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT24_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT25_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT26_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT27_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT28_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT29_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT30_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
#define XCHAL_INT31_TYPE 	XTHAL_INTTYPE_UNCONFIGURED
/*  As an array of entries (eg. for C constant arrays):  */
#define XCHAL_INT_TYPES		XTHAL_INTTYPE_EXTERN_LEVEL     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_LEVEL     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_LEVEL     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_LEVEL     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_LEVEL     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_LEVEL     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_LEVEL     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_EDGE     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_EDGE     	XCHAL_SEP \
				XTHAL_INTTYPE_EXTERN_EDGE     	XCHAL_SEP \
				XTHAL_INTTYPE_TIMER     	XCHAL_SEP \
				XTHAL_INTTYPE_TIMER     	XCHAL_SEP \
				XTHAL_INTTYPE_TIMER     	XCHAL_SEP \
				XTHAL_INTTYPE_SOFTWARE     	XCHAL_SEP \
				XTHAL_INTTYPE_SOFTWARE     	XCHAL_SEP \
				XTHAL_INTTYPE_SOFTWARE     	XCHAL_SEP \
				XTHAL_INTTYPE_SOFTWARE     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED     	XCHAL_SEP \
				XTHAL_INTTYPE_UNCONFIGURED

/*  Masks of interrupts for each type of interrupt:  */
#define XCHAL_INTTYPE_MASK_UNCONFIGURED	0xFFFE0000
#define XCHAL_INTTYPE_MASK_SOFTWARE	0x0001E000
#define XCHAL_INTTYPE_MASK_EXTERN_EDGE	0x00000380
#define XCHAL_INTTYPE_MASK_EXTERN_LEVEL	0x0000007F
#define XCHAL_INTTYPE_MASK_TIMER	0x00001C00
#define XCHAL_INTTYPE_MASK_NMI		0x00000000
/*  As an array of entries (eg. for C constant arrays):  */
#define XCHAL_INTTYPE_MASKS		0xFFFE0000	XCHAL_SEP \
					0x0001E000	XCHAL_SEP \
					0x00000380	XCHAL_SEP \
					0x0000007F	XCHAL_SEP \
					0x00001C00	XCHAL_SEP \
					0x00000000

/*  Interrupts assigned to each timer (CCOMPARE0 to CCOMPARE3), -1 if unassigned  */
#define XCHAL_TIMER0_INTERRUPT	10
#define XCHAL_TIMER1_INTERRUPT	11
#define XCHAL_TIMER2_INTERRUPT	12
#define XCHAL_TIMER3_INTERRUPT	XTHAL_TIMER_UNCONFIGURED
/*  As an array of entries (eg. for C constant arrays):  */
#define XCHAL_TIMER_INTERRUPTS	10	XCHAL_SEP \
				11	XCHAL_SEP \
				12	XCHAL_SEP \
				XTHAL_TIMER_UNCONFIGURED

/*  Indexing macros:  */
#define _XCHAL_INTLEVEL_MASK(n)		XCHAL_INTLEVEL ## n ## _MASK
#define XCHAL_INTLEVEL_MASK(n)		_XCHAL_INTLEVEL_MASK(n)		/* n = 0 .. 15 */
#define _XCHAL_INTLEVEL_ANDBELOWMASK(n)	XCHAL_INTLEVEL ## n ## _ANDBELOW_MASK
#define XCHAL_INTLEVEL_ANDBELOW_MASK(n)	_XCHAL_INTLEVEL_ANDBELOWMASK(n)	/* n = 0 .. 15 */
#define _XCHAL_INT_LEVEL(n)		XCHAL_INT ## n ## _LEVEL
#define XCHAL_INT_LEVEL(n)		_XCHAL_INT_LEVEL(n)		/* n = 0 .. 31 */
#define _XCHAL_INT_TYPE(n)		XCHAL_INT ## n ## _TYPE
#define XCHAL_INT_TYPE(n)		_XCHAL_INT_TYPE(n)		/* n = 0 .. 31 */
#define _XCHAL_TIMER_INTERRUPT(n)	XCHAL_TIMER ## n ## _INTERRUPT
#define XCHAL_TIMER_INTERRUPT(n)	_XCHAL_TIMER_INTERRUPT(n)	/* n = 0 .. 3 */



/*
 *  External interrupt vectors/levels.
 *  These macros describe how Xtensa processor interrupt numbers
 *  (as numbered internally, eg. in INTERRUPT and INTENABLE registers)
 *  map to external BInterrupt<n> pins, for those interrupts
 *  configured as external (level-triggered, edge-triggered, or NMI).
 *  See the Xtensa processor databook for more details.
 */

/*  Core interrupt numbers mapped to each EXTERNAL interrupt number:  */
#define XCHAL_EXTINT0_NUM		0	/* (intlevel 1) */
#define XCHAL_EXTINT1_NUM		1	/* (intlevel 2) */
#define XCHAL_EXTINT2_NUM		2	/* (intlevel 3) */
#define XCHAL_EXTINT3_NUM		3	/* (intlevel 1) */
#define XCHAL_EXTINT4_NUM		4	/* (intlevel 1) */
#define XCHAL_EXTINT5_NUM		5	/* (intlevel 1) */
#define XCHAL_EXTINT6_NUM		6	/* (intlevel 1) */
#define XCHAL_EXTINT7_NUM		7	/* (intlevel 1) */
#define XCHAL_EXTINT8_NUM		8	/* (intlevel 2) */
#define XCHAL_EXTINT9_NUM		9	/* (intlevel 3) */

/*  Corresponding interrupt masks:  */
#define XCHAL_EXTINT0_MASK		0x00000001
#define XCHAL_EXTINT1_MASK		0x00000002
#define XCHAL_EXTINT2_MASK		0x00000004
#define XCHAL_EXTINT3_MASK		0x00000008
#define XCHAL_EXTINT4_MASK		0x00000010
#define XCHAL_EXTINT5_MASK		0x00000020
#define XCHAL_EXTINT6_MASK		0x00000040
#define XCHAL_EXTINT7_MASK		0x00000080
#define XCHAL_EXTINT8_MASK		0x00000100
#define XCHAL_EXTINT9_MASK		0x00000200

/*  Core config interrupt levels mapped to each external interrupt:  */
#define XCHAL_EXTINT0_LEVEL		1	/* (int number 0) */
#define XCHAL_EXTINT1_LEVEL		2	/* (int number 1) */
#define XCHAL_EXTINT2_LEVEL		3	/* (int number 2) */
#define XCHAL_EXTINT3_LEVEL		1	/* (int number 3) */
#define XCHAL_EXTINT4_LEVEL		1	/* (int number 4) */
#define XCHAL_EXTINT5_LEVEL		1	/* (int number 5) */
#define XCHAL_EXTINT6_LEVEL		1	/* (int number 6) */
#define XCHAL_EXTINT7_LEVEL		1	/* (int number 7) */
#define XCHAL_EXTINT8_LEVEL		2	/* (int number 8) */
#define XCHAL_EXTINT9_LEVEL		3	/* (int number 9) */


/*----------------------------------------------------------------------
			EXCEPTIONS and VECTORS
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_EXCEPTIONS		1	/* 1 if exception option configured, 0 otherwise */

#define XCHAL_XEA_VERSION		2	/* Xtensa Exception Architecture number: 1 for XEA1 (old), 2 for XEA2 (new) */
#define XCHAL_HAVE_XEA1			0	/* 1 if XEA1, 0 otherwise */
#define XCHAL_HAVE_XEA2			1	/* 1 if XEA2, 0 otherwise */
/*  For backward compatibility ONLY -- DO NOT USE (will be removed in future release):  */
#define XCHAL_HAVE_OLD_EXC_ARCH		XCHAL_HAVE_XEA1	/* (DEPRECATED) 1 if old exception architecture (XEA1), 0 otherwise (eg. XEA2) */
#define XCHAL_HAVE_EXCM			XCHAL_HAVE_XEA2	/* (DEPRECATED) 1 if PS.EXCM bit exists (currently equals XCHAL_HAVE_TLBS) */

#define XCHAL_RESET_VECTOR_VADDR	0xFE000020
#define XCHAL_RESET_VECTOR_PADDR	0xFE000020
#define XCHAL_USER_VECTOR_VADDR		0xD0000220
#define XCHAL_PROGRAMEXC_VECTOR_VADDR	XCHAL_USER_VECTOR_VADDR		/* for backward compatibility */
#define XCHAL_USEREXC_VECTOR_VADDR	XCHAL_USER_VECTOR_VADDR		/* for backward compatibility */
#define XCHAL_USER_VECTOR_PADDR		0x00000220
#define XCHAL_PROGRAMEXC_VECTOR_PADDR	XCHAL_USER_VECTOR_PADDR		/* for backward compatibility */
#define XCHAL_USEREXC_VECTOR_PADDR	XCHAL_USER_VECTOR_PADDR		/* for backward compatibility */
#define XCHAL_KERNEL_VECTOR_VADDR	0xD0000200
#define XCHAL_STACKEDEXC_VECTOR_VADDR	XCHAL_KERNEL_VECTOR_VADDR	/* for backward compatibility */
#define XCHAL_KERNELEXC_VECTOR_VADDR	XCHAL_KERNEL_VECTOR_VADDR	/* for backward compatibility */
#define XCHAL_KERNEL_VECTOR_PADDR	0x00000200
#define XCHAL_STACKEDEXC_VECTOR_PADDR	XCHAL_KERNEL_VECTOR_PADDR	/* for backward compatibility */
#define XCHAL_KERNELEXC_VECTOR_PADDR	XCHAL_KERNEL_VECTOR_PADDR	/* for backward compatibility */
#define XCHAL_DOUBLEEXC_VECTOR_VADDR	0xD0000290
#define XCHAL_DOUBLEEXC_VECTOR_PADDR	0x00000290
#define XCHAL_WINDOW_VECTORS_VADDR	0xD0000000
#define XCHAL_WINDOW_VECTORS_PADDR	0x00000000
#define XCHAL_INTLEVEL2_VECTOR_VADDR	0xD0000240
#define XCHAL_INTLEVEL2_VECTOR_PADDR	0x00000240
#define XCHAL_INTLEVEL3_VECTOR_VADDR	0xD0000250
#define XCHAL_INTLEVEL3_VECTOR_PADDR	0x00000250
#define XCHAL_INTLEVEL4_VECTOR_VADDR	0xFE000520
#define XCHAL_INTLEVEL4_VECTOR_PADDR	0xFE000520
#define XCHAL_DEBUG_VECTOR_VADDR	XCHAL_INTLEVEL4_VECTOR_VADDR
#define XCHAL_DEBUG_VECTOR_PADDR	XCHAL_INTLEVEL4_VECTOR_PADDR

/*  Indexing macros:  */
#define _XCHAL_INTLEVEL_VECTOR_VADDR(n)		XCHAL_INTLEVEL ## n ## _VECTOR_VADDR
#define XCHAL_INTLEVEL_VECTOR_VADDR(n)		_XCHAL_INTLEVEL_VECTOR_VADDR(n)		/* n = 0 .. 15 */

/*
 *  General Exception Causes
 *  (values of EXCCAUSE special register set by general exceptions,
 *   which vector to the user, kernel, or double-exception vectors):
 */
#define XCHAL_EXCCAUSE_ILLEGAL_INSTRUCTION		0	/* Illegal Instruction (IllegalInstruction) */
#define XCHAL_EXCCAUSE_SYSTEM_CALL			1	/* System Call (SystemCall) */
#define XCHAL_EXCCAUSE_INSTRUCTION_FETCH_ERROR		2	/* Instruction Fetch Error (InstructionFetchError) */
#define XCHAL_EXCCAUSE_LOAD_STORE_ERROR			3	/* Load Store Error (LoadStoreError) */
#define XCHAL_EXCCAUSE_LEVEL1_INTERRUPT			4	/* Level 1 Interrupt (Level1Interrupt) */
#define XCHAL_EXCCAUSE_ALLOCA				5	/* Stack Extension Assist (Alloca) */
#define XCHAL_EXCCAUSE_INTEGER_DIVIDE_BY_ZERO		6	/* Integer Divide by Zero (IntegerDivideByZero) */
#define XCHAL_EXCCAUSE_SPECULATION			7	/* Speculation (Speculation) */
#define XCHAL_EXCCAUSE_PRIVILEGED			8	/* Privileged Instruction (Privileged) */
#define XCHAL_EXCCAUSE_UNALIGNED			9	/* Unaligned Load Store (Unaligned) */
#define XCHAL_EXCCAUSE_ITLB_MISS			16	/* ITlb Miss Exception (ITlbMiss) */
#define XCHAL_EXCCAUSE_ITLB_MULTIHIT			17	/* ITlb Mutltihit Exception (ITlbMultihit) */
#define XCHAL_EXCCAUSE_ITLB_PRIVILEGE			18	/* ITlb Privilege Exception (ITlbPrivilege) */
#define XCHAL_EXCCAUSE_ITLB_SIZE_RESTRICTION		19	/* ITlb Size Restriction Exception (ITlbSizeRestriction) */
#define XCHAL_EXCCAUSE_FETCH_CACHE_ATTRIBUTE		20	/* Fetch Cache Attribute Exception (FetchCacheAttribute) */
#define XCHAL_EXCCAUSE_DTLB_MISS			24	/* DTlb Miss Exception (DTlbMiss) */
#define XCHAL_EXCCAUSE_DTLB_MULTIHIT			25	/* DTlb Multihit Exception (DTlbMultihit) */
#define XCHAL_EXCCAUSE_DTLB_PRIVILEGE			26	/* DTlb Privilege Exception (DTlbPrivilege) */
#define XCHAL_EXCCAUSE_DTLB_SIZE_RESTRICTION		27	/* DTlb Size Restriction Exception (DTlbSizeRestriction) */
#define XCHAL_EXCCAUSE_LOAD_CACHE_ATTRIBUTE		28	/* Load Cache Attribute Exception (LoadCacheAttribute) */
#define XCHAL_EXCCAUSE_STORE_CACHE_ATTRIBUTE		29	/* Store Cache Attribute Exception (StoreCacheAttribute) */
#define XCHAL_EXCCAUSE_FLOATING_POINT			40	/* Floating Point Exception (FloatingPoint) */



/*----------------------------------------------------------------------
				TIMERS
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_CCOUNT		1	/* 1 if have CCOUNT, 0 otherwise */
/*#define XCHAL_HAVE_TIMERS		XCHAL_HAVE_CCOUNT*/
#define XCHAL_NUM_TIMERS		3	/* number of CCOMPAREn regs */



/*----------------------------------------------------------------------
				DEBUG
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_DEBUG		1	/* 1 if debug option configured, 0 otherwise */
#define XCHAL_HAVE_OCD			1	/* 1 if OnChipDebug option configured, 0 otherwise */
#define XCHAL_NUM_IBREAK		2	/* number of IBREAKn regs */
#define XCHAL_NUM_DBREAK		2	/* number of DBREAKn regs */
#define XCHAL_DEBUGLEVEL		4	/* debug interrupt level */
/*DebugExternalInterrupt		0		0|1*/
/*DebugUseDIRArray			0		0|1*/




/*----------------------------------------------------------------------
			COPROCESSORS and EXTRA STATE
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_CP			0	/* 1 if coprocessor option configured (CPENABLE present) */
#define XCHAL_CP_MAXCFG			0	/* max allowed cp id plus one (per cfg) */

#include <xtensa/config/tie.h>




/*----------------------------------------------------------------------
			INTERNAL I/D RAM/ROMs and XLMI
  ----------------------------------------------------------------------*/

#define XCHAL_NUM_INSTROM		0	/* number of core instruction ROMs configured */
#define XCHAL_NUM_INSTRAM		0	/* number of core instruction RAMs configured */
#define XCHAL_NUM_DATAROM		0	/* number of core data ROMs configured */
#define XCHAL_NUM_DATARAM		0	/* number of core data RAMs configured */
#define XCHAL_NUM_XLMI			0	/* number of core XLMI ports configured */
#define  XCHAL_NUM_IROM			XCHAL_NUM_INSTROM	/* (DEPRECATED) */
#define  XCHAL_NUM_IRAM			XCHAL_NUM_INSTRAM	/* (DEPRECATED) */
#define  XCHAL_NUM_DROM			XCHAL_NUM_DATAROM	/* (DEPRECATED) */
#define  XCHAL_NUM_DRAM			XCHAL_NUM_DATARAM	/* (DEPRECATED) */



/*----------------------------------------------------------------------
				CACHE
  ----------------------------------------------------------------------*/

/*  Size of the cache lines in log2(bytes):  */
#define XCHAL_ICACHE_LINEWIDTH		4
#define XCHAL_DCACHE_LINEWIDTH		4
/*  Size of the cache lines in bytes:  */
#define XCHAL_ICACHE_LINESIZE		16
#define XCHAL_DCACHE_LINESIZE		16
/*  Max for both I-cache and D-cache (used for general alignment):  */
#define XCHAL_CACHE_LINEWIDTH_MAX	4
#define XCHAL_CACHE_LINESIZE_MAX	16

/*  Number of cache sets in log2(lines per way):  */
#define XCHAL_ICACHE_SETWIDTH		8
#define XCHAL_DCACHE_SETWIDTH		8
/*  Max for both I-cache and D-cache (used for general cache-coherency page alignment):  */
#define XCHAL_CACHE_SETWIDTH_MAX	8
#define XCHAL_CACHE_SETSIZE_MAX		256

/*  Cache set associativity (number of ways):  */
#define XCHAL_ICACHE_WAYS		2
#define XCHAL_DCACHE_WAYS		2

/*  Size of the caches in bytes (ways * 2^(linewidth + setwidth)):  */
#define XCHAL_ICACHE_SIZE		8192
#define XCHAL_DCACHE_SIZE		8192

/*  Cache features:  */
#define XCHAL_DCACHE_IS_WRITEBACK	0
/*  Whether cache locking feature is available:  */
#define XCHAL_ICACHE_LINE_LOCKABLE	0
#define XCHAL_DCACHE_LINE_LOCKABLE	0

/*  Number of (encoded) cache attribute bits:  */
#define XCHAL_CA_BITS			4	/* number of bits needed to hold cache attribute encoding */
/*  (The number of access mode bits (decoded cache attribute bits) is defined by the architecture; see xtensa/hal.h?)  */


/*  Cache Attribute encodings -- lists of access modes for each cache attribute:  */
#define XCHAL_FCA_LIST		XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_BYPASS	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_BYPASS	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_CACHED	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_CACHED	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_CACHED	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_CACHED	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION
#define XCHAL_LCA_LIST		XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_BYPASSG	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_BYPASSG	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_NACACHED	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_NACACHED	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_ISOLATE	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_CACHED
#define XCHAL_SCA_LIST		XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_BYPASS	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_WRITETHRU	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_WRITETHRU	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_ISOLATE	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_WRITETHRU

/*  Test:
	read/only: 0 + 1 + 2 + 4 + 5 + 6 + 8 + 9 + 10 + 12 + 14
	read/only: 0 + 1 + 2 + 4 + 5 + 6 + 8 + 9 + 10 + 12 + 14
	all:       0 + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 11 + 12 + 13 + 14 + 15
	fault:     0 + 2 + 4 + 6 + 8 + 10 + 12 + 14
	r/w/x cached:
	r/w/x dcached:
	I-bypass:  1 + 3

	load guard bit set: 1 + 3
	load guard bit clr: 0 + 2 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 11 + 12 + 13 + 14 + 15
	hit-cache r/w/x: 7 + 11

	fams: 5
	fams: 0 / 6 / 18 / 1 / 2
	fams: Bypass / Isolate / Cached / Exception / NACached

        MMU okay:  yes
*/


/*----------------------------------------------------------------------
				MMU
  ----------------------------------------------------------------------*/

/*
 *  General notes on MMU parameters.
 *
 *  Terminology:
 *	ASID = address-space ID (acts as an "extension" of virtual addresses)
 *	VPN  = virtual page number
 *	PPN  = physical page number
 *	CA   = encoded cache attribute (access modes)
 *	TLB  = translation look-aside buffer (term is stretched somewhat here)
 *	I    = instruction (fetch accesses)
 *	D    = data (load and store accesses)
 *	way  = each TLB (ITLB and DTLB) consists of a number of "ways"
 *		that simultaneously match the virtual address of an access;
 *		a TLB successfully translates a virtual address if exactly
 *		one way matches the vaddr; if none match, it is a miss;
 *		if multiple match, one gets a "multihit" exception;
 *		each way can be independently configured in terms of number of
 *		entries, page sizes, which fields are writable or constant, etc.
 *	set  = group of contiguous ways with exactly identical parameters
 *	ARF  = auto-refill; hardware services a 1st-level miss by loading a PTE
 *		from the page table and storing it in one of the auto-refill ways;
 *		if this PTE load also misses, a miss exception is posted for s/w.
 *	min-wired = a "min-wired" way can be used to map a single (minimum-sized)
 * 		page arbitrarily under program control; it has a single entry,
 *		is non-auto-refill (some other way(s) must be auto-refill),
 *		all its fields (VPN, PPN, ASID, CA) are all writable, and it
 *		supports the XCHAL_MMU_MIN_PTE_PAGE_SIZE page size (a current
 *		restriction is that this be the only page size it supports).
 *
 *  TLB way entries are virtually indexed.
 *  TLB ways that support multiple page sizes:
 *	- must have all writable VPN and PPN fields;
 *	- can only use one page size at any given time (eg. setup at startup),
 *	  selected by the respective ITLBCFG or DTLBCFG special register,
 *	  whose bits n*4+3 .. n*4 index the list of page sizes for way n
 *	  (XCHAL_xTLB_SETm_PAGESZ_LOG2_LIST for set m corresponding to way n);
 *	  this list may be sparse for auto-refill ways because auto-refill
 *	  ways have independent lists of supported page sizes sharing a
 *	  common encoding with PTE entries; the encoding is the index into
 *	  this list; unsupported sizes for a given way are zero in the list;
 *	  selecting unsupported sizes results in undefined hardware behaviour;
 *	- is only possible for ways 0 thru 7 (due to ITLBCFG/DTLBCFG definition).
 */

#define XCHAL_HAVE_CACHEATTR		0	/* 1 if CACHEATTR register present, 0 if TLBs present instead */
#define XCHAL_HAVE_TLBS			1	/* 1 if TLBs present, 0 if CACHEATTR present instead */
#define XCHAL_HAVE_MMU			XCHAL_HAVE_TLBS	/* (DEPRECATED; use XCHAL_HAVE_TLBS instead; will be removed in future release) */
#define XCHAL_HAVE_SPANNING_WAY		0	/* 1 if single way maps entire virtual address space in I+D */
#define XCHAL_HAVE_IDENTITY_MAP		0	/* 1 if virtual addr == physical addr always, 0 otherwise */
#define XCHAL_HAVE_MIMIC_CACHEATTR	0	/* 1 if have MMU that mimics a CACHEATTR config (CaMMU) */
#define XCHAL_HAVE_XLT_CACHEATTR	0	/* 1 if have MMU that mimics a CACHEATTR config, but with translation (CaXltMMU) */

#define XCHAL_MMU_ASID_BITS		8	/* number of bits in ASIDs (address space IDs) */
#define XCHAL_MMU_ASID_INVALID		0	/* ASID value indicating invalid address space */
#define XCHAL_MMU_ASID_KERNEL		1	/* ASID value indicating kernel (ring 0) address space */
#define XCHAL_MMU_RINGS			4	/* number of rings supported (1..4) */
#define XCHAL_MMU_RING_BITS		2	/* number of bits needed to hold ring number */
#define XCHAL_MMU_SR_BITS		0	/* number of size-restriction bits supported */
#define XCHAL_MMU_CA_BITS		4	/* number of bits needed to hold cache attribute encoding */
#define XCHAL_MMU_MAX_PTE_PAGE_SIZE	12	/* max page size in a PTE structure (log2) */
#define XCHAL_MMU_MIN_PTE_PAGE_SIZE	12	/* min page size in a PTE structure (log2) */


/***  Instruction TLB:  ***/

#define XCHAL_ITLB_WAY_BITS		3	/* number of bits holding the ways */
#define XCHAL_ITLB_WAYS			7	/* number of ways (n-way set-associative TLB) */
#define XCHAL_ITLB_ARF_WAYS		4	/* number of auto-refill ways */
#define XCHAL_ITLB_SETS			4	/* number of sets (groups of ways with identical settings) */

/*  Way set to which each way belongs:  */
#define XCHAL_ITLB_WAY0_SET		0
#define XCHAL_ITLB_WAY1_SET		0
#define XCHAL_ITLB_WAY2_SET		0
#define XCHAL_ITLB_WAY3_SET		0
#define XCHAL_ITLB_WAY4_SET		1
#define XCHAL_ITLB_WAY5_SET		2
#define XCHAL_ITLB_WAY6_SET		3

/*  Ways sets that are used by hardware auto-refill (ARF):  */
#define XCHAL_ITLB_ARF_SETS		1	/* number of auto-refill sets */
#define XCHAL_ITLB_ARF_SET0		0	/* index of n'th auto-refill set */

/*  Way sets that are "min-wired" (see terminology comment above):  */
#define XCHAL_ITLB_MINWIRED_SETS	0	/* number of "min-wired" sets */


/*  ITLB way set 0 (group of ways 0 thru 3):  */
#define XCHAL_ITLB_SET0_WAY			0	/* index of first way in this way set */
#define XCHAL_ITLB_SET0_WAYS			4	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET0_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET0_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET0_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET0_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET0_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET0_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET0_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET0_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET0_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET0_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET0_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET0_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET0_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET0_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET0_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET0_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  ITLB way set 1 (group of ways 4 thru 4):  */
#define XCHAL_ITLB_SET1_WAY			4	/* index of first way in this way set */
#define XCHAL_ITLB_SET1_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET1_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET1_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET1_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET1_PAGESIZES		4	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET1_PAGESZ_BITS		2	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET1_PAGESZ_LOG2_MIN		20	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET1_PAGESZ_LOG2_MAX		26	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET1_PAGESZ_LOG2_LIST	20 XCHAL_SEP 22 XCHAL_SEP 24 XCHAL_SEP 26	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET1_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET1_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET1_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET1_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET1_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET1_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET1_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET1_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  ITLB way set 2 (group of ways 5 thru 5):  */
#define XCHAL_ITLB_SET2_WAY			5	/* index of first way in this way set */
#define XCHAL_ITLB_SET2_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET2_ENTRIES_LOG2		1	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET2_ENTRIES			2	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET2_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET2_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET2_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET2_PAGESZ_LOG2_MIN		27	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET2_PAGESZ_LOG2_MAX		27	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET2_PAGESZ_LOG2_LIST	27	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET2_ASID_CONSTMASK		0xFF	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET2_VPN_CONSTMASK		0xF0000000	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET2_PPN_CONSTMASK		0xF8000000	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET2_CA_CONSTMASK		0x0000000F	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET2_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET2_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET2_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET2_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */
/*  Constant ASID values for each entry of ITLB way set 2 (because ASID_CONSTMASK is non-zero):  */
#define XCHAL_ITLB_SET2_E0_ASID_CONST		0x01
#define XCHAL_ITLB_SET2_E1_ASID_CONST		0x01
/*  Constant VPN values for each entry of ITLB way set 2 (because VPN_CONSTMASK is non-zero):  */
#define XCHAL_ITLB_SET2_E0_VPN_CONST		0xD0000000
#define XCHAL_ITLB_SET2_E1_VPN_CONST		0xD8000000
/*  Constant PPN values for each entry of ITLB way set 2 (because PPN_CONSTMASK is non-zero):  */
#define XCHAL_ITLB_SET2_E0_PPN_CONST		0x00000000
#define XCHAL_ITLB_SET2_E1_PPN_CONST		0x00000000
/*  Constant CA values for each entry of ITLB way set 2 (because CA_CONSTMASK is non-zero):  */
#define XCHAL_ITLB_SET2_E0_CA_CONST		0x07
#define XCHAL_ITLB_SET2_E1_CA_CONST		0x03

/*  ITLB way set 3 (group of ways 6 thru 6):  */
#define XCHAL_ITLB_SET3_WAY			6	/* index of first way in this way set */
#define XCHAL_ITLB_SET3_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET3_ENTRIES_LOG2		1	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET3_ENTRIES			2	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET3_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET3_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET3_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET3_PAGESZ_LOG2_MIN		28	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET3_PAGESZ_LOG2_MAX		28	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET3_PAGESZ_LOG2_LIST	28	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET3_ASID_CONSTMASK		0xFF	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET3_VPN_CONSTMASK		0xE0000000	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET3_PPN_CONSTMASK		0xF0000000	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET3_CA_CONSTMASK		0x0000000F	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET3_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET3_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET3_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET3_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */
/*  Constant ASID values for each entry of ITLB way set 3 (because ASID_CONSTMASK is non-zero):  */
#define XCHAL_ITLB_SET3_E0_ASID_CONST		0x01
#define XCHAL_ITLB_SET3_E1_ASID_CONST		0x01
/*  Constant VPN values for each entry of ITLB way set 3 (because VPN_CONSTMASK is non-zero):  */
#define XCHAL_ITLB_SET3_E0_VPN_CONST		0xE0000000
#define XCHAL_ITLB_SET3_E1_VPN_CONST		0xF0000000
/*  Constant PPN values for each entry of ITLB way set 3 (because PPN_CONSTMASK is non-zero):  */
#define XCHAL_ITLB_SET3_E0_PPN_CONST		0xF0000000
#define XCHAL_ITLB_SET3_E1_PPN_CONST		0xF0000000
/*  Constant CA values for each entry of ITLB way set 3 (because CA_CONSTMASK is non-zero):  */
#define XCHAL_ITLB_SET3_E0_CA_CONST		0x07
#define XCHAL_ITLB_SET3_E1_CA_CONST		0x03

/*  Indexing macros:  */
#define _XCHAL_ITLB_SET(n,_what)	XCHAL_ITLB_SET ## n ## _what
#define XCHAL_ITLB_SET(n,what)		_XCHAL_ITLB_SET(n, _ ## what )
#define _XCHAL_ITLB_SET_E(n,i,_what)	XCHAL_ITLB_SET ## n ## _E ## i ## _what
#define XCHAL_ITLB_SET_E(n,i,what)	_XCHAL_ITLB_SET_E(n,i, _ ## what )
/*
 *  Example use:  XCHAL_ITLB_SET(XCHAL_ITLB_ARF_SET0,ENTRIES)
 *	to get the value of XCHAL_ITLB_SET<n>_ENTRIES where <n> is the first auto-refill set.
 */


/***  Data TLB:  ***/

#define XCHAL_DTLB_WAY_BITS		4	/* number of bits holding the ways */
#define XCHAL_DTLB_WAYS			10	/* number of ways (n-way set-associative TLB) */
#define XCHAL_DTLB_ARF_WAYS		4	/* number of auto-refill ways */
#define XCHAL_DTLB_SETS			5	/* number of sets (groups of ways with identical settings) */

/*  Way set to which each way belongs:  */
#define XCHAL_DTLB_WAY0_SET		0
#define XCHAL_DTLB_WAY1_SET		0
#define XCHAL_DTLB_WAY2_SET		0
#define XCHAL_DTLB_WAY3_SET		0
#define XCHAL_DTLB_WAY4_SET		1
#define XCHAL_DTLB_WAY5_SET		2
#define XCHAL_DTLB_WAY6_SET		3
#define XCHAL_DTLB_WAY7_SET		4
#define XCHAL_DTLB_WAY8_SET		4
#define XCHAL_DTLB_WAY9_SET		4

/*  Ways sets that are used by hardware auto-refill (ARF):  */
#define XCHAL_DTLB_ARF_SETS		1	/* number of auto-refill sets */
#define XCHAL_DTLB_ARF_SET0		0	/* index of n'th auto-refill set */

/*  Way sets that are "min-wired" (see terminology comment above):  */
#define XCHAL_DTLB_MINWIRED_SETS	1	/* number of "min-wired" sets */
#define XCHAL_DTLB_MINWIRED_SET0	4	/* index of n'th "min-wired" set */


/*  DTLB way set 0 (group of ways 0 thru 3):  */
#define XCHAL_DTLB_SET0_WAY			0	/* index of first way in this way set */
#define XCHAL_DTLB_SET0_WAYS			4	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET0_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET0_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET0_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET0_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET0_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET0_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET0_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET0_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET0_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET0_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET0_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET0_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET0_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET0_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET0_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET0_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 1 (group of ways 4 thru 4):  */
#define XCHAL_DTLB_SET1_WAY			4	/* index of first way in this way set */
#define XCHAL_DTLB_SET1_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET1_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET1_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET1_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET1_PAGESIZES		4	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET1_PAGESZ_BITS		2	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET1_PAGESZ_LOG2_MIN		20	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET1_PAGESZ_LOG2_MAX		26	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET1_PAGESZ_LOG2_LIST	20 XCHAL_SEP 22 XCHAL_SEP 24 XCHAL_SEP 26	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET1_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET1_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET1_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET1_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET1_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET1_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET1_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET1_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 2 (group of ways 5 thru 5):  */
#define XCHAL_DTLB_SET2_WAY			5	/* index of first way in this way set */
#define XCHAL_DTLB_SET2_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET2_ENTRIES_LOG2		1	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET2_ENTRIES			2	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET2_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET2_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET2_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET2_PAGESZ_LOG2_MIN		27	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET2_PAGESZ_LOG2_MAX		27	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET2_PAGESZ_LOG2_LIST	27	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET2_ASID_CONSTMASK		0xFF	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET2_VPN_CONSTMASK		0xF0000000	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET2_PPN_CONSTMASK		0xF8000000	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET2_CA_CONSTMASK		0x0000000F	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET2_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET2_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET2_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET2_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */
/*  Constant ASID values for each entry of DTLB way set 2 (because ASID_CONSTMASK is non-zero):  */
#define XCHAL_DTLB_SET2_E0_ASID_CONST		0x01
#define XCHAL_DTLB_SET2_E1_ASID_CONST		0x01
/*  Constant VPN values for each entry of DTLB way set 2 (because VPN_CONSTMASK is non-zero):  */
#define XCHAL_DTLB_SET2_E0_VPN_CONST		0xD0000000
#define XCHAL_DTLB_SET2_E1_VPN_CONST		0xD8000000
/*  Constant PPN values for each entry of DTLB way set 2 (because PPN_CONSTMASK is non-zero):  */
#define XCHAL_DTLB_SET2_E0_PPN_CONST		0x00000000
#define XCHAL_DTLB_SET2_E1_PPN_CONST		0x00000000
/*  Constant CA values for each entry of DTLB way set 2 (because CA_CONSTMASK is non-zero):  */
#define XCHAL_DTLB_SET2_E0_CA_CONST		0x07
#define XCHAL_DTLB_SET2_E1_CA_CONST		0x03

/*  DTLB way set 3 (group of ways 6 thru 6):  */
#define XCHAL_DTLB_SET3_WAY			6	/* index of first way in this way set */
#define XCHAL_DTLB_SET3_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET3_ENTRIES_LOG2		1	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET3_ENTRIES			2	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET3_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET3_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET3_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET3_PAGESZ_LOG2_MIN		28	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET3_PAGESZ_LOG2_MAX		28	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET3_PAGESZ_LOG2_LIST	28	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET3_ASID_CONSTMASK		0xFF	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET3_VPN_CONSTMASK		0xE0000000	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET3_PPN_CONSTMASK		0xF0000000	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET3_CA_CONSTMASK		0x0000000F	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET3_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET3_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET3_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET3_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */
/*  Constant ASID values for each entry of DTLB way set 3 (because ASID_CONSTMASK is non-zero):  */
#define XCHAL_DTLB_SET3_E0_ASID_CONST		0x01
#define XCHAL_DTLB_SET3_E1_ASID_CONST		0x01
/*  Constant VPN values for each entry of DTLB way set 3 (because VPN_CONSTMASK is non-zero):  */
#define XCHAL_DTLB_SET3_E0_VPN_CONST		0xE0000000
#define XCHAL_DTLB_SET3_E1_VPN_CONST		0xF0000000
/*  Constant PPN values for each entry of DTLB way set 3 (because PPN_CONSTMASK is non-zero):  */
#define XCHAL_DTLB_SET3_E0_PPN_CONST		0xF0000000
#define XCHAL_DTLB_SET3_E1_PPN_CONST		0xF0000000
/*  Constant CA values for each entry of DTLB way set 3 (because CA_CONSTMASK is non-zero):  */
#define XCHAL_DTLB_SET3_E0_CA_CONST		0x07
#define XCHAL_DTLB_SET3_E1_CA_CONST		0x03

/*  DTLB way set 4 (group of ways 7 thru 9):  */
#define XCHAL_DTLB_SET4_WAY			7	/* index of first way in this way set */
#define XCHAL_DTLB_SET4_WAYS			3	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET4_ENTRIES_LOG2		0	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET4_ENTRIES			1	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET4_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET4_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET4_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET4_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET4_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET4_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET4_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET4_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET4_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET4_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET4_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET4_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET4_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET4_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  Indexing macros:  */
#define _XCHAL_DTLB_SET(n,_what)	XCHAL_DTLB_SET ## n ## _what
#define XCHAL_DTLB_SET(n,what)		_XCHAL_DTLB_SET(n, _ ## what )
#define _XCHAL_DTLB_SET_E(n,i,_what)	XCHAL_DTLB_SET ## n ## _E ## i ## _what
#define XCHAL_DTLB_SET_E(n,i,what)	_XCHAL_DTLB_SET_E(n,i, _ ## what )
/*
 *  Example use:  XCHAL_DTLB_SET(XCHAL_DTLB_ARF_SET0,ENTRIES)
 *	to get the value of XCHAL_DTLB_SET<n>_ENTRIES where <n> is the first auto-refill set.
 */


/*
 *  Determine whether we have a full MMU (with Page Table and Protection)
 *  usable for an MMU-based OS:
 */
#if XCHAL_HAVE_TLBS && !XCHAL_HAVE_SPANNING_WAY && XCHAL_ITLB_ARF_WAYS > 0 && XCHAL_DTLB_ARF_WAYS > 0 && XCHAL_MMU_RINGS >= 2
# define XCHAL_HAVE_PTP_MMU		1	/* have full MMU (with page table [autorefill] and protection) */
#else
# define XCHAL_HAVE_PTP_MMU		0	/* don't have full MMU */
#endif

/*
 *  For full MMUs, report kernel RAM segment and kernel I/O segment static page mappings:
 */
#if XCHAL_HAVE_PTP_MMU
#define XCHAL_KSEG_CACHED_VADDR		0xD0000000	/* virt.addr of kernel RAM cached static map */
#define XCHAL_KSEG_CACHED_PADDR		0x00000000	/* phys.addr of kseg_cached */
#define XCHAL_KSEG_CACHED_SIZE		0x08000000	/* size in bytes of kseg_cached (assumed power of 2!!!) */
#define XCHAL_KSEG_BYPASS_VADDR		0xD8000000	/* virt.addr of kernel RAM bypass (uncached) static map */
#define XCHAL_KSEG_BYPASS_PADDR		0x00000000	/* phys.addr of kseg_bypass */
#define XCHAL_KSEG_BYPASS_SIZE		0x08000000	/* size in bytes of kseg_bypass (assumed power of 2!!!) */

#define XCHAL_KIO_CACHED_VADDR		0xE0000000	/* virt.addr of kernel I/O cached static map */
#define XCHAL_KIO_CACHED_PADDR		0xF0000000	/* phys.addr of kio_cached */
#define XCHAL_KIO_CACHED_SIZE		0x10000000	/* size in bytes of kio_cached (assumed power of 2!!!) */
#define XCHAL_KIO_BYPASS_VADDR		0xF0000000	/* virt.addr of kernel I/O bypass (uncached) static map */
#define XCHAL_KIO_BYPASS_PADDR		0xF0000000	/* phys.addr of kio_bypass */
#define XCHAL_KIO_BYPASS_SIZE		0x10000000	/* size in bytes of kio_bypass (assumed power of 2!!!) */

#define XCHAL_SEG_MAPPABLE_VADDR	0x00000000	/* start of largest non-static-mapped virtual addr area */
#define XCHAL_SEG_MAPPABLE_SIZE		0xD0000000	/* size in bytes of  "  */
/* define XCHAL_SEG_MAPPABLE2_xxx if more areas present, sorted in order of descending size.  */
#endif


/*----------------------------------------------------------------------
				MISC
  ----------------------------------------------------------------------*/

#define XCHAL_NUM_WRITEBUFFER_ENTRIES	4	/* number of write buffer entries */

#define XCHAL_CORE_ID			"linux_be"	/* configuration's alphanumeric core identifier
							   (CoreID) set in the Xtensa Processor Generator */

#define XCHAL_BUILD_UNIQUE_ID		0x00003256	/* software build-unique ID (22-bit) */

/*  These definitions describe the hardware targeted by this software:  */
#define XCHAL_HW_CONFIGID0		0xC103D1FF	/* config ID reg 0 value (upper 32 of 64 bits) */
#define XCHAL_HW_CONFIGID1		0x00803256	/* config ID reg 1 value (lower 32 of 64 bits) */
#define XCHAL_CONFIGID0			XCHAL_HW_CONFIGID0	/* for backward compatibility only -- don't use! */
#define XCHAL_CONFIGID1			XCHAL_HW_CONFIGID1	/* for backward compatibility only -- don't use! */
#define XCHAL_HW_RELEASE_MAJOR		1050	/* major release of targeted hardware */
#define XCHAL_HW_RELEASE_MINOR		1	/* minor release of targeted hardware */
#define XCHAL_HW_RELEASE_NAME		"T1050.1"	/* full release name of targeted hardware */
#define XTHAL_HW_REL_T1050	1
#define XTHAL_HW_REL_T1050_1	1
#define XCHAL_HW_CONFIGID_RELIABLE	1


/*
 *  Miscellaneous special register fields:
 */


/*  DBREAKC (special register number 160):  */
#define XCHAL_DBREAKC_VALIDMASK	0xC000003F	/* bits of DBREAKC that are defined */
/*  MASK field:  */
#define XCHAL_DBREAKC_MASK_BITS 	6		/* number of bits in MASK field */
#define XCHAL_DBREAKC_MASK_NUM  	64		/* max number of possible causes (2^bits) */
#define XCHAL_DBREAKC_MASK_SHIFT	0		/* position of MASK bits in DBREAKC, starting from lsbit */
#define XCHAL_DBREAKC_MASK_MASK 	0x0000003F	/* mask of bits in MASK field of DBREAKC */
/*  LOADBREAK field:  */
#define XCHAL_DBREAKC_LOADBREAK_BITS 	1		/* number of bits in LOADBREAK field */
#define XCHAL_DBREAKC_LOADBREAK_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_DBREAKC_LOADBREAK_SHIFT	30		/* position of LOADBREAK bits in DBREAKC, starting from lsbit */
#define XCHAL_DBREAKC_LOADBREAK_MASK 	0x40000000	/* mask of bits in LOADBREAK field of DBREAKC */
/*  STOREBREAK field:  */
#define XCHAL_DBREAKC_STOREBREAK_BITS 	1		/* number of bits in STOREBREAK field */
#define XCHAL_DBREAKC_STOREBREAK_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_DBREAKC_STOREBREAK_SHIFT	31		/* position of STOREBREAK bits in DBREAKC, starting from lsbit */
#define XCHAL_DBREAKC_STOREBREAK_MASK 	0x80000000	/* mask of bits in STOREBREAK field of DBREAKC */

/*  PS (special register number 230):  */
#define XCHAL_PS_VALIDMASK	0x00070FFF	/* bits of PS that are defined */
/*  INTLEVEL field:  */
#define XCHAL_PS_INTLEVEL_BITS 	4		/* number of bits in INTLEVEL field */
#define XCHAL_PS_INTLEVEL_NUM  	16		/* max number of possible causes (2^bits) */
#define XCHAL_PS_INTLEVEL_SHIFT	0		/* position of INTLEVEL bits in PS, starting from lsbit */
#define XCHAL_PS_INTLEVEL_MASK 	0x0000000F	/* mask of bits in INTLEVEL field of PS */
/*  EXCM field:  */
#define XCHAL_PS_EXCM_BITS 	1		/* number of bits in EXCM field */
#define XCHAL_PS_EXCM_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_PS_EXCM_SHIFT	4		/* position of EXCM bits in PS, starting from lsbit */
#define XCHAL_PS_EXCM_MASK 	0x00000010	/* mask of bits in EXCM field of PS */
/*  PROGSTACK field:  */
#define XCHAL_PS_PROGSTACK_BITS 	1		/* number of bits in PROGSTACK field */
#define XCHAL_PS_PROGSTACK_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_PS_PROGSTACK_SHIFT	5		/* position of PROGSTACK bits in PS, starting from lsbit */
#define XCHAL_PS_PROGSTACK_MASK 	0x00000020	/* mask of bits in PROGSTACK field of PS */
/*  RING field:  */
#define XCHAL_PS_RING_BITS 	2		/* number of bits in RING field */
#define XCHAL_PS_RING_NUM  	4		/* max number of possible causes (2^bits) */
#define XCHAL_PS_RING_SHIFT	6		/* position of RING bits in PS, starting from lsbit */
#define XCHAL_PS_RING_MASK 	0x000000C0	/* mask of bits in RING field of PS */
/*  OWB field:  */
#define XCHAL_PS_OWB_BITS 	4		/* number of bits in OWB field */
#define XCHAL_PS_OWB_NUM  	16		/* max number of possible causes (2^bits) */
#define XCHAL_PS_OWB_SHIFT	8		/* position of OWB bits in PS, starting from lsbit */
#define XCHAL_PS_OWB_MASK 	0x00000F00	/* mask of bits in OWB field of PS */
/*  CALLINC field:  */
#define XCHAL_PS_CALLINC_BITS 	2		/* number of bits in CALLINC field */
#define XCHAL_PS_CALLINC_NUM  	4		/* max number of possible causes (2^bits) */
#define XCHAL_PS_CALLINC_SHIFT	16		/* position of CALLINC bits in PS, starting from lsbit */
#define XCHAL_PS_CALLINC_MASK 	0x00030000	/* mask of bits in CALLINC field of PS */
/*  WOE field:  */
#define XCHAL_PS_WOE_BITS 	1		/* number of bits in WOE field */
#define XCHAL_PS_WOE_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_PS_WOE_SHIFT	18		/* position of WOE bits in PS, starting from lsbit */
#define XCHAL_PS_WOE_MASK 	0x00040000	/* mask of bits in WOE field of PS */

/*  EXCCAUSE (special register number 232):  */
#define XCHAL_EXCCAUSE_VALIDMASK	0x0000003F	/* bits of EXCCAUSE that are defined */
/*  EXCCAUSE field:  */
#define XCHAL_EXCCAUSE_BITS 		6		/* number of bits in EXCCAUSE register */
#define XCHAL_EXCCAUSE_NUM  		64		/* max number of possible causes (2^bits) */
#define XCHAL_EXCCAUSE_SHIFT		0		/* position of EXCCAUSE bits in register, starting from lsbit */
#define XCHAL_EXCCAUSE_MASK 		0x0000003F	/* mask of bits in EXCCAUSE register */

/*  DEBUGCAUSE (special register number 233):  */
#define XCHAL_DEBUGCAUSE_VALIDMASK	0x0000003F	/* bits of DEBUGCAUSE that are defined */
/*  ICOUNT field:  */
#define XCHAL_DEBUGCAUSE_ICOUNT_BITS 	1		/* number of bits in ICOUNT field */
#define XCHAL_DEBUGCAUSE_ICOUNT_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_DEBUGCAUSE_ICOUNT_SHIFT	0		/* position of ICOUNT bits in DEBUGCAUSE, starting from lsbit */
#define XCHAL_DEBUGCAUSE_ICOUNT_MASK 	0x00000001	/* mask of bits in ICOUNT field of DEBUGCAUSE */
/*  IBREAK field:  */
#define XCHAL_DEBUGCAUSE_IBREAK_BITS 	1		/* number of bits in IBREAK field */
#define XCHAL_DEBUGCAUSE_IBREAK_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_DEBUGCAUSE_IBREAK_SHIFT	1		/* position of IBREAK bits in DEBUGCAUSE, starting from lsbit */
#define XCHAL_DEBUGCAUSE_IBREAK_MASK 	0x00000002	/* mask of bits in IBREAK field of DEBUGCAUSE */
/*  DBREAK field:  */
#define XCHAL_DEBUGCAUSE_DBREAK_BITS 	1		/* number of bits in DBREAK field */
#define XCHAL_DEBUGCAUSE_DBREAK_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_DEBUGCAUSE_DBREAK_SHIFT	2		/* position of DBREAK bits in DEBUGCAUSE, starting from lsbit */
#define XCHAL_DEBUGCAUSE_DBREAK_MASK 	0x00000004	/* mask of bits in DBREAK field of DEBUGCAUSE */
/*  BREAK field:  */
#define XCHAL_DEBUGCAUSE_BREAK_BITS 	1		/* number of bits in BREAK field */
#define XCHAL_DEBUGCAUSE_BREAK_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_DEBUGCAUSE_BREAK_SHIFT	3		/* position of BREAK bits in DEBUGCAUSE, starting from lsbit */
#define XCHAL_DEBUGCAUSE_BREAK_MASK 	0x00000008	/* mask of bits in BREAK field of DEBUGCAUSE */
/*  BREAKN field:  */
#define XCHAL_DEBUGCAUSE_BREAKN_BITS 	1		/* number of bits in BREAKN field */
#define XCHAL_DEBUGCAUSE_BREAKN_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_DEBUGCAUSE_BREAKN_SHIFT	4		/* position of BREAKN bits in DEBUGCAUSE, starting from lsbit */
#define XCHAL_DEBUGCAUSE_BREAKN_MASK 	0x00000010	/* mask of bits in BREAKN field of DEBUGCAUSE */
/*  DEBUGINT field:  */
#define XCHAL_DEBUGCAUSE_DEBUGINT_BITS 	1		/* number of bits in DEBUGINT field */
#define XCHAL_DEBUGCAUSE_DEBUGINT_NUM  	2		/* max number of possible causes (2^bits) */
#define XCHAL_DEBUGCAUSE_DEBUGINT_SHIFT	5		/* position of DEBUGINT bits in DEBUGCAUSE, starting from lsbit */
#define XCHAL_DEBUGCAUSE_DEBUGINT_MASK 	0x00000020	/* mask of bits in DEBUGINT field of DEBUGCAUSE */



/*----------------------------------------------------------------------
				ISA
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_DENSITY		1	/* 1 if density option configured, 0 otherwise */
#define XCHAL_HAVE_LOOPS		1	/* 1 if zero-overhead loops option configured, 0 otherwise */
/*  Misc instructions:  */
#define XCHAL_HAVE_NSA			0	/* 1 if NSA/NSAU instructions option configured, 0 otherwise */
#define XCHAL_HAVE_MINMAX		0	/* 1 if MIN/MAX instructions option configured, 0 otherwise */
#define XCHAL_HAVE_SEXT			0	/* 1 if sign-extend instruction option configured, 0 otherwise */
#define XCHAL_HAVE_CLAMPS		0	/* 1 if CLAMPS instruction option configured, 0 otherwise */
#define XCHAL_HAVE_MAC16		0	/* 1 if MAC16 option configured, 0 otherwise */
#define XCHAL_HAVE_MUL16		0	/* 1 if 16-bit integer multiply option configured, 0 otherwise */
/*#define XCHAL_HAVE_POPC		0*/	/* 1 if CRC instruction option configured, 0 otherwise */
/*#define XCHAL_HAVE_CRC		0*/	/* 1 if POPC instruction option configured, 0 otherwise */

#define XCHAL_HAVE_SPECULATION		0	/* 1 if speculation option configured, 0 otherwise */
/*#define XCHAL_HAVE_MP_SYNC		0*/	/* 1 if multiprocessor sync. option configured, 0 otherwise */
#define XCHAL_HAVE_PRID			0	/* 1 if processor ID register configured, 0 otherwise */

#define XCHAL_NUM_MISC_REGS		2	/* number of miscellaneous registers (0..4) */

/*  These relate a bit more to TIE:  */
#define XCHAL_HAVE_BOOLEANS		0	/* 1 if booleans option configured, 0 otherwise */
#define XCHAL_HAVE_MUL32		0	/* 1 if 32-bit integer multiply option configured, 0 otherwise */
#define XCHAL_HAVE_MUL32_HIGH		0	/* 1 if MUL32 option includes MULUH and MULSH, 0 otherwise */
#define XCHAL_HAVE_FP			0	/* 1 if floating point option configured, 0 otherwise */


/*----------------------------------------------------------------------
				DERIVED
  ----------------------------------------------------------------------*/

#if XCHAL_HAVE_BE
#define XCHAL_INST_ILLN			0xD60F		/* 2-byte illegal instruction, msb-first */
#define XCHAL_INST_ILLN_BYTE0		0xD6		/* 2-byte illegal instruction, 1st byte */
#define XCHAL_INST_ILLN_BYTE1		0x0F		/* 2-byte illegal instruction, 2nd byte */
#else
#define XCHAL_INST_ILLN			0xF06D		/* 2-byte illegal instruction, lsb-first */
#define XCHAL_INST_ILLN_BYTE0		0x6D		/* 2-byte illegal instruction, 1st byte */
#define XCHAL_INST_ILLN_BYTE1		0xF0		/* 2-byte illegal instruction, 2nd byte */
#endif
/*  Belongs in xtensa/hal.h:  */
#define XTHAL_INST_ILL			0x000000	/* 3-byte illegal instruction */


/*
 *  Because information as to exactly which hardware release is targeted
 *  by a given software build is not always available, compile-time HAL
 *  Hardware-Release "_AT" macros are fuzzy (return 0, 1, or XCHAL_MAYBE):
 */
#ifndef XCHAL_HW_RELEASE_MAJOR
# define XCHAL_HW_CONFIGID_RELIABLE	0
#endif
#if XCHAL_HW_CONFIGID_RELIABLE
# define XCHAL_HW_RELEASE_AT_OR_BELOW(major,minor)	(XTHAL_REL_LE( XCHAL_HW_RELEASE_MAJOR,XCHAL_HW_RELEASE_MINOR, major,minor ) ? 1 : 0)
# define XCHAL_HW_RELEASE_AT_OR_ABOVE(major,minor)	(XTHAL_REL_GE( XCHAL_HW_RELEASE_MAJOR,XCHAL_HW_RELEASE_MINOR, major,minor ) ? 1 : 0)
# define XCHAL_HW_RELEASE_AT(major,minor)		(XTHAL_REL_EQ( XCHAL_HW_RELEASE_MAJOR,XCHAL_HW_RELEASE_MINOR, major,minor ) ? 1 : 0)
# define XCHAL_HW_RELEASE_MAJOR_AT(major)		((XCHAL_HW_RELEASE_MAJOR == (major)) ? 1 : 0)
#else
# define XCHAL_HW_RELEASE_AT_OR_BELOW(major,minor)	( ((major) < 1040 && XCHAL_HAVE_XEA2) ? 0 \
							: ((major) > 1050 && XCHAL_HAVE_XEA1) ? 1 \
							: XTHAL_MAYBE )
# define XCHAL_HW_RELEASE_AT_OR_ABOVE(major,minor)	( ((major) >= 2000 && XCHAL_HAVE_XEA1) ? 0 \
							: (XTHAL_REL_LE(major,minor, 1040,0) && XCHAL_HAVE_XEA2) ? 1 \
							: XTHAL_MAYBE )
# define XCHAL_HW_RELEASE_AT(major,minor)		( (((major) < 1040 && XCHAL_HAVE_XEA2) || \
							   ((major) >= 2000 && XCHAL_HAVE_XEA1)) ? 0 : XTHAL_MAYBE)
# define XCHAL_HW_RELEASE_MAJOR_AT(major)		XCHAL_HW_RELEASE_AT(major,0)
#endif

/*
 *  Specific errata:
 */

/*
 *  Erratum T1020.H13, T1030.H7, T1040.H10, T1050.H4 (fixed in T1040.3 and T1050.1;
 *  relevant only in XEA1, kernel-vector mode, level-one interrupts and overflows enabled):
 */
#define XCHAL_MAYHAVE_ERRATUM_XEA1KWIN	(XCHAL_HAVE_XEA1 && \
					 (XCHAL_HW_RELEASE_AT_OR_BELOW(1040,2) != 0 \
					  || XCHAL_HW_RELEASE_AT(1050,0)))



#endif /*XTENSA_CONFIG_CORE_H*/

