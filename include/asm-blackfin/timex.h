/* blackfin architecture timex specifications: Lineo Inc. 2001
 *
 * Based on: include/asm-m68knommu/timex.h
 */

#ifndef _ASMBLACKFIN_TIMEX_H
#define _ASMBLACKFIN_TIMEX_H

#define CLOCK_TICK_RATE	1000000	/* Underlying HZ */

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return 0;
}

#endif
