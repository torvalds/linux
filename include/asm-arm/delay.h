/*
 * Copyright (C) 1995-2004 Russell King
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */
#ifndef __ASM_ARM_DELAY_H
#define __ASM_ARM_DELAY_H

extern void __delay(int loops);

/*
 * This function intentionally does not exist; if you see references to
 * it, it means that you're calling udelay() with an out of range value.
 *
 * With currently imposed limits, this means that we support a max delay
 * of 2000us and 671 bogomips
 */
extern void __bad_udelay(void);

/*
 * division by multiplication: you don't have to worry about
 * loss of precision.
 *
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */
extern void __udelay(unsigned long usecs);
extern void __const_udelay(unsigned long);

#define MAX_UDELAY_MS 2

#define udelay(n)						\
	(__builtin_constant_p(n) ?				\
	  ((n) > (MAX_UDELAY_MS * 1000) ? __bad_udelay() :	\
			__const_udelay((n) * 0x68dbul)) :	\
	  __udelay(n))

#endif /* defined(_ARM_DELAY_H) */

