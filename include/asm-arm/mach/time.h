/*
 * linux/include/asm-arm/mach/time.h
 *
 * Copyright (C) 2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_MACH_TIME_H
#define __ASM_ARM_MACH_TIME_H

#include <linux/sysdev.h>

/*
 * This is our kernel timer structure.
 *
 * - init
 *   Initialise the kernels jiffy timer source, claim interrupt
 *   using setup_irq.  This is called early on during initialisation
 *   while interrupts are still disabled on the local CPU.
 * - suspend
 *   Suspend the kernel jiffy timer source, if necessary.  This
 *   is called with interrupts disabled, after all normal devices
 *   have been suspended.  If no action is required, set this to
 *   NULL.
 * - resume
 *   Resume the kernel jiffy timer source, if necessary.  This
 *   is called with interrupts disabled before any normal devices
 *   are resumed.  If no action is required, set this to NULL.
 * - offset
 *   Return the timer offset in microseconds since the last timer
 *   interrupt.  Note: this must take account of any unprocessed
 *   timer interrupt which may be pending.
 */
struct sys_timer {
	struct sys_device	dev;
	void			(*init)(void);
	void			(*suspend)(void);
	void			(*resume)(void);
#ifndef CONFIG_GENERIC_TIME
	unsigned long		(*offset)(void);
#endif

#ifdef CONFIG_NO_IDLE_HZ
	struct dyn_tick_timer	*dyn_tick;
#endif
};

#ifdef CONFIG_NO_IDLE_HZ

#define DYN_TICK_ENABLED	(1 << 1)

struct dyn_tick_timer {
	spinlock_t	lock;
	unsigned int	state;			/* Current state */
	int		(*enable)(void);	/* Enables dynamic tick */
	int		(*disable)(void);	/* Disables dynamic tick */
	void		(*reprogram)(unsigned long); /* Reprograms the timer */
	int		(*handler)(int, void *);
};

void timer_dyn_reprogram(void);
#else
#define timer_dyn_reprogram()	do { } while (0)
#endif

extern struct sys_timer *system_timer;
extern void timer_tick(void);

/*
 * Kernel time keeping support.
 */
struct timespec;
extern int (*set_rtc)(void);
extern void save_time_delta(struct timespec *delta, struct timespec *rtc);
extern void restore_time_delta(struct timespec *delta, struct timespec *rtc);

#endif
