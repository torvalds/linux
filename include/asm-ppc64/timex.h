/*
 * linux/include/asm-ppc/timex.h
 *
 * PPC64 architecture timex specifications
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASMPPC64_TIMEX_H
#define _ASMPPC64_TIMEX_H

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	cycles_t ret;

	__asm__ __volatile__("mftb %0" : "=r" (ret) : );
	return ret;
}

#endif
