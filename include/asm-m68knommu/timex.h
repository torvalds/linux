/*
 * linux/include/asm-m68knommu/timex.h
 *
 * m68knommu architecture timex specifications
 */
#ifndef _ASM_M68KNOMMU_TIMEX_H
#define _ASM_M68KNOMMU_TIMEX_H

#ifdef CONFIG_COLDFIRE
#include <asm/coldfire.h>
#define CLOCK_TICK_RATE	MCF_CLK
#else
#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */
#endif

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return 0;
}

#endif
