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
 * For detailed explanation of the Preemptible Read-Copy Update mechanism see -
 * 		 http://lwn.net/Articles/253651/
 */

#ifndef __LINUX_RCUPREEMPT_TRACE_H
#define __LINUX_RCUPREEMPT_TRACE_H

#include <linux/types.h>
#include <linux/kernel.h>

#include <asm/atomic.h>

/*
 * PREEMPT_RCU data structures.
 */

struct rcupreempt_trace {
	long		next_length;
	long		next_add;
	long		wait_length;
	long		wait_add;
	long		done_length;
	long		done_add;
	long		done_remove;
	atomic_t	done_invoked;
	long		rcu_check_callbacks;
	atomic_t	rcu_try_flip_1;
	atomic_t	rcu_try_flip_e1;
	long		rcu_try_flip_i1;
	long		rcu_try_flip_ie1;
	long		rcu_try_flip_g1;
	long		rcu_try_flip_a1;
	long		rcu_try_flip_ae1;
	long		rcu_try_flip_a2;
	long		rcu_try_flip_z1;
	long		rcu_try_flip_ze1;
	long		rcu_try_flip_z2;
	long		rcu_try_flip_m1;
	long		rcu_try_flip_me1;
	long		rcu_try_flip_m2;
};

#ifdef CONFIG_RCU_TRACE
#define RCU_TRACE(fn, arg) 	fn(arg);
#else
#define RCU_TRACE(fn, arg)
#endif

extern void rcupreempt_trace_move2done(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_move2wait(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_e1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_i1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_ie1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_g1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_a1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_ae1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_a2(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_z1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_ze1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_z2(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_m1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_me1(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_try_flip_m2(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_check_callbacks(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_done_remove(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_invoke(struct rcupreempt_trace *trace);
extern void rcupreempt_trace_next_add(struct rcupreempt_trace *trace);

#endif /* __LINUX_RCUPREEMPT_TRACE_H */
