/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf Electronics
 * Copyright (C) 1995 - 2000, 01, 03 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_DELAY_H
#define _ASM_DELAY_H

#include <linux/config.h>
#include <linux/param.h>

#include <asm/compiler.h>

extern unsigned long loops_per_jiffy;

static inline void __delay(unsigned long loops)
{
	if (sizeof(long) == 4)
		__asm__ __volatile__ (
		".set\tnoreorder\n"
		"1:\tbnez\t%0,1b\n\t"
		"subu\t%0,1\n\t"
		".set\treorder"
		: "=r" (loops)
		: "0" (loops));
	else if (sizeof(long) == 8)
		__asm__ __volatile__ (
		".set\tnoreorder\n"
		"1:\tbnez\t%0,1b\n\t"
		"dsubu\t%0,1\n\t"
		".set\treorder"
		:"=r" (loops)
		:"0" (loops));
}


/*
 * Division by multiplication: you don't have to worry about
 * loss of precision.
 *
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */

static inline void __udelay(unsigned long usecs, unsigned long lpj)
{
	unsigned long lo;

	/*
	 * The common rates of 1000 and 128 are rounded wrongly by the
	 * catchall case for 64-bit.  Excessive precission?  Probably ...
	 */
#if defined(CONFIG_MIPS64) && (HZ == 128)
	usecs *= 0x0008637bd05af6c7UL;		/* 2**64 / (1000000 / HZ) */
#elif defined(CONFIG_MIPS64) && (HZ == 1000)
	usecs *= 0x004189374BC6A7f0UL;		/* 2**64 / (1000000 / HZ) */
#elif defined(CONFIG_MIPS64)
	usecs *= (0x8000000000000000UL / (500000 / HZ));
#else /* 32-bit junk follows here */
	usecs *= (unsigned long) (((0x8000000000000000ULL / (500000 / HZ)) +
	                           0x80000000ULL) >> 32);
#endif

	if (sizeof(long) == 4)
		__asm__("multu\t%2, %3"
		: "=h" (usecs), "=l" (lo)
		: "r" (usecs), "r" (lpj)
		: GCC_REG_ACCUM);
	else if (sizeof(long) == 8)
		__asm__("dmultu\t%2, %3"
		: "=h" (usecs), "=l" (lo)
		: "r" (usecs), "r" (lpj)
		: GCC_REG_ACCUM);

	__delay(usecs);
}

#ifdef CONFIG_SMP
#define __udelay_val cpu_data[smp_processor_id()].udelay_val
#else
#define __udelay_val loops_per_jiffy
#endif

#define udelay(usecs) __udelay((usecs),__udelay_val)

#endif /* _ASM_DELAY_H */
