/*
 * Read-Copy Update definitions shared among RCU implementations.
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
 * Copyright IBM Corporation, 2011
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

#include <trace/events/rcu.h>
#ifdef CONFIG_RCU_TRACE
#define RCU_TRACE(stmt) stmt
#else /* #ifdef CONFIG_RCU_TRACE */
#define RCU_TRACE(stmt)
#endif /* #else #ifdef CONFIG_RCU_TRACE */

/*
 * Process-level increment to ->dynticks_nesting field.  This allows for
 * architectures that use half-interrupts and half-exceptions from
 * process context.
 *
 * DYNTICK_TASK_NEST_MASK defines a field of width DYNTICK_TASK_NEST_WIDTH
 * that counts the number of process-based reasons why RCU cannot
 * consider the corresponding CPU to be idle, and DYNTICK_TASK_NEST_VALUE
 * is the value used to increment or decrement this field.
 *
 * The rest of the bits could in principle be used to count interrupts,
 * but this would mean that a negative-one value in the interrupt
 * field could incorrectly zero out the DYNTICK_TASK_NEST_MASK field.
 * We therefore provide a two-bit guard field defined by DYNTICK_TASK_MASK
 * that is set to DYNTICK_TASK_FLAG upon initial exit from idle.
 * The DYNTICK_TASK_EXIT_IDLE value is thus the combined value used upon
 * initial exit from idle.
 */
#define DYNTICK_TASK_NEST_WIDTH 7
#define DYNTICK_TASK_NEST_VALUE ((LLONG_MAX >> DYNTICK_TASK_NEST_WIDTH) + 1)
#define DYNTICK_TASK_NEST_MASK  (LLONG_MAX - DYNTICK_TASK_NEST_VALUE + 1)
#define DYNTICK_TASK_FLAG	   ((DYNTICK_TASK_NEST_VALUE / 8) * 2)
#define DYNTICK_TASK_MASK	   ((DYNTICK_TASK_NEST_VALUE / 8) * 3)
#define DYNTICK_TASK_EXIT_IDLE	   (DYNTICK_TASK_NEST_VALUE + \
				    DYNTICK_TASK_FLAG)

/*
 * debug_rcu_head_queue()/debug_rcu_head_unqueue() are used internally
 * by call_rcu() and rcu callback execution, and are therefore not part of the
 * RCU API. Leaving in rcupdate.h because they are used by all RCU flavors.
 */

#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
# define STATE_RCU_HEAD_READY	0
# define STATE_RCU_HEAD_QUEUED	1

extern struct debug_obj_descr rcuhead_debug_descr;

static inline int debug_rcu_head_queue(struct rcu_head *head)
{
	int r1;

	r1 = debug_object_activate(head, &rcuhead_debug_descr);
	debug_object_active_state(head, &rcuhead_debug_descr,
				  STATE_RCU_HEAD_READY,
				  STATE_RCU_HEAD_QUEUED);
	return r1;
}

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
	debug_object_active_state(head, &rcuhead_debug_descr,
				  STATE_RCU_HEAD_QUEUED,
				  STATE_RCU_HEAD_READY);
	debug_object_deactivate(head, &rcuhead_debug_descr);
}
#else	/* !CONFIG_DEBUG_OBJECTS_RCU_HEAD */
static inline int debug_rcu_head_queue(struct rcu_head *head)
{
	return 0;
}

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
}
#endif	/* #else !CONFIG_DEBUG_OBJECTS_RCU_HEAD */

void kfree(const void *);

/*
 * Reclaim the specified callback, either by invoking it (non-lazy case)
 * or freeing it directly (lazy case).  Return true if lazy, false otherwise.
 */
static inline bool __rcu_reclaim(const char *rn, struct rcu_head *head)
{
	unsigned long offset = (unsigned long)head->func;

	rcu_lock_acquire(&rcu_callback_map);
	if (__is_kfree_rcu_offset(offset)) {
		RCU_TRACE(trace_rcu_invoke_kfree_callback(rn, head, offset));
		kfree((void *)head - offset);
		rcu_lock_release(&rcu_callback_map);
		return true;
	} else {
		RCU_TRACE(trace_rcu_invoke_callback(rn, head));
		head->func(head);
		rcu_lock_release(&rcu_callback_map);
		return false;
	}
}

#ifdef CONFIG_RCU_STALL_COMMON

extern int rcu_cpu_stall_suppress;
int rcu_jiffies_till_stall_check(void);

#endif /* #ifdef CONFIG_RCU_STALL_COMMON */

/*
 * Strings used in tracepoints need to be exported via the
 * tracing system such that tools like perf and trace-cmd can
 * translate the string address pointers to actual text.
 */
#define TPS(x)  tracepoint_string(x)

void rcu_early_boot_tests(void);

#endif /* __LINUX_RCU_H */
