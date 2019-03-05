/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion,
 *	tiny version for non-preemptible single-CPU use.
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
 * Copyright (C) IBM Corporation, 2017
 *
 * Author: Paul McKenney <paulmck@us.ibm.com>
 */

#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/preempt.h>
#include <linux/rcupdate_wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/srcu.h>

#include <linux/rcu_node_tree.h>
#include "rcu_segcblist.h"
#include "rcu.h"

int rcu_scheduler_active __read_mostly;
static LIST_HEAD(srcu_boot_list);
static bool srcu_init_done;

static int init_srcu_struct_fields(struct srcu_struct *ssp)
{
	ssp->srcu_lock_nesting[0] = 0;
	ssp->srcu_lock_nesting[1] = 0;
	init_swait_queue_head(&ssp->srcu_wq);
	ssp->srcu_cb_head = NULL;
	ssp->srcu_cb_tail = &ssp->srcu_cb_head;
	ssp->srcu_gp_running = false;
	ssp->srcu_gp_waiting = false;
	ssp->srcu_idx = 0;
	INIT_WORK(&ssp->srcu_work, srcu_drive_gp);
	INIT_LIST_HEAD(&ssp->srcu_work.entry);
	return 0;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *ssp, const char *name,
		       struct lock_class_key *key)
{
	/* Don't re-initialize a lock while it is held. */
	debug_check_no_locks_freed((void *)ssp, sizeof(*ssp));
	lockdep_init_map(&ssp->dep_map, name, key, 0);
	return init_srcu_struct_fields(ssp);
}
EXPORT_SYMBOL_GPL(__init_srcu_struct);

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/*
 * init_srcu_struct - initialize a sleep-RCU structure
 * @ssp: structure to initialize.
 *
 * Must invoke this on a given srcu_struct before passing that srcu_struct
 * to any other function.  Each srcu_struct represents a separate domain
 * of SRCU protection.
 */
int init_srcu_struct(struct srcu_struct *ssp)
{
	return init_srcu_struct_fields(ssp);
}
EXPORT_SYMBOL_GPL(init_srcu_struct);

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/*
 * cleanup_srcu_struct - deconstruct a sleep-RCU structure
 * @ssp: structure to clean up.
 *
 * Must invoke this after you are finished using a given srcu_struct that
 * was initialized via init_srcu_struct(), else you leak memory.
 */
void _cleanup_srcu_struct(struct srcu_struct *ssp, bool quiesced)
{
	WARN_ON(ssp->srcu_lock_nesting[0] || ssp->srcu_lock_nesting[1]);
	if (quiesced)
		WARN_ON(work_pending(&ssp->srcu_work));
	else
		flush_work(&ssp->srcu_work);
	WARN_ON(ssp->srcu_gp_running);
	WARN_ON(ssp->srcu_gp_waiting);
	WARN_ON(ssp->srcu_cb_head);
	WARN_ON(&ssp->srcu_cb_head != ssp->srcu_cb_tail);
}
EXPORT_SYMBOL_GPL(_cleanup_srcu_struct);

/*
 * Removes the count for the old reader from the appropriate element of
 * the srcu_struct.
 */
void __srcu_read_unlock(struct srcu_struct *ssp, int idx)
{
	int newval = ssp->srcu_lock_nesting[idx] - 1;

	WRITE_ONCE(ssp->srcu_lock_nesting[idx], newval);
	if (!newval && READ_ONCE(ssp->srcu_gp_waiting))
		swake_up_one(&ssp->srcu_wq);
}
EXPORT_SYMBOL_GPL(__srcu_read_unlock);

/*
 * Workqueue handler to drive one grace period and invoke any callbacks
 * that become ready as a result.  Single-CPU and !PREEMPT operation
 * means that we get away with murder on synchronization.  ;-)
 */
void srcu_drive_gp(struct work_struct *wp)
{
	int idx;
	struct rcu_head *lh;
	struct rcu_head *rhp;
	struct srcu_struct *ssp;

	ssp = container_of(wp, struct srcu_struct, srcu_work);
	if (ssp->srcu_gp_running || !READ_ONCE(ssp->srcu_cb_head))
		return; /* Already running or nothing to do. */

	/* Remove recently arrived callbacks and wait for readers. */
	WRITE_ONCE(ssp->srcu_gp_running, true);
	local_irq_disable();
	lh = ssp->srcu_cb_head;
	ssp->srcu_cb_head = NULL;
	ssp->srcu_cb_tail = &ssp->srcu_cb_head;
	local_irq_enable();
	idx = ssp->srcu_idx;
	WRITE_ONCE(ssp->srcu_idx, !ssp->srcu_idx);
	WRITE_ONCE(ssp->srcu_gp_waiting, true);  /* srcu_read_unlock() wakes! */
	swait_event_exclusive(ssp->srcu_wq, !READ_ONCE(ssp->srcu_lock_nesting[idx]));
	WRITE_ONCE(ssp->srcu_gp_waiting, false); /* srcu_read_unlock() cheap. */

	/* Invoke the callbacks we removed above. */
	while (lh) {
		rhp = lh;
		lh = lh->next;
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
	WRITE_ONCE(ssp->srcu_gp_running, false);
	if (READ_ONCE(ssp->srcu_cb_head))
		schedule_work(&ssp->srcu_work);
}
EXPORT_SYMBOL_GPL(srcu_drive_gp);

/*
 * Enqueue an SRCU callback on the specified srcu_struct structure,
 * initiating grace-period processing if it is not already running.
 */
void call_srcu(struct srcu_struct *ssp, struct rcu_head *rhp,
	       rcu_callback_t func)
{
	unsigned long flags;

	rhp->func = func;
	rhp->next = NULL;
	local_irq_save(flags);
	*ssp->srcu_cb_tail = rhp;
	ssp->srcu_cb_tail = &rhp->next;
	local_irq_restore(flags);
	if (!READ_ONCE(ssp->srcu_gp_running)) {
		if (likely(srcu_init_done))
			schedule_work(&ssp->srcu_work);
		else if (list_empty(&ssp->srcu_work.entry))
			list_add(&ssp->srcu_work.entry, &srcu_boot_list);
	}
}
EXPORT_SYMBOL_GPL(call_srcu);

/*
 * synchronize_srcu - wait for prior SRCU read-side critical-section completion
 */
void synchronize_srcu(struct srcu_struct *ssp)
{
	struct rcu_synchronize rs;

	init_rcu_head_on_stack(&rs.head);
	init_completion(&rs.completion);
	call_srcu(ssp, &rs.head, wakeme_after_rcu);
	wait_for_completion(&rs.completion);
	destroy_rcu_head_on_stack(&rs.head);
}
EXPORT_SYMBOL_GPL(synchronize_srcu);

/* Lockdep diagnostics.  */
void __init rcu_scheduler_starting(void)
{
	rcu_scheduler_active = RCU_SCHEDULER_RUNNING;
}

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
