/*
 * include/asm-xtensa/timex.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_TIMEX_H
#define _XTENSA_TIMEX_H

#ifdef __KERNEL__

#include <asm/processor.h>
#include <linux/stringify.h>

#if XCHAL_INT_LEVEL(XCHAL_TIMER0_INTERRUPT) == 1
# define LINUX_TIMER     0
#elif XCHAL_INT_LEVEL(XCHAL_TIMER1_INTERRUPT) == 1
# define LINUX_TIMER     1
#elif XCHAL_INT_LEVEL(XCHAL_TIMER2_INTERRUPT) == 1
# define LINUX_TIMER     2
#else
# error "Bad timer number for Linux configurations!"
#endif

#define LINUX_TIMER_INT         XCHAL_TIMER_INTERRUPT(LINUX_TIMER)
#define LINUX_TIMER_MASK        (1L << LINUX_TIMER_INT)

#define CLOCK_TICK_RATE 	1193180	/* (everyone is using this value) */
#define CLOCK_TICK_FACTOR       20 /* Factor of both 10^6 and CLOCK_TICK_RATE */
#define FINETUNE ((((((long)LATCH * HZ - CLOCK_TICK_RATE) << SHIFT_HZ) * \
	(1000000/CLOCK_TICK_FACTOR) / (CLOCK_TICK_RATE/CLOCK_TICK_FACTOR)) \
		<< (SHIFT_SCALE-SHIFT_HZ)) / HZ)

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
extern unsigned long ccount_per_jiffy;
extern unsigned long ccount_nsec;
#define CCOUNT_PER_JIFFY ccount_per_jiffy
#define CCOUNT_NSEC ccount_nsec
#else
#define CCOUNT_PER_JIFFY (CONFIG_XTENSA_CPU_CLOCK*(1000000UL/HZ))
#define CCOUNT_NSEC (1000000000UL / CONFIG_XTENSA_CPU_CLOCK)
#endif


typedef unsigned long long cycles_t;

/*
 * Only used for SMP.
 */

extern cycles_t cacheflush_time;

#define get_cycles()	(0)


/*
 * Register access.
 */

#define WSR_CCOUNT(r)	  __asm__("wsr %0,"__stringify(CCOUNT) :: "a" (r))
#define RSR_CCOUNT(r)	  __asm__("rsr %0,"__stringify(CCOUNT) : "=a" (r))
#define WSR_CCOMPARE(x,r) __asm__("wsr %0,"__stringify(CCOMPARE_0)"+"__stringify(x) :: "a"(r))
#define RSR_CCOMPARE(x,r) __asm__("rsr %0,"__stringify(CCOMPARE_0)"+"__stringify(x) : "=a"(r))

static inline unsigned long get_ccount (void)
{
	unsigned long ccount;
	RSR_CCOUNT(ccount);
	return ccount;
}

static inline void set_ccount (unsigned long ccount)
{
	WSR_CCOUNT(ccount);
}

static inline unsigned long get_linux_timer (void)
{
	unsigned ccompare;
	RSR_CCOMPARE(LINUX_TIMER, ccompare);
	return ccompare;
}

static inline void set_linux_timer (unsigned long ccompare)
{
	WSR_CCOMPARE(LINUX_TIMER, ccompare);
}

#endif	/* __KERNEL__ */
#endif	/* _XTENSA_TIMEX_H */
