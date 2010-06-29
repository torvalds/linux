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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

#include <linux/cache.h>

void rcu_sched_qs(int cpu);
void rcu_bh_qs(int cpu);

#ifdef CONFIG_TINY_RCU
#define __rcu_read_lock()	preempt_disable()
#define __rcu_read_unlock()	preempt_enable()
#else /* #ifdef CONFIG_TINY_RCU */
void __rcu_read_lock(void);
void __rcu_read_unlock(void);
#endif /* #else #ifdef CONFIG_TINY_RCU */
#define __rcu_read_lock_bh()	local_bh_disable()
#define __rcu_read_unlock_bh()	local_bh_enable()
extern void call_rcu_sched(struct rcu_head *head,
			   void (*func)(struct rcu_head *rcu));

#define rcu_init_sched()	do { } while (0)

extern void synchronize_sched(void);

#ifdef CONFIG_TINY_RCU

#define call_rcu		call_rcu_sched

static inline void synchronize_rcu(void)
{
	synchronize_sched();
}

static inline void synchronize_rcu_expedited(void)
{
	synchronize_sched();	/* Only one CPU, so pretty fast anyway!!! */
}

static inline void rcu_barrier(void)
{
	rcu_barrier_sched();  /* Only one CPU, so only one list of callbacks! */
}

#else /* #ifdef CONFIG_TINY_RCU */

void synchronize_rcu(void);
void rcu_barrier(void);
void synchronize_rcu_expedited(void);

#endif /* #else #ifdef CONFIG_TINY_RCU */

static inline void synchronize_rcu_bh(void)
{
	synchronize_sched();
}

static inline void synchronize_rcu_bh_expedited(void)
{
	synchronize_sched();
}

struct notifier_block;

#ifdef CONFIG_NO_HZ

extern void rcu_enter_nohz(void);
extern void rcu_exit_nohz(void);

#else /* #ifdef CONFIG_NO_HZ */

static inline void rcu_enter_nohz(void)
{
}

static inline void rcu_exit_nohz(void)
{
}

#endif /* #else #ifdef CONFIG_NO_HZ */

#ifdef CONFIG_TINY_RCU

static inline void rcu_preempt_note_context_switch(void)
{
}

static inline void exit_rcu(void)
{
}

static inline int rcu_needs_cpu(int cpu)
{
	return 0;
}

static inline int rcu_preempt_depth(void)
{
	return 0;
}

#else /* #ifdef CONFIG_TINY_RCU */

void rcu_preempt_note_context_switch(void);
extern void exit_rcu(void);
int rcu_preempt_needs_cpu(void);

static inline int rcu_needs_cpu(int cpu)
{
	return rcu_preempt_needs_cpu();
}

/*
 * Defined as macro as it is a very low level header
 * included from areas that don't even know about current
 * FIXME: combine with include/linux/rcutree.h into rcupdate.h.
 */
#define rcu_preempt_depth() (current->rcu_read_lock_nesting)

#endif /* #else #ifdef CONFIG_TINY_RCU */

static inline void rcu_note_context_switch(int cpu)
{
	rcu_sched_qs(cpu);
	rcu_preempt_note_context_switch();
}

extern void rcu_check_callbacks(int cpu, int user);

/*
 * Return the number of grace periods.
 */
static inline long rcu_batches_completed(void)
{
	return 0;
}

/*
 * Return the number of bottom-half grace periods.
 */
static inline long rcu_batches_completed_bh(void)
{
	return 0;
}

static inline void rcu_force_quiescent_state(void)
{
}

static inline void rcu_bh_force_quiescent_state(void)
{
}

static inline void rcu_sched_force_quiescent_state(void)
{
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

extern int rcu_scheduler_active __read_mostly;
extern void rcu_scheduler_starting(void);

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

static inline void rcu_scheduler_starting(void)
{
}

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

#endif /* __LINUX_RCUTINY_H */
