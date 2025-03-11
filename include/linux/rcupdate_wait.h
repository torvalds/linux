/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_RCUPDATE_WAIT_H
#define _LINUX_SCHED_RCUPDATE_WAIT_H

/*
 * RCU synchronization types and methods:
 */

#include <linux/rcupdate.h>
#include <linux/completion.h>
#include <linux/sched.h>

/*
 * Structure allowing asynchronous waiting on RCU.
 */
struct rcu_synchronize {
	struct rcu_head head;
	struct completion completion;
};
void wakeme_after_rcu(struct rcu_head *head);

void __wait_rcu_gp(bool checktiny, unsigned int state, int n, call_rcu_func_t *crcu_array,
		   struct rcu_synchronize *rs_array);

#define _wait_rcu_gp(checktiny, state, ...) \
do {												\
	call_rcu_func_t __crcu_array[] = { __VA_ARGS__ };					\
	struct rcu_synchronize __rs_array[ARRAY_SIZE(__crcu_array)];				\
	__wait_rcu_gp(checktiny, state, ARRAY_SIZE(__crcu_array), __crcu_array, __rs_array);	\
} while (0)

#define wait_rcu_gp(...) _wait_rcu_gp(false, TASK_UNINTERRUPTIBLE, __VA_ARGS__)
#define wait_rcu_gp_state(state, ...) _wait_rcu_gp(false, state, __VA_ARGS__)

/**
 * synchronize_rcu_mult - Wait concurrently for multiple grace periods
 * @...: List of call_rcu() functions for different grace periods to wait on
 *
 * This macro waits concurrently for multiple types of RCU grace periods.
 * For example, synchronize_rcu_mult(call_rcu, call_rcu_tasks) would wait
 * on concurrent RCU and RCU-tasks grace periods.  Waiting on a given SRCU
 * domain requires you to write a wrapper function for that SRCU domain's
 * call_srcu() function, with this wrapper supplying the pointer to the
 * corresponding srcu_struct.
 *
 * Note that call_rcu_hurry() should be used instead of call_rcu()
 * because in kernels built with CONFIG_RCU_LAZY=y the delay between the
 * invocation of call_rcu() and that of the corresponding RCU callback
 * can be multiple seconds.
 *
 * The first argument tells Tiny RCU's _wait_rcu_gp() not to
 * bother waiting for RCU.  The reason for this is because anywhere
 * synchronize_rcu_mult() can be called is automatically already a full
 * grace period.
 */
#define synchronize_rcu_mult(...) \
	_wait_rcu_gp(IS_ENABLED(CONFIG_TINY_RCU), TASK_UNINTERRUPTIBLE, __VA_ARGS__)

static inline void cond_resched_rcu(void)
{
#if defined(CONFIG_DEBUG_ATOMIC_SLEEP) || !defined(CONFIG_PREEMPT_RCU)
	rcu_read_unlock();
	cond_resched();
	rcu_read_lock();
#endif
}

// Has the current task blocked within its current RCU read-side
// critical section?
static inline bool has_rcu_reader_blocked(void)
{
#ifdef CONFIG_PREEMPT_RCU
	return !list_empty(&current->rcu_node_entry);
#else
	return false;
#endif
}

#endif /* _LINUX_SCHED_RCUPDATE_WAIT_H */
