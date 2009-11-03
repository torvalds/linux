/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
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
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *	   Paul E. McKenney <paulmck@linux.vnet.ibm.com> Hierarchical algorithm
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *	Documentation/RCU
 */

#ifndef __LINUX_RCUTREE_H
#define __LINUX_RCUTREE_H

struct notifier_block;

extern void rcu_sched_qs(int cpu);
extern void rcu_bh_qs(int cpu);
extern int rcu_cpu_notify(struct notifier_block *self,
			  unsigned long action, void *hcpu);
extern int rcu_needs_cpu(int cpu);
extern int rcu_expedited_torture_stats(char *page);

#ifdef CONFIG_TREE_PREEMPT_RCU

extern void __rcu_read_lock(void);
extern void __rcu_read_unlock(void);
extern void exit_rcu(void);

#else /* #ifdef CONFIG_TREE_PREEMPT_RCU */

static inline void __rcu_read_lock(void)
{
	preempt_disable();
}

static inline void __rcu_read_unlock(void)
{
	preempt_enable();
}

#define __synchronize_sched() synchronize_rcu()

static inline void exit_rcu(void)
{
}

#endif /* #else #ifdef CONFIG_TREE_PREEMPT_RCU */

static inline void __rcu_read_lock_bh(void)
{
	local_bh_disable();
}
static inline void __rcu_read_unlock_bh(void)
{
	local_bh_enable();
}

extern void call_rcu_sched(struct rcu_head *head,
			   void (*func)(struct rcu_head *rcu));

static inline void synchronize_rcu_expedited(void)
{
	synchronize_sched_expedited();
}

static inline void synchronize_rcu_bh_expedited(void)
{
	synchronize_sched_expedited();
}

extern void __rcu_init(void);
extern void rcu_check_callbacks(int cpu, int user);

extern long rcu_batches_completed(void);
extern long rcu_batches_completed_bh(void);
extern long rcu_batches_completed_sched(void);

#ifdef CONFIG_NO_HZ
void rcu_enter_nohz(void);
void rcu_exit_nohz(void);
#else /* CONFIG_NO_HZ */
static inline void rcu_enter_nohz(void)
{
}
static inline void rcu_exit_nohz(void)
{
}
#endif /* CONFIG_NO_HZ */

/* A context switch is a grace period for RCU-sched and RCU-bh. */
static inline int rcu_blocking_is_gp(void)
{
	return num_online_cpus() == 1;
}

#endif /* __LINUX_RCUTREE_H */
