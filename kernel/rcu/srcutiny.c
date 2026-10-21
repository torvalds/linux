// SPDX-License-Identifier: GPL-2.0+
/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion,
 *	tiny version for non-preemptible single-CPU use.
 *
 * Copyright (C) IBM Corporation, 2017
 *
 * Author: Paul McKenney <paulmck@linux.ibm.com>
 */

#include <linux/export.h>
#include <linux/irq_work.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/preempt.h>
#include <linux/rcupdate_wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/srcu.h>

#include <linux/rcu_node_tree.h>
#include "rcu_segcblist.h"
#include "rcu.h"

#ifndef CONFIG_TREE_RCU
int rcu_scheduler_active __read_mostly;
#else // #ifndef CONFIG_TREE_RCU
extern int rcu_scheduler_active;
#endif // #else // #ifndef CONFIG_TREE_RCU
static LIST_HEAD(srcu_boot_list);
static bool srcu_init_done;

static void __srcu_defer_drain(struct srcu_struct *ssp);

static int init_srcu_struct_fields(struct srcu_struct *ssp)
{
	ssp->srcu_lock_nesting[0] = 0;
	ssp->srcu_lock_nesting[1] = 0;
	init_swait_queue_head(&ssp->srcu_wq);
	ssp->srcu_cb_head = NULL;
	ssp->srcu_cb_tail = &ssp->srcu_cb_head;
	ssp->srcu_gp_running = false;
	ssp->srcu_gp_waiting = false;
	ssp->srcu_atomic_gp_flag = 0;
	ssp->srcu_idx = 0;
	ssp->srcu_idx_max = 0;
	INIT_WORK(&ssp->srcu_work, srcu_drive_gp);
	INIT_LIST_HEAD(&ssp->srcu_work.entry);
	init_irq_work(&ssp->srcu_irq_work, srcu_tiny_irq_work);
	init_llist_head(&ssp->defer_cbs);
	ssp->defer_iw = IRQ_WORK_INIT_HARD(srcu_defer_drain);
	return 0;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int init_srcu_struct_lockdep(struct srcu_struct *ssp, const char *name,
		     struct lock_class_key *key)
{
	/* Don't re-initialize a lock while it is held. */
	debug_check_no_locks_freed((void *)ssp, sizeof(*ssp));
	lockdep_init_map(&ssp->dep_map, name, key, 0);
	return init_srcu_struct_fields(ssp);
}
EXPORT_SYMBOL_GPL(init_srcu_struct_lockdep);

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/*
 * init_srcu_struct_generic - initialize a sleep-RCU structure
 * @ssp: structure to initialize.
 *
 * Must invoke this on a given srcu_struct before passing that srcu_struct
 * to any other function.  Each srcu_struct represents a separate domain
 * of SRCU protection.
 */
int init_srcu_struct_generic(struct srcu_struct *ssp)
{
	return init_srcu_struct_fields(ssp);
}
EXPORT_SYMBOL_GPL(init_srcu_struct_generic);

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/*
 * cleanup_srcu_struct - deconstruct a sleep-RCU structure
 * @ssp: structure to clean up.
 *
 * Must invoke this after you are finished using a given srcu_struct that
 * was initialized via init_srcu_struct(), else you leak memory.
 */
void cleanup_srcu_struct(struct srcu_struct *ssp)
{
	WARN_ON(srcu_readers_active(ssp));
	/*
	 * Re-issue any deferred callbacks, then wait out ->defer_iw before it is
	 * freed.  Skipped entirely with CONFIG_RCU_DEFER=n: irq_work_sync() ends
	 * in an unconditional synchronize_rcu() wherever
	 * arch_irq_work_has_interrupt() is false, which is every !SMP target.
	 */
	if (IS_ENABLED(CONFIG_RCU_DEFER)) {
		__srcu_defer_drain(ssp);
		irq_work_sync(&ssp->defer_iw);
	}
	irq_work_sync(&ssp->srcu_irq_work);
	flush_work(&ssp->srcu_work);
	WARN_ON(ssp->srcu_gp_running);
	WARN_ON(ssp->srcu_gp_waiting);
	WARN_ON(ssp->srcu_cb_head);
	WARN_ON(&ssp->srcu_cb_head != ssp->srcu_cb_tail);
	WARN_ON(ssp->srcu_idx != ssp->srcu_idx_max);
	WARN_ON(ssp->srcu_idx & 0x1);
}
EXPORT_SYMBOL_GPL(cleanup_srcu_struct);

/*
 * Removes the count for the old reader from the appropriate element of
 * the srcu_struct.
 */
void __srcu_read_unlock(struct srcu_struct *ssp, int idx)
{
	int newval;

	preempt_disable();  // Needed for PREEMPT_LAZY
	newval = READ_ONCE(ssp->srcu_lock_nesting[idx]) - 1;
	WRITE_ONCE(ssp->srcu_lock_nesting[idx], newval);
	preempt_enable();
	if (!newval && READ_ONCE(ssp->srcu_gp_waiting) && in_task() && !irqs_disabled())
		swake_up_one(&ssp->srcu_wq);
}
EXPORT_SYMBOL_GPL(__srcu_read_unlock);

/*
 * Workqueue handler to drive one grace period and invoke any callbacks
 * that become ready as a result.  Single-CPU operation and preemption
 * disabling mean that we get away with murder on synchronization.  ;-)
 */
void srcu_drive_gp(struct work_struct *wp)
{
	int idx;
	struct rcu_head *lh;
	struct rcu_head *rhp;
	struct srcu_struct *ssp;

	ssp = container_of(wp, struct srcu_struct, srcu_work);
	preempt_disable();  // Needed for PREEMPT_LAZY
	if (ssp->srcu_gp_running || ULONG_CMP_GE(ssp->srcu_idx, READ_ONCE(ssp->srcu_idx_max))) {
		preempt_enable();
		return; /* Already running or nothing to do. */
	}

	/* Remove recently arrived callbacks and wait for readers. */
	WRITE_ONCE(ssp->srcu_gp_running, true);
	local_irq_disable();
	lh = ssp->srcu_cb_head;
	ssp->srcu_cb_head = NULL;
	ssp->srcu_cb_tail = &ssp->srcu_cb_head;
	local_irq_enable();
	idx = (ssp->srcu_idx & 0x2) / 2;
	WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 1);
	WRITE_ONCE(ssp->srcu_gp_waiting, true);  /* srcu_read_unlock() wakes! */
	preempt_enable();
	if (IS_ENABLED(CONFIG_PREEMPTION))
		synchronize_rcu(); // Needed for RCU Tasks Trace to imply RCU grace period
	do {
		// Deadlock issues prevent __srcu_read_unlock() from
		// doing an unconditional wakeup, so polling is required.
		swait_event_timeout_exclusive(ssp->srcu_wq,
					      !READ_ONCE(ssp->srcu_lock_nesting[idx]), HZ / 10);
	} while (READ_ONCE(ssp->srcu_lock_nesting[idx]));
	preempt_disable();  // Needed for PREEMPT_LAZY
	WRITE_ONCE(ssp->srcu_gp_waiting, false); /* srcu_read_unlock() cheap. */
	WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 1);
	preempt_enable();

	/* Invoke the callbacks we removed above. */
	while (lh) {
		rhp = lh;
		lh = lh->next;
		debug_rcu_head_callback(rhp);
		local_bh_disable();
		rhp->func(rhp);
		local_bh_enable();
	}

	/*
	 * Enable rescheduling, and if there are more callbacks,
	 * reschedule ourselves.  This can race with a call_srcu()
	 * at interrupt level, but the ->srcu_gp_running checks will
	 * straighten that out.
	 */
	preempt_disable();  // Needed for PREEMPT_LAZY
	WRITE_ONCE(ssp->srcu_gp_running, false);
	idx = ULONG_CMP_LT(ssp->srcu_idx, READ_ONCE(ssp->srcu_idx_max));
	preempt_enable();
	if (idx)
		schedule_work(&ssp->srcu_work);
}
EXPORT_SYMBOL_GPL(srcu_drive_gp);

