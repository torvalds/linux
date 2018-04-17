/*
 * Read-Copy Update mechanism for mutual exclusion, the Bloatwatch edition.
 *
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2008
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		Documentation/RCU
 */
#ifndef __LINUX_TINY_H
#define __LINUX_TINY_H

#include <linux/ktime.h>

struct rcu_dynticks;
static inline int rcu_dynticks_snap(struct rcu_dynticks *rdtp)
{
	return 0;
}

/* Never flag non-existent other CPUs! */
static inline bool rcu_eqs_special_set(int cpu) { return false; }

static inline unsigned long get_state_synchronize_rcu(void)
{
	return 0;
}

static inline void cond_synchronize_rcu(unsigned long oldstate)
{
	might_sleep();
}

static inline unsigned long get_state_synchronize_sched(void)
{
	return 0;
}

static inline void cond_synchronize_sched(unsigned long oldstate)
{
	might_sleep();
}

extern void rcu_barrier_bh(void);
extern void rcu_barrier_sched(void);

static inline void synchronize_rcu_expedited(void)
{
	synchronize_sched();	/* Only one CPU, so pretty fast anyway!!! */
}

static inline void rcu_barrier(void)
{
	rcu_barrier_sched();  /* Only one CPU, so only one list of callbacks! */
}

static inline void synchronize_rcu_bh(void)
{
	synchronize_sched();
}

static inline void synchronize_rcu_bh_expedited(void)
{
	synchronize_sched();
}

static inline void synchronize_sched_expedited(void)
{
	synchronize_sched();
}

static inline void kfree_call_rcu(struct rcu_head *head,
				  rcu_callback_t func)
{
	call_rcu(head, func);
}

#define rcu_note_context_switch(preempt) \
	do { \
		rcu_sched_qs(); \
		rcu_note_voluntary_context_switch_lite(current); \
	} while (0)

static inline int rcu_needs_cpu(u64 basemono, u64 *nextevt)
{
	*nextevt = KTIME_MAX;
	return 0;
}

/*
 * Take advantage of the fact that there is only one CPU, which
 * allows us to ignore virtualization-based context switches.
 */
static inline void rcu_virt_note_context_switch(int cpu) { }
static inline void rcu_cpu_stall_reset(void) { }
static inline void rcu_idle_enter(void) { }
static inline void rcu_idle_exit(void) { }
static inline void rcu_irq_enter(void) { }
static inline void rcu_irq_exit_irqson(void) { }
static inline void rcu_irq_enter_irqson(void) { }
static inline void rcu_irq_exit(void) { }
static inline void exit_rcu(void) { }
#ifdef CONFIG_SRCU
void rcu_scheduler_starting(void);
#else /* #ifndef CONFIG_SRCU */
static inline void rcu_scheduler_starting(void) { }
#endif /* #else #ifndef CONFIG_SRCU */
static inline void rcu_end_inkernel_boot(void) { }
static inline bool rcu_is_watching(void) { return true; }

/* Avoid RCU read-side critical sections leaking across. */
static inline void rcu_all_qs(void) { barrier(); }

/* RCUtree hotplug events */
#define rcutree_prepare_cpu      NULL
#define rcutree_online_cpu       NULL
#define rcutree_offline_cpu      NULL
#define rcutree_dead_cpu         NULL
#define rcutree_dying_cpu        NULL

#endif /* __LINUX_RCUTINY_H */
