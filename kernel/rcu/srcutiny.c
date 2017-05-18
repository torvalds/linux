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

static int init_srcu_struct_fields(struct srcu_struct *sp)
{
	sp->srcu_lock_nesting[0] = 0;
	sp->srcu_lock_nesting[1] = 0;
	init_swait_queue_head(&sp->srcu_wq);
	sp->srcu_gp_seq = 0;
	rcu_segcblist_init(&sp->srcu_cblist);
	sp->srcu_gp_running = false;
	sp->srcu_gp_waiting = false;
	sp->srcu_idx = 0;
	INIT_WORK(&sp->srcu_work, srcu_drive_gp);
	return 0;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *sp, const char *name,
		       struct lock_class_key *key)
{
	/* Don't re-initialize a lock while it is held. */
	debug_check_no_locks_freed((void *)sp, sizeof(*sp));
	lockdep_init_map(&sp->dep_map, name, key, 0);
	return init_srcu_struct_fields(sp);
}
EXPORT_SYMBOL_GPL(__init_srcu_struct);

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/*
 * init_srcu_struct - initialize a sleep-RCU structure
 * @sp: structure to initialize.
 *
 * Must invoke this on a given srcu_struct before passing that srcu_struct
 * to any other function.  Each srcu_struct represents a separate domain
 * of SRCU protection.
 */
int init_srcu_struct(struct srcu_struct *sp)
{
	return init_srcu_struct_fields(sp);
}
EXPORT_SYMBOL_GPL(init_srcu_struct);

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/*
 * cleanup_srcu_struct - deconstruct a sleep-RCU structure
 * @sp: structure to clean up.
 *
 * Must invoke this after you are finished using a given srcu_struct that
 * was initialized via init_srcu_struct(), else you leak memory.
 */
void cleanup_srcu_struct(struct srcu_struct *sp)
{
	WARN_ON(sp->srcu_lock_nesting[0] || sp->srcu_lock_nesting[1]);
	flush_work(&sp->srcu_work);
	WARN_ON(rcu_seq_state(sp->srcu_gp_seq));
	WARN_ON(sp->srcu_gp_running);
	WARN_ON(sp->srcu_gp_waiting);
	WARN_ON(!rcu_segcblist_empty(&sp->srcu_cblist));
}
EXPORT_SYMBOL_GPL(cleanup_srcu_struct);

/*
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct.  Must be called from process context.
 * Returns an index that must be passed to the matching srcu_read_unlock().
 */
int __srcu_read_lock(struct srcu_struct *sp)
{
	int idx;

	idx = READ_ONCE(sp->srcu_idx);
	WRITE_ONCE(sp->srcu_lock_nesting[idx], sp->srcu_lock_nesting[idx] + 1);
	return idx;
}
EXPORT_SYMBOL_GPL(__srcu_read_lock);

/*
 * Removes the count for the old reader from the appropriate element of
 * the srcu_struct.  Must be called from process context.
 */
void __srcu_read_unlock(struct srcu_struct *sp, int idx)
{
	int newval = sp->srcu_lock_nesting[idx] - 1;

	WRITE_ONCE(sp->srcu_lock_nesting[idx], newval);
	if (!newval && READ_ONCE(sp->srcu_gp_waiting))
		swake_up(&sp->srcu_wq);
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
	struct rcu_cblist ready_cbs;
	struct srcu_struct *sp;
	struct rcu_head *rhp;

	sp = container_of(wp, struct srcu_struct, srcu_work);
	if (sp->srcu_gp_running || rcu_segcblist_empty(&sp->srcu_cblist))
		return; /* Already running or nothing to do. */

	/* Tag recently arrived callbacks and wait for readers. */
	WRITE_ONCE(sp->srcu_gp_running, true);
	rcu_segcblist_accelerate(&sp->srcu_cblist,
				 rcu_seq_snap(&sp->srcu_gp_seq));
	rcu_seq_start(&sp->srcu_gp_seq);
	idx = sp->srcu_idx;
	WRITE_ONCE(sp->srcu_idx, !sp->srcu_idx);
	WRITE_ONCE(sp->srcu_gp_waiting, true);  /* srcu_read_unlock() wakes! */
	swait_event(sp->srcu_wq, !READ_ONCE(sp->srcu_lock_nesting[idx]));
	WRITE_ONCE(sp->srcu_gp_waiting, false); /* srcu_read_unlock() cheap. */
	rcu_seq_end(&sp->srcu_gp_seq);

	/* Update callback list based on GP, and invoke ready callbacks. */
	rcu_segcblist_advance(&sp->srcu_cblist,
			      rcu_seq_current(&sp->srcu_gp_seq));
	if (rcu_segcblist_ready_cbs(&sp->srcu_cblist)) {
		rcu_cblist_init(&ready_cbs);
		local_irq_disable();
		rcu_segcblist_extract_done_cbs(&sp->srcu_cblist, &ready_cbs);
		local_irq_enable();
		rhp = rcu_cblist_dequeue(&ready_cbs);
		for (; rhp != NULL; rhp = rcu_cblist_dequeue(&ready_cbs)) {
			local_bh_disable();
			rhp->func(rhp);
			local_bh_enable();
		}
		local_irq_disable();
		rcu_segcblist_insert_count(&sp->srcu_cblist, &ready_cbs);
		local_irq_enable();
	}
	WRITE_ONCE(sp->srcu_gp_running, false);

	/*
	 * If more callbacks, reschedule ourselves.  This can race with
	 * a call_srcu() at interrupt level, but the ->srcu_gp_running
	 * checks will straighten that out.
	 */
	if (!rcu_segcblist_empty(&sp->srcu_cblist))
		schedule_work(&sp->srcu_work);
}
EXPORT_SYMBOL_GPL(srcu_drive_gp);

/*
 * Enqueue an SRCU callback on the specified srcu_struct structure,
 * initiating grace-period processing if it is not already running.
 */
void call_srcu(struct srcu_struct *sp, struct rcu_head *head,
	       rcu_callback_t func)
{
	unsigned long flags;

	head->func = func;
	local_irq_save(flags);
	rcu_segcblist_enqueue(&sp->srcu_cblist, head, false);
	local_irq_restore(flags);
	if (!READ_ONCE(sp->srcu_gp_running))
		schedule_work(&sp->srcu_work);
}
EXPORT_SYMBOL_GPL(call_srcu);

/*
 * synchronize_srcu - wait for prior SRCU read-side critical-section completion
 */
void synchronize_srcu(struct srcu_struct *sp)
{
	struct rcu_synchronize rs;

	init_rcu_head_on_stack(&rs.head);
	init_completion(&rs.completion);
	call_srcu(sp, &rs.head, wakeme_after_rcu);
	wait_for_completion(&rs.completion);
	destroy_rcu_head_on_stack(&rs.head);
}
EXPORT_SYMBOL_GPL(synchronize_srcu);
