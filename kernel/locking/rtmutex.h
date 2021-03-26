/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RT-Mutexes: blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * This file contains macros used solely by rtmutex.c.
 * Non-debug version.
 */

#define debug_rt_mutex_init_waiter(w)			do { } while (0)
#define debug_rt_mutex_free_waiter(w)			do { } while (0)
#define debug_rt_mutex_proxy_unlock(l)			do { } while (0)
#define debug_rt_mutex_unlock(l)			do { } while (0)
#define debug_rt_mutex_init(m, n, k)			do { } while (0)

static inline bool debug_rt_mutex_detect_deadlock(struct rt_mutex_waiter *w,
						  enum rtmutex_chainwalk walk)
{
	return walk == RT_MUTEX_FULL_CHAINWALK;
}
