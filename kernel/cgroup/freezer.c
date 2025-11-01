// SPDX-License-Identifier: GPL-2.0
#include <linux/cgroup.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>

#include "cgroup-internal.h"

#include <trace/events/cgroup.h>

/*
 * Update CGRP_FROZEN of cgroup.flag
 * Return true if flags is updated; false if flags has no change
 */
static bool cgroup_update_frozen_flag(struct cgroup *cgrp, bool frozen)
{
	lockdep_assert_held(&css_set_lock);

	/* Already there? */
	if (test_bit(CGRP_FROZEN, &cgrp->flags) == frozen)
		return false;

	if (frozen)
		set_bit(CGRP_FROZEN, &cgrp->flags);
	else
		clear_bit(CGRP_FROZEN, &cgrp->flags);

	cgroup_file_notify(&cgrp->events_file);
	TRACE_CGROUP_PATH(notify_frozen, cgrp, frozen);
	return true;
}

/*
 * Propagate the cgroup frozen state upwards by the cgroup tree.
 */
static void cgroup_propagate_frozen(struct cgroup *cgrp, bool frozen)
{
	int desc = 1;

	/*
	 * If the new state is frozen, some freezing ancestor cgroups may change
	 * their state too, depending on if all their descendants are frozen.
	 *
	 * Otherwise, all ancestor cgroups are forced into the non-frozen state.
	 */
	while ((cgrp = cgroup_parent(cgrp))) {
		if (frozen) {
			cgrp->freezer.nr_frozen_descendants += desc;
			if (!test_bit(CGRP_FREEZE, &cgrp->flags) ||
			    (cgrp->freezer.nr_frozen_descendants !=
			    cgrp->nr_descendants))
				continue;
		} else {
			cgrp->freezer.nr_frozen_descendants -= desc;
		}

		if (cgroup_update_frozen_flag(cgrp, frozen))
			desc++;
	}
}

/*
 * Revisit the cgroup frozen state.
 * Checks if the cgroup is really frozen and perform all state transitions.
 */
void cgroup_update_frozen(struct cgroup *cgrp)
{
	bool frozen;

	/*
	 * If the cgroup has to be frozen (CGRP_FREEZE bit set),
	 * and all tasks are frozen and/or stopped, let's consider
	 * the cgroup frozen. Otherwise it's not frozen.
	 */
	frozen = test_bit(CGRP_FREEZE, &cgrp->flags) &&
		cgrp->freezer.nr_frozen_tasks == __cgroup_task_count(cgrp);

	/* If flags is updated, update the state of ancestor cgroups. */
	if (cgroup_update_frozen_flag(cgrp, frozen))
		cgroup_propagate_frozen(cgrp, frozen);
}

/*
 * Increment cgroup's nr_frozen_tasks.
 */
static void cgroup_inc_frozen_cnt(struct cgroup *cgrp)
{
	cgrp->freezer.nr_frozen_tasks++;
}

/*
 * Decrement cgroup's nr_frozen_tasks.
 */
static void cgroup_dec_frozen_cnt(struct cgroup *cgrp)
{
	cgrp->freezer.nr_frozen_tasks--;
	WARN_ON_ONCE(cgrp->freezer.nr_frozen_tasks < 0);
}

/*
 * Enter frozen/stopped state, if not yet there. Update cgroup's counters,
 * and revisit the state of the cgroup, if necessary.
 */
void cgroup_enter_frozen(void)
{
	struct cgroup *cgrp;

	if (current->frozen)
		return;

	spin_lock_irq(&css_set_lock);
	current->frozen = true;
	cgrp = task_dfl_cgroup(current);
	cgroup_inc_frozen_cnt(cgrp);
	cgroup_update_frozen(cgrp);
	spin_unlock_irq(&css_set_lock);
}

/*
 * Conditionally leave frozen/stopped state. Update cgroup's counters,
 * and revisit the state of the cgroup, if necessary.
 *
 * If always_leave is not set, and the cgroup is freezing,
 * we're racing with the cgroup freezing. In this case, we don't
 * drop the frozen counter to avoid a transient switch to
 * the unfrozen state.
 */
void cgroup_leave_frozen(bool always_leave)
{
	struct cgroup *cgrp;

	spin_lock_irq(&css_set_lock);
	cgrp = task_dfl_cgroup(current);
	if (always_leave || !test_bit(CGRP_FREEZE, &cgrp->flags)) {
		cgroup_dec_frozen_cnt(cgrp);
		cgroup_update_frozen(cgrp);
		WARN_ON_ONCE(!current->frozen);
		current->frozen = false;
	} else if (!(current->jobctl & JOBCTL_TRAP_FREEZE)) {
		spin_lock(&current->sighand->siglock);
		current->jobctl |= JOBCTL_TRAP_FREEZE;
		set_thread_flag(TIF_SIGPENDING);
		spin_unlock(&current->sighand->siglock);
	}
	spin_unlock_irq(&css_set_lock);
}

/*
 * Freeze or unfreeze the task by setting or clearing the JOBCTL_TRAP_FREEZE
 * jobctl bit.
 */
static void cgroup_freeze_task(struct task_struct *task, bool freeze)
{
	unsigned long flags;

	/* If the task is about to die, don't bother with freezing it. */
	if (!lock_task_sighand(task, &flags))
		return;

	if (freeze) {
		task->jobctl |= JOBCTL_TRAP_FREEZE;
		signal_wake_up(task, false);
	} else {
		task->jobctl &= ~JOBCTL_TRAP_FREEZE;
		wake_up_process(task);
	}

	unlock_task_sighand(task, &flags);
}

