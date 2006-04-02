/*
 * linux/include/asm-sparc64/timex.h
 *
 * sparc64 architecture timex specifications
 */
#ifndef _ASMsparc64_TIMEX_H
#define _ASMsparc64_TIMEX_H

#include <asm/timer.h>

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */

/* Getting on the cycle counter on sparc64. */
typedef unsigned long cycles_t;
#define get_cycles()	tick_ops->get_tick()

#define ARCH_HAS_READ_CURRENT_TIMER	1
#define read_current_timer(timer_val_p) 	\
({	*timer_val_p = tick_ops->get_tick();	\
	0;					\
})

#endif