/*
 * Use an irq_work to defer schedule_work() to avoid acquiring the workqueue
 * pool->lock while the caller might hold scheduler locks, causing lockdep
 * splats due to workqueue_init() doing a wakeup.
 */
void srcu_tiny_irq_work(struct irq_work *irq_work)
{
	struct srcu_struct *ssp;

	ssp = container_of(irq_work, struct srcu_struct, srcu_irq_work);
	schedule_work(&ssp->srcu_work);
}
EXPORT_SYMBOL_GPL(srcu_tiny_irq_work);

static void srcu_gp_start_if_needed(struct srcu_struct *ssp)
{
	unsigned long cookie;

	lockdep_assert_preemption_disabled(); // Needed for PREEMPT_LAZY
	cookie = get_state_synchronize_srcu(ssp);
	if (ULONG_CMP_GE(READ_ONCE(ssp->srcu_idx_max), cookie)) {
		return;
	}
	WRITE_ONCE(ssp->srcu_idx_max, cookie);
	if (!READ_ONCE(ssp->srcu_gp_running)) {
		if (likely(srcu_init_done))
			irq_work_queue(&ssp->srcu_irq_work);
		else if (list_empty(&ssp->srcu_work.entry))
			list_add(&ssp->srcu_work.entry, &srcu_boot_list);
	}
}

