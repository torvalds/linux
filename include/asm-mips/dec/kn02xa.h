/*
 * Hardware info common to DECstation 5000/1xx systems (otherwise
 * known as 3min or kn02ba) and Personal DECstations 5000/xx ones
 * (otherwise known as maxine or kn02ca).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995,1996 by Paul M. Antoine, some code and definitions
 * are by courtesy of Chris Fraser.
 * Copyright (C) 2000, 2002, 2003  Maciej W. Rozycki
 *
 * These are addresses which have to be known early in the boot process.
 * For other addresses refer to tc.h, ioasic_addrs.h and friends.
 */
#ifndef __ASM_MIPS_DEC_KN02XA_H
#define __ASM_MIPS_DEC_KN02XA_H

#include <asm/addrspace.h>
#include <asm/dec/ioasic_addrs.h>

#define KN02XA_SLOT_BASE	KSEG1ADDR(0x1c000000)

/*
 * Some port addresses...
 */
#define KN02XA_IOASIC_BASE    (KN02XA_SLOT_BASE + IOASIC_IOCTL)	/* I/O ASIC */
#define KN02XA_RTC_BASE		(KN02XA_SLOT_BASE + IOASIC_TOY)	/* RTC */


/*
 * Memory control ASIC registers.
 */
#define KN02XA_MER	KSEG1ADDR(0x0c400000)	/* memory error register */
#define KN02XA_MSR	KSEG1ADDR(0x0c800000)	/* memory size register */

/*
 * CPU control ASIC registers.
 */
#define KN02XA_MEM_CONF	KSEG1ADDR(0x0e000000)	/* write timeout config */
#define KN02XA_EAR	KSEG1ADDR(0x0e000004)	/* error address register */
#define KN02XA_BOOT0	KSEG1ADDR(0x0e000008)	/* boot 0 register */
#define KN02XA_MEM_INTR	KSEG1ADDR(0x0e00000c)	/* write err IRQ stat & ack */

/*
 * Memory Error Register bits, common definitions.
 * The rest is defined in system-specific headers.
 */
#define KN02XA_MER_RES_28	(0xf<<28)	/* unused */
#define KN02XA_MER_RES_17	(0x3ff<<17)	/* unused */
#define KN02XA_MER_PAGERR	(1<<16)		/* 2k page boundary error */
#define KN02XA_MER_TRANSERR	(1<<15)		/* transfer length error */
#define KN02XA_MER_PARDIS	(1<<14)		/* parity error disable */
#define KN02XA_MER_RES_12	(0x3<<12)	/* unused */
#define KN02XA_MER_BYTERR	(0xf<<8)	/* byte lane error bitmask */
#define KN02XA_MER_RES_0	(0xff<<0)	/* unused */

/*
 * Memory Size Register bits, common definitions.
 * The rest is defined in system-specific headers.
 */
#define KN02XA_MSR_RES_27	(0x1f<<27)	/* unused */
#define KN02XA_MSR_RES_14	(0x7<<14)	/* unused */
#define KN02XA_MSR_SIZE		(1<<13)		/* 16M/4M stride */
#define KN02XA_MSR_RES_0	(0x1fff<<0)	/* unused */

/*
 * Error Address Register bits.
 */
#define KN02XA_EAR_RES_29	(0x7<<29)	/* unused */
#define KN02XA_EAR_ADDRESS	(0x7ffffff<<2)	/* address involved */
#define KN02XA_EAR_RES_0	(0x3<<0)	/* unused */

#endif /* __ASM_MIPS_DEC_KN02XA_H */