/*
 * Freeze or unfreeze all tasks in the given cgroup.
 */
static void cgroup_do_freeze(struct cgroup *cgrp, bool freeze, u64 ts_nsec)
{
	struct css_task_iter it;
	struct task_struct *task;

	lockdep_assert_held(&cgroup_mutex);

	spin_lock_irq(&css_set_lock);
	write_seqcount_begin(&cgrp->freezer.freeze_seq);
	if (freeze) {
		set_bit(CGRP_FREEZE, &cgrp->flags);
		cgrp->freezer.freeze_start_nsec = ts_nsec;
	} else {
		clear_bit(CGRP_FREEZE, &cgrp->flags);
		cgrp->freezer.frozen_nsec += (ts_nsec -
			cgrp->freezer.freeze_start_nsec);
	}
	write_seqcount_end(&cgrp->freezer.freeze_seq);
	spin_unlock_irq(&css_set_lock);

	if (freeze)
		TRACE_CGROUP_PATH(freeze, cgrp);
	else
		TRACE_CGROUP_PATH(unfreeze, cgrp);

	css_task_iter_start(&cgrp->self, 0, &it);
	while ((task = css_task_iter_next(&it))) {
		/*
		 * Ignore kernel threads here. Freezing cgroups containing
		 * kthreads isn't supported.
		 */
		if (task->flags & PF_KTHREAD)
			continue;
		cgroup_freeze_task(task, freeze);
	}
	css_task_iter_end(&it);

	/*
	 * Cgroup state should be revisited here to cover empty leaf cgroups
	 * and cgroups which descendants are already in the desired state.
	 */
	spin_lock_irq(&css_set_lock);
	if (cgrp->nr_descendants == cgrp->freezer.nr_frozen_descendants)
		cgroup_update_frozen(cgrp);
	spin_unlock_irq(&css_set_lock);
}

/*
 * Adjust the task state (freeze or unfreeze) and revisit the state of
 * source and destination cgroups.
 */
void cgroup_freezer_migrate_task(struct task_struct *task,
				 struct cgroup *src, struct cgroup *dst)
{
	lockdep_assert_held(&css_set_lock);

	/*
	 * Kernel threads are not supposed to be frozen at all.
	 */
	if (task->flags & PF_KTHREAD)
		return;

	/*
	 * It's not necessary to do changes if both of the src and dst cgroups
	 * are not freezing and task is not frozen.
	 */
	if (!test_bit(CGRP_FREEZE, &src->flags) &&
	    !test_bit(CGRP_FREEZE, &dst->flags) &&
	    !task->frozen)
		return;

	/*
	 * Adjust counters of freezing and frozen tasks.
	 * Note, that if the task is frozen, but the destination cgroup is not
	 * frozen, we bump both counters to keep them balanced.
	 */
	if (task->frozen) {
		cgroup_inc_frozen_cnt(dst);
		cgroup_dec_frozen_cnt(src);
	}
	cgroup_update_frozen(dst);
	cgroup_update_frozen(src);

	/*
	 * Force the task to the desired state.
	 */
	cgroup_freeze_task(task, test_bit(CGRP_FREEZE, &dst->flags));
}

void cgroup_freeze(struct cgroup *cgrp, bool freeze)
{
	struct cgroup_subsys_state *css;
	struct cgroup *parent;
	struct cgroup *dsct;
	bool applied = false;
	u64 ts_nsec;
	bool old_e;

	lockdep_assert_held(&cgroup_mutex);

	/*
	 * Nothing changed? Just exit.
	 */
	if (cgrp->freezer.freeze == freeze)
		return;

	cgrp->freezer.freeze = freeze;
	ts_nsec = ktime_get_ns();

	/*
	 * Propagate changes downwards the cgroup tree.
	 */
	css_for_each_descendant_pre(css, &cgrp->self) {
		dsct = css->cgroup;

		if (cgroup_is_dead(dsct))
			continue;

		/*
		 * e_freeze is affected by parent's e_freeze and dst's freeze.
		 * If old e_freeze eq new e_freeze, no change, its children
		 * will not be affected. So do nothing and skip the subtree
		 */
		old_e = dsct->freezer.e_freeze;
		parent = cgroup_parent(dsct);
		dsct->freezer.e_freeze = (dsct->freezer.freeze ||
					  parent->freezer.e_freeze);
		if (dsct->freezer.e_freeze == old_e) {
			css = css_rightmost_descendant(css);
			continue;
		}

		/*
		 * Do change actual state: freeze or unfreeze.
		 */
		cgroup_do_freeze(dsct, freeze, ts_nsec);
		applied = true;
	}

	/*
	 * Even if the actual state hasn't changed, let's notify a user.
	 * The state can be enforced by an ancestor cgroup: the cgroup
	 * can already be in the desired state or it can be locked in the
	 * opposite state, so that the transition will never happen.
	 * In both cases it's better to notify a user, that there is
	 * nothing to wait for.
	 */
	if (!applied) {
		TRACE_CGROUP_PATH(notify_frozen, cgrp,
				  test_bit(CGRP_FROZEN, &cgrp->flags));
		cgroup_file_notify(&cgrp->events_file);
	}
}
