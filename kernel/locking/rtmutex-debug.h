/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RT-Mutexes: blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * This file contains macros used solely by rtmutex.c. Debug version.
 */

extern void debug_rt_mutex_init_waiter(struct rt_mutex_waiter *waiter);
extern void debug_rt_mutex_free_waiter(struct rt_mutex_waiter *waiter);
extern void debug_rt_mutex_init(struct rt_mutex *lock, const char *name, struct lock_class_key *key);
extern void debug_rt_mutex_lock(struct rt_mutex *lock);
extern void debug_rt_mutex_unlock(struct rt_mutex *lock);
extern void debug_rt_mutex_proxy_lock(struct rt_mutex *lock,
				      struct task_struct *powner);
extern void debug_rt_mutex_proxy_unlock(struct rt_mutex *lock);

static inline bool debug_rt_mutex_detect_deadlock(struct rt_mutex_waiter *waiter,
						  enum rtmutex_chainwalk walk)
{
	return (waiter != NULL);
}
