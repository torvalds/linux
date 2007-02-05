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

/* Inline functions for clock register access. */
static inline int set_clock(__u64 time)
{
	int cc;

	asm volatile(
		"   sck   0(%2)\n"
		"   ipm   %0\n"
		"   srl   %0,28\n"
		: "=d" (cc) : "m" (time), "a" (&time) : "cc");
	return cc;
}

static inline int store_clock(__u64 *time)
{
	int cc;

	asm volatile(
		"   stck  0(%2)\n"
		"   ipm   %0\n"
		"   srl   %0,28\n"
		: "=d" (cc), "=m" (*time) : "a" (time) : "cc");
	return cc;
}

static inline void set_clock_comparator(__u64 time)
{
	asm volatile("sckc 0(%1)" : : "m" (time), "a" (&time));
}

static inline void store_clock_comparator(__u64 *time)
{
	asm volatile("stckc 0(%1)" : "=m" (*time) : "a" (time));
}

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

static inline void get_clock_extended(void *dest)
{
	typedef struct { unsigned long long clk[2]; } __clock_t;

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)
	asm volatile("stcke %0" : "=Q" (*((__clock_t *)dest)) : : "cc");
#else /* __GNUC__ */
	asm volatile("stcke 0(%1)" : "=m" (*((__clock_t *)dest))
				   : "a" ((__clock_t *)dest) : "cc");
#endif /* __GNUC__ */
}

static inline cycles_t get_cycles(void)
{
	return (cycles_t) get_clock() >> 2;
}

int get_sync_clock(unsigned long long *clock);
void init_cpu_timer(void);

#endif
