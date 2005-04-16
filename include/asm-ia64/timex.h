#ifndef _ASM_IA64_TIMEX_H
#define _ASM_IA64_TIMEX_H

/*
 * Copyright (C) 1998-2001, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
/*
 * 2001/01/18 davidm	Removed CLOCK_TICK_RATE.  It makes no sense on IA-64.
 *			Also removed cacheflush_time as it's entirely unused.
 */

#include <asm/intrinsics.h>
#include <asm/processor.h>

typedef unsigned long cycles_t;

/*
 * For performance reasons, we don't want to define CLOCK_TICK_TRATE as
 * local_cpu_data->itc_rate.  Fortunately, we don't have to, either: according to George
 * Anzinger, 1/CLOCK_TICK_RATE is taken as the resolution of the timer clock.  The time
 * calculation assumes that you will use enough of these so that your tick size <= 1/HZ.
 * If the calculation shows that your CLOCK_TICK_RATE can not supply exactly 1/HZ ticks,
 * the actual value is calculated and used to update the wall clock each jiffie.  Setting
 * the CLOCK_TICK_RATE to x*HZ insures that the calculation will find no errors.  Hence we
 * pick a multiple of HZ which gives us a (totally virtual) CLOCK_TICK_RATE of about
 * 100MHz.
 */
#define CLOCK_TICK_RATE		(HZ * 100000UL)

static inline cycles_t
get_cycles (void)
{
	cycles_t ret;

	ret = ia64_getreg(_IA64_REG_AR_ITC);
	return ret;
}

#endif /* _ASM_IA64_TIMEX_H */
