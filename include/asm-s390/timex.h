/*
 *  include/asm-s390/timex.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 *  Derived from "include/asm-i386/timex.h"
 *    Copyright (C) 1992, Linus Torvalds
 */

#ifndef _ASM_S390_TIMEX_H
#define _ASM_S390_TIMEX_H

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */

typedef unsigned long long cycles_t;

static inline unsigned long long get_clock (void)
{
	unsigned long long clk;

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)
	asm volatile("stck %0" : "=Q" (clk) : : "cc");
#else /* __GNUC__ */
	asm volatile("stck 0(%1)" : "=m" (clk) : "a" (&clk) : "cc");
#endif /* __GNUC__ */
	return clk;
}

static inline cycles_t get_cycles(void)
{
	return (cycles_t) get_clock() >> 2;
}

#endif
