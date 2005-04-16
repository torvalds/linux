/* $Id: delay.h,v 1.11 2001/01/01 01:46:15 davem Exp $
 * delay.h: Linux delay routines on the Sparc.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu).
 */

#ifndef __SPARC_DELAY_H
#define __SPARC_DELAY_H

#include <linux/config.h>
#include <asm/cpudata.h>

extern __inline__ void __delay(unsigned long loops)
{
	__asm__ __volatile__("cmp %0, 0\n\t"
			     "1: bne 1b\n\t"
			     "subcc %0, 1, %0\n" :
			     "=&r" (loops) :
			     "0" (loops) :
			     "cc");
}

/* This is too messy with inline asm on the Sparc. */
extern void __udelay(unsigned long usecs, unsigned long lpj);
extern void __ndelay(unsigned long nsecs, unsigned long lpj);

#ifdef CONFIG_SMP
#define __udelay_val	cpu_data(smp_processor_id()).udelay_val
#else /* SMP */
#define __udelay_val	loops_per_jiffy
#endif /* SMP */
#define udelay(__usecs)	__udelay(__usecs, __udelay_val)
#define ndelay(__nsecs)	__ndelay(__nsecs, __udelay_val)

#endif /* defined(__SPARC_DELAY_H) */