/*
 * Also called by __srcu_defer_drain() to re-issue a deferred callback, so it
 * must not re-check the deferral condition.
 */
static void srcu_do_enqueue(struct srcu_struct *ssp, struct rcu_head *rhp,
			    rcu_callback_t func)
{
	unsigned long flags;

	rhp->func = func;
	rhp->next = NULL;
	preempt_disable();  // Needed for PREEMPT_LAZY
	local_irq_save(flags);
	*ssp->srcu_cb_tail = rhp;
	ssp->srcu_cb_tail = &rhp->next;
	local_irq_restore(flags);
	srcu_gp_start_if_needed(ssp);
	preempt_enable();
}

/*
 * Set only by the irq_work drain, the one drain its own re-issue can re-feed;
 * a callback staged during a direct drain is taken by ->defer_iw afterwards.
 * Global rather than per-srcu_struct: a re-entrant call_srcu(B) inside a drain
 * of A raises B's own ->defer_iw, whose drain can stage back onto A.
 */
static bool srcu_defer_draining;

static void __srcu_defer_drain(struct srcu_struct *ssp)
{
	struct llist_node *node, *next;
	unsigned long flags;

	if (!IS_ENABLED(CONFIG_RCU_DEFER))
		return;

	/* Re-issued newest-first; nothing depends on call_srcu() ordering. */
	local_irq_save(flags);
	llist_for_each_safe(node, next, llist_del_all(&ssp->defer_cbs)) {
		struct rcu_head *rhp = (struct rcu_head *)node;

		srcu_do_enqueue(ssp, rhp, rhp->func);
	}
	local_irq_restore(flags);
}

/* Only the irq_work drain can be re-fed by its own re-issue; see Tree SRCU. */
void srcu_defer_drain(struct irq_work *iw)
{
	struct srcu_struct *ssp = container_of(iw, struct srcu_struct, defer_iw);

	WRITE_ONCE(srcu_defer_draining, true);
	__srcu_defer_drain(ssp);
	WRITE_ONCE(srcu_defer_draining, false);
}
EXPORT_SYMBOL_GPL(srcu_defer_drain);

void call_srcu(struct srcu_struct *ssp, struct rcu_head *rhp,
	       rcu_callback_t func)
{
	if (should_rcu_defer()) {
		/* A re-entrant call_srcu() during the drain would livelock it. */
		if (READ_ONCE(srcu_defer_draining) && !in_nmi()) {
			WARN_ONCE(IS_ENABLED(CONFIG_PROVE_RCU),
				  "call_srcu() re-entered during callback drain; leaking callback\n");
			return;
		}
		rhp->func = func;
		if (llist_add((struct llist_node *)rhp, &ssp->defer_cbs))
			irq_work_queue(&ssp->defer_iw);
		return;
	}

	/*
	 * Only reachable from an NMI when deferral is off: before the scheduler
	 * is up, or with CONFIG_RCU_DEFER=n.  The enqueue can then race.
	 */
	WARN_ON_ONCE(IS_ENABLED(CONFIG_PROVE_RCU) && in_nmi());

	srcu_do_enqueue(ssp, rhp, func);
}
EXPORT_SYMBOL_GPL(call_srcu);

