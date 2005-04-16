#ifndef __ASM_ARM_DELAY_H
#define __ASM_ARM_DELAY_H

/*
 * Copyright (C) 1995 Russell King
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */

extern void __delay(int loops);

/*
 * division by multiplication: you don't have to worry about
 * loss of precision.
 *
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 *
 * FIXME - lets improve it then...
 */
extern void udelay(unsigned long usecs);

static inline unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	return a * b / c;
}

	

#endif /* defined(_ARM_DELAY_H) */

