/* $Id: timer.h,v 1.3 2000/05/09 17:40:15 davem Exp $
 * timer.h: System timer definitions for sun5.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_TIMER_H
#define _SPARC64_TIMER_H

#include <linux/types.h>

/* How timers work:
 *
 * On uniprocessors we just use counter zero for the system wide
 * ticker, this performs thread scheduling, clock book keeping,
 * and runs timer based events.  Previously we used the Ultra
 * %tick interrupt for this purpose.
 *
 * On multiprocessors we pick one cpu as the master level 10 tick
 * processor.  Here this counter zero tick handles clock book
 * keeping and timer events only.  Each Ultra has it's level
 * 14 %tick interrupt set to fire off as well, even the master
 * tick cpu runs this locally.  This ticker performs thread
 * scheduling, system/user tick counting for the current thread,
 * and also profiling if enabled.
 */

#include <linux/config.h>

/* Two timers, traditionally steered to PIL's 10 and 14 respectively.
 * But since INO packets are used on sun5, we could use any PIL level
 * we like, however for now we use the normal ones.
 *
 * The 'reg' and 'interrupts' properties for these live in nodes named
 * 'counter-timer'.  The first of three 'reg' properties describe where
 * the sun5_timer registers are.  The other two I have no idea. (XXX)
 */
struct sun5_timer {
	u64	count0;
	u64	limit0;
	u64	count1;
	u64	limit1;
};

#define SUN5_LIMIT_ENABLE	0x80000000
#define SUN5_LIMIT_TOZERO	0x40000000
#define SUN5_LIMIT_ZRESTART	0x20000000
#define SUN5_LIMIT_CMASK	0x1fffffff

/* Given a HZ value, set the limit register to so that the timer IRQ
 * gets delivered that often.
 */
#define SUN5_HZ_TO_LIMIT(__hz)  (1000000/(__hz))

struct sparc64_tick_ops {
	void (*init_tick)(unsigned long);
	unsigned long (*get_tick)(void);
	unsigned long (*get_compare)(void);
	unsigned long (*add_tick)(unsigned long, unsigned long);
	unsigned long (*add_compare)(unsigned long);
	unsigned long softint_mask;
};

extern struct sparc64_tick_ops *tick_ops;

#ifdef CONFIG_SMP
extern unsigned long timer_tick_offset;
struct pt_regs;
extern void timer_tick_interrupt(struct pt_regs *);
#endif

extern unsigned long sparc64_get_clock_tick(unsigned int cpu);

#endif /* _SPARC64_TIMER_H */
