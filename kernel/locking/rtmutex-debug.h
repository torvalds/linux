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
extern void debug_rt_mutex_init(struct rt_mutex *lock, const char *name);
extern void debug_rt_mutex_lock(struct rt_mutex *lock);
extern void debug_rt_mutex_unlock(struct rt_mutex *lock);
extern void debug_rt_mutex_proxy_lock(struct rt_mutex *lock,
				      struct task_struct *powner);
extern void debug_rt_mutex_proxy_unlock(struct rt_mutex *lock);
extern void debug_rt_mutex_deadlock(enum rtmutex_chainwalk chwalk,
				    struct rt_mutex_waiter *waiter,
				    struct rt_mutex *lock);
extern void debug_rt_mutex_print_deadlock(struct rt_mutex_waiter *waiter);
# define debug_rt_mutex_reset_waiter(w)			\
	do { (w)->deadlock_lock = NULL; } while (0)

static inline bool debug_rt_mutex_detect_deadlock(struct rt_mutex_waiter *waiter,
						  enum rtmutex_chainwalk walk)
{
	return (waiter != NULL);
}

static inline void rt_mutex_print_deadlock(struct rt_mutex_waiter *w)
{
	debug_rt_mutex_print_deadlock(w);
}
