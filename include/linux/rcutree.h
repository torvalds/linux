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

extern void rcu_init(void);
extern void rcu_note_context_switch(int cpu);
extern int rcu_needs_cpu(int cpu);
extern void rcu_cpu_stall_reset(void);

#ifdef CONFIG_TREE_PREEMPT_RCU

extern void exit_rcu(void);

#else /* #ifdef CONFIG_TREE_PREEMPT_RCU */

static inline void exit_rcu(void)
{
}

#endif /* #else #ifdef CONFIG_TREE_PREEMPT_RCU */

extern void synchronize_rcu_bh(void);
extern void synchronize_rcu_expedited(void);

static inline void synchronize_rcu_bh_expedited(void)
{
	synchronize_sched_expedited();
}

extern void rcu_barrier(void);

extern long rcu_batches_completed(void);
extern long rcu_batches_completed_bh(void);
extern long rcu_batches_completed_sched(void);
extern void rcu_force_quiescent_state(void);
extern void rcu_bh_force_quiescent_state(void);
extern void rcu_sched_force_quiescent_state(void);

/* A context switch is a grace period for RCU-sched and RCU-bh. */
static inline int rcu_blocking_is_gp(void)
{
	return num_online_cpus() == 1;
}

extern void rcu_scheduler_starting(void);
extern int rcu_scheduler_active __read_mostly;

#endif /* __LINUX_RCUTREE_H */
