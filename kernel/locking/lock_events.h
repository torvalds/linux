/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors: Waiman Long <longman@redhat.com>
 */

#ifndef __LOCKING_LOCK_EVENTS_H
#define __LOCKING_LOCK_EVENTS_H

enum lock_events {

#include "lock_events_list.h"

	lockevent_num,	/* Total number of lock event counts */
	LOCKEVENT_reset_cnts = lockevent_num,
};

#ifdef CONFIG_LOCK_EVENT_COUNTS
/*
 * Per-cpu counters
 */
DECLARE_PER_CPU(unsigned long, lockevents[lockevent_num]);

/*
 * The purpose of the lock event counting subsystem is to provide a low
 * overhead way to record the number of specific locking events by using
 * percpu counters. It is the percpu sum that matters, not specifically
 * how many of them happens in each cpu.
 *
 * It is possible that the same percpu counter may be modified in both
 * the process and interrupt contexts. For architectures that perform
 * percpu operation with multiple instructions, it is possible to lose
 * count if a process context percpu update is interrupted in the middle
 * and the same counter is updated in the interrupt context. Therefore,
 * the generated percpu sum may not be precise. The error, if any, should
 * be small and insignificant.
 *
 * For those architectures that do multi-instruction percpu operation,
 * preemption in the middle and moving the task to another cpu may cause
 * a larger error in the count. Again, this will be few and far between.
 * Given the imprecise nature of the count and the possibility of resetting
 * the count and doing the measurement again, this is not really a big
 * problem.
 *
 * To get a better picture of what is happening under the hood, it is
 * suggested that a few measurements should be taken with the counts
 * reset in between to stamp out outliner because of these possible
 * error conditions.
 *
 * To minimize overhead, we use __this_cpu_*() in all cases except when
 * CONFIG_DEBUG_PREEMPT is defined. In this particular case, this_cpu_*()
 * will be used to avoid the appearance of unwanted BUG messages.
 */
#ifdef CONFIG_DEBUG_PREEMPT
#define lockevent_percpu_inc(x)		this_cpu_inc(x)
#define lockevent_percpu_add(x, v)	this_cpu_add(x, v)
#else
#define lockevent_percpu_inc(x)		__this_cpu_inc(x)
#define lockevent_percpu_add(x, v)	__this_cpu_add(x, v)
#endif

/*
 * Increment the PV qspinlock statistical counters
 */
static inline void __lockevent_inc(enum lock_events event, bool cond)
{
	if (cond)
		lockevent_percpu_inc(lockevents[event]);
}

#define lockevent_inc(ev)	  __lockevent_inc(LOCKEVENT_ ##ev, true)
#define lockevent_cond_inc(ev, c) __lockevent_inc(LOCKEVENT_ ##ev, c)

static inline void __lockevent_add(enum lock_events event, int inc)
{
	lockevent_percpu_add(lockevents[event], inc);
}

#define lockevent_add(ev, c)	__lockevent_add(LOCKEVENT_ ##ev, c)

#else  /* CONFIG_LOCK_EVENT_COUNTS */

#define lockevent_inc(ev)
#define lockevent_add(ev, c)
#define lockevent_cond_inc(ev, c)

#endif /* CONFIG_LOCK_EVENT_COUNTS */
#endif /* __LOCKING_LOCK_EVENTS_H */