/*
 * synchronize_srcu - wait for prior SRCU read-side critical-section completion
 */
void synchronize_srcu(struct srcu_struct *ssp)
{
	struct rcu_synchronize rs;

	srcu_lock_sync(&ssp->dep_map);

	RCU_LOCKDEP_WARN(lockdep_is_held(ssp) ||
			lock_is_held(&rcu_bh_lock_map) ||
			lock_is_held(&rcu_lock_map) ||
			lock_is_held(&rcu_sched_lock_map),
			"Illegal synchronize_srcu() in same-type SRCU (or in RCU) read-side critical section");

	if (rcu_scheduler_active == RCU_SCHEDULER_INACTIVE)
		return;

	might_sleep();
	init_rcu_head_on_stack(&rs.head);
	init_completion(&rs.completion);
	call_srcu(ssp, &rs.head, wakeme_after_rcu);
	wait_for_completion(&rs.completion);
	destroy_rcu_head_on_stack(&rs.head);
}
EXPORT_SYMBOL_GPL(synchronize_srcu);

/*
 * synchronize_srcu_atomic - spinning grace period for atomic-reader domains
 * @ssp: srcu_struct with which to synchronize.
 *
 * On !SMP this cannot spin: a reader observed mid-section is preempted
 * or interrupted-out, and can only finish if we yield the CPU. But it
 * is also never needed: an atomic-flavor reader (preemption disabled)
 * cannot be observed mid-section from process context on the sole CPU.
 * So a reader observed here has broken the atomic-domain promise, and
 * the only correct wait for it is a real grace period.
 *
 * (Actual kernel-doc header is in Tree SRCU.)
 */
void synchronize_srcu_atomic(struct srcu_struct *ssp)
{
	int idx;
	bool ret;
	unsigned long srcu_state = get_state_synchronize_srcu(ssp);

	srcu_lock_sync(&ssp->dep_map);

	RCU_LOCKDEP_WARN(lockdep_is_held(ssp),
			 "Illegal synchronize_srcu_atomic() in same-type SRCU read-side critical section");

	if (rcu_scheduler_active == RCU_SCHEDULER_INACTIVE)
		return;

	if (IS_ENABLED(CONFIG_PREEMPTION))
		synchronize_rcu(); // Needed for RCU Tasks Trace to imply RCU grace period.
				   // And in Tiny RCU, it is near zero cost and doesn't block.

	// Usually, there will be no readers.
	preempt_disable();  // Guard against lazy preemption and some other grace period.
	ret = !READ_ONCE(ssp->srcu_lock_nesting[0]) && !READ_ONCE(ssp->srcu_lock_nesting[1]);
	if (ret) {
		WRITE_ONCE(ssp->srcu_idx_max, ssp->srcu_idx + 2);
		WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 2);
		preempt_enable();
		return;
	}

	// Because readers disable preemption, we should never get here.
	// However, a splat and some spinning is usually preferable to
	// memory corruption due to a too-short grace period.  There is
	// the possibility that this will hang if the preempted reader is
	// not looked upon favorably by the scheduler, but this is still
	// preferable to memory corruption.
	WARN_ON_ONCE(1);

	// Wait to drive a grace period or for someone else to do it
	// for us while we are lazily preempted.
	while (ssp->srcu_atomic_gp_flag) {
		if (poll_state_synchronize_srcu(ssp, srcu_state)) {
			preempt_enable();
			return;
		}
		preempt_enable();
		cpu_relax();
		cond_resched_tasks_rcu_qs();
		preempt_disable();
	}
	ssp->srcu_atomic_gp_flag = 1;  // Acquire SRCU grace-period "lock".
	preempt_enable();

	// We get here if a reader has been lazily preempted.
	// First, wait for old readers, which are quite unlikely.
	WRITE_ONCE(ssp->srcu_idx_max, get_state_synchronize_srcu(ssp));
	idx = !(((READ_ONCE(ssp->srcu_idx) + 1) & 0x2) >> 1);
	while (READ_ONCE(ssp->srcu_lock_nesting[idx])) {
		cond_resched_tasks_rcu_qs();
		cpu_relax();
	}

	// Next, flip the index and wait for the other group of readers.
	WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 1);
	idx = !idx;
	while (READ_ONCE(ssp->srcu_lock_nesting[idx])) {
		cond_resched_tasks_rcu_qs();
		cpu_relax();
	}

	// Finally, flip the index again for poll_state_synchronize_srcu().
	WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 1);
	WARN_ON_ONCE(!poll_state_synchronize_srcu(ssp, srcu_state));
	ssp->srcu_atomic_gp_flag = 0; // Release SRCU grace-period "lock".
}
EXPORT_SYMBOL_GPL(synchronize_srcu_atomic);

