/*
 * linux/include/asm-i386/timex.h
 *
 * i386 architecture timex specifications
 */
#ifndef _ASMi386_TIMEX_H
#define _ASMi386_TIMEX_H

#include <asm/processor.h>
#include <asm/tsc.h>

#ifdef CONFIG_X86_ELAN
#  define CLOCK_TICK_RATE 1189200 /* AMD Elan has different frequency! */
#else
#  define CLOCK_TICK_RATE 1193182 /* Underlying HZ */
#endif


extern int read_current_timer(unsigned long *timer_value);
#define ARCH_HAS_READ_CURRENT_TIMER	1

#endif
