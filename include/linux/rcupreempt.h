/*
 * Read-Copy Update mechanism for mutual exclusion (RT implementation)
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
 * Copyright (C) IBM Corporation, 2006
 *
 * Author:  Paul McKenney <paulmck@us.ibm.com>
 *
 * Based on the original work by Paul McKenney <paul.mckenney@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU
 *
 */

#ifndef __LINUX_RCUPREEMPT_H
#define __LINUX_RCUPREEMPT_H

#ifdef __KERNEL__

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>

#define rcu_qsctr_inc(cpu)
#define rcu_bh_qsctr_inc(cpu)
#define call_rcu_bh(head, rcu) call_rcu(head, rcu)

extern void __rcu_read_lock(void)	__acquires(RCU);
extern void __rcu_read_unlock(void)	__releases(RCU);
extern int rcu_pending(int cpu);
extern int rcu_needs_cpu(int cpu);

#define __rcu_read_lock_bh()	{ rcu_read_lock(); local_bh_disable(); }
#define __rcu_read_unlock_bh()	{ local_bh_enable(); rcu_read_unlock(); }

extern void __synchronize_sched(void);

extern void __rcu_init(void);
extern void rcu_check_callbacks(int cpu, int user);
extern void rcu_restart_cpu(int cpu);
extern long rcu_batches_completed(void);

/*
 * Return the number of RCU batches processed thus far. Useful for debug
 * and statistic. The _bh variant is identifcal to straight RCU
 */
static inline long rcu_batches_completed_bh(void)
{
	return rcu_batches_completed();
}

#ifdef CONFIG_RCU_TRACE
struct rcupreempt_trace;
extern long *rcupreempt_flipctr(int cpu);
extern long rcupreempt_data_completed(void);
extern int rcupreempt_flip_flag(int cpu);
extern int rcupreempt_mb_flag(int cpu);
extern char *rcupreempt_try_flip_state_name(void);
extern struct rcupreempt_trace *rcupreempt_trace_cpu(int cpu);
#endif

struct softirq_action;

#ifdef CONFIG_NO_HZ
DECLARE_PER_CPU(long, dynticks_progress_counter);

static inline void rcu_enter_nohz(void)
{
	__get_cpu_var(dynticks_progress_counter)++;
	WARN_ON(__get_cpu_var(dynticks_progress_counter) & 0x1);
	mb();
}

static inline void rcu_exit_nohz(void)
{
	mb();
	__get_cpu_var(dynticks_progress_counter)++;
	WARN_ON(!(__get_cpu_var(dynticks_progress_counter) & 0x1));
}

#else /* CONFIG_NO_HZ */
#define rcu_enter_nohz()	do { } while (0)
#define rcu_exit_nohz()		do { } while (0)
#endif /* CONFIG_NO_HZ */

#endif /* __KERNEL__ */
#endif /* __LINUX_RCUPREEMPT_H */