/* Register any deferred callbacks, then wait for all in-flight ones. */
void srcu_barrier(struct srcu_struct *ssp)
{
	__srcu_defer_drain(ssp);
	synchronize_srcu(ssp);
}
EXPORT_SYMBOL_GPL(srcu_barrier);

/*
 * get_state_synchronize_srcu - Provide an end-of-grace-period cookie
 */
unsigned long get_state_synchronize_srcu(struct srcu_struct *ssp)
{
	unsigned long ret;

	barrier();
	ret = (READ_ONCE(ssp->srcu_idx) + 3) & ~0x1;
	barrier();
	return ret;
}
EXPORT_SYMBOL_GPL(get_state_synchronize_srcu);

/*
 * start_poll_synchronize_srcu - Provide cookie and start grace period
 *
 * The difference between this and get_state_synchronize_srcu() is that
 * this function ensures that the poll_state_synchronize_srcu() will
 * eventually return the value true.
 *
 * This function cannot be used with atomic SRCU, which only has
 * atomic grace periods.  Doing so will silently corrupt internal
 * SRCU state.  Tree SRCU has appropriate checking with splats,
 * so please test with CONFIG_SMP=y as well as CONFIG_SMP=n.
 */
unsigned long start_poll_synchronize_srcu(struct srcu_struct *ssp)
{
	unsigned long ret;

	preempt_disable();  // Needed for PREEMPT_LAZY
	ret = get_state_synchronize_srcu(ssp);
	srcu_gp_start_if_needed(ssp);
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL_GPL(start_poll_synchronize_srcu);

/*
 * poll_state_synchronize_srcu - Has cookie's grace period ended?
 */
bool poll_state_synchronize_srcu(struct srcu_struct *ssp, unsigned long cookie)
{
	unsigned long cur_s = READ_ONCE(ssp->srcu_idx);

	barrier();
	return cookie == SRCU_GET_STATE_COMPLETED ||
	       ULONG_CMP_GE(cur_s, cookie) || ULONG_CMP_LT(cur_s, cookie - 3);
}
EXPORT_SYMBOL_GPL(poll_state_synchronize_srcu);

#ifndef CONFIG_TREE_RCU
/* Lockdep diagnostics.  */
void __init rcu_scheduler_starting(void)
{
	rcu_scheduler_active = RCU_SCHEDULER_RUNNING;
}
#endif // #ifndef CONFIG_TREE_RCU

/*
 * Queue work for srcu_struct structures with early boot callbacks.
 * The work won't actually execute until the workqueue initialization
 * phase that takes place after the scheduler starts.
 */
void __init srcu_init(void)
{
	struct srcu_struct *ssp;

	srcu_init_done = true;
	while (!list_empty(&srcu_boot_list)) {
		ssp = list_first_entry(&srcu_boot_list,
				      struct srcu_struct, srcu_work.entry);
		list_del_init(&ssp->srcu_work.entry);
		schedule_work(&ssp->srcu_work);
	}
}
