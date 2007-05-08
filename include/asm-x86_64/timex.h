/*
 * linux/include/asm-x86_64/timex.h
 *
 * x86-64 architecture timex specifications
 */
#ifndef _ASMx8664_TIMEX_H
#define _ASMx8664_TIMEX_H

#include <asm/8253pit.h>
#include <asm/msr.h>
#include <asm/vsyscall.h>
#include <asm/hpet.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/tsc.h>
#include <linux/compiler.h>

#define CLOCK_TICK_RATE	PIT_TICK_RATE	/* Underlying HZ */

extern int read_current_timer(unsigned long *timer_value);
#define ARCH_HAS_READ_CURRENT_TIMER	1

#define USEC_PER_TICK (USEC_PER_SEC / HZ)
#define NSEC_PER_TICK (NSEC_PER_SEC / HZ)
#define FSEC_PER_TICK (FSEC_PER_SEC / HZ)

#define NS_SCALE        10 /* 2^10, carefully chosen */
#define US_SCALE        32 /* 2^32, arbitralrily chosen */

extern void mark_tsc_unstable(char *msg);
extern void set_cyc2ns_scale(unsigned long khz);
#endif
