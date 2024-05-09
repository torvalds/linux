// SPDX-License-Identifier: GPL-2.0-only
/*
 * kernel/freezer.c - Function to freeze a process
 *
 * Originally from kernel/power/process.c
 */

#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/freezer.h>
#include <linux/kthread.h>

/* total number of freezing conditions in effect */
DEFINE_STATIC_KEY_FALSE(freezer_active);
EXPORT_SYMBOL(freezer_active);

/*
 * indicate whether PM freezing is in effect, protected by
 * system_transition_mutex
 */
bool pm_freezing;
bool pm_nosig_freezing;

/* protects freezing and frozen transitions */
static DEFINE_SPINLOCK(freezer_lock);

/**
 * freezing_slow_path - slow path for testing whether a task needs to be frozen
 * @p: task to be tested
 *
 * This function is called by freezing() if freezer_active isn't zero
 * and tests whether @p needs to enter and stay in frozen state.  Can be
 * called under any context.  The freezers are responsible for ensuring the
 * target tasks see the updated state.
 */
bool freezing_slow_path(struct task_struct *p)
{
	if (p->flags & (PF_NOFREEZE | PF_SUSPEND_TASK))
		return false;

	if (test_tsk_thread_flag(p, TIF_MEMDIE))
		return false;

	if (pm_nosig_freezing || cgroup_freezing(p))
		return true;

	if (pm_freezing && !(p->flags & PF_KTHREAD))
		return true;

	return false;
}
EXPORT_SYMBOL(freezing_slow_path);

bool frozen(struct task_struct *p)
{
	return READ_ONCE(p->__state) & TASK_FROZEN;
}

/* Refrigerator is place where frozen processes are stored :-). */
bool __refrigerator(bool check_kthr_stop)
{
	unsigned int state = get_current_state();
	bool was_frozen = false;

	pr_debug("%s entered refrigerator\n", current->comm);

	WARN_ON_ONCE(state && !(state & TASK_NORMAL));

	for (;;) {
		bool freeze;

		raw_spin_lock_irq(&current->pi_lock);
		set_current_state(TASK_FROZEN);
		/* unstale saved_state so that __thaw_task() will wake us up */
		current->saved_state = TASK_RUNNING;
		raw_spin_unlock_irq(&current->pi_lock);

		spin_lock_irq(&freezer_lock);
		freeze = freezing(current) && !(check_kthr_stop && kthread_should_stop());
		spin_unlock_irq(&freezer_lock);

		if (!freeze)
			break;

		was_frozen = true;
		schedule();
	}
	__set_current_state(TASK_RUNNING);

	pr_debug("%s left refrigerator\n", current->comm);

	return was_frozen;
}
EXPORT_SYMBOL(__refrigerator);

static void fake_signal_wake_up(struct task_struct *p)
{
	unsigned long flags;

	if (lock_task_sighand(p, &flags)) {
		signal_wake_up(p, 0);
		unlock_task_sighand(p, &flags);
	}
}

static int __set_task_frozen(struct task_struct *p, void *arg)
{
	unsigned int state = READ_ONCE(p->__state);

	if (p->on_rq)
		return 0;

	if (p != current && task_curr(p))
		return 0;

	if (!(state & (TASK_FREEZABLE | __TASK_STOPPED | __TASK_TRACED)))
		return 0;

	/*
	 * Only TASK_NORMAL can be augmented with TASK_FREEZABLE, since they
	 * can suffer spurious wakeups.
	 */
	if (state & TASK_FREEZABLE)
		WARN_ON_ONCE(!(state & TASK_NORMAL));

#ifdef CONFIG_LOCKDEP
	/*
	 * It's dangerous to freeze with locks held; there be dragons there.
	 */
	if (!(state & __TASK_FREEZABLE_UNSAFE))
		WARN_ON_ONCE(debug_locks && p->lockdep_depth);
#endif

	p->saved_state = p->__state;
	WRITE_ONCE(p->__state, TASK_FROZEN);
	return TASK_FROZEN;
}

static bool __freeze_task(struct task_struct *p)
{
	/* TASK_FREEZABLE|TASK_STOPPED|TASK_TRACED -> TASK_FROZEN */
	return task_call_func(p, __set_task_frozen, NULL);
}

/**
 * freeze_task - send a freeze request to given task
 * @p: task to send the request to
 *
 * If @p is freezing, the freeze request is sent either by sending a fake
 * signal (if it's not a kernel thread) or waking it up (if it's a kernel
 * thread).
 *
 * RETURNS:
 * %false, if @p is not freezing or already frozen; %true, otherwise
 */
bool freeze_task(struct task_struct *p)
{
	unsigned long flags;

	spin_lock_irqsave(&freezer_lock, flags);
	if (!freezing(p) || frozen(p) || __freeze_task(p)) {
		spin_unlock_irqrestore(&freezer_lock, flags);
		return false;
	}

	if (!(p->flags & PF_KTHREAD))
		fake_signal_wake_up(p);
	else
		wake_up_state(p, TASK_NORMAL);

	spin_unlock_irqrestore(&freezer_lock, flags);
	return true;
}

/*
 * Restore the saved_state before the task entered freezer. For typical task
 * in the __refrigerator(), saved_state == TASK_RUNNING so nothing happens
 * here. For tasks which were TASK_NORMAL | TASK_FREEZABLE, their initial state
 * is restored unless they got an expected wakeup (see ttwu_state_match()).
 * Returns 1 if the task state was restored.
 */
static int __restore_freezer_state(struct task_struct *p, void *arg)
{
	unsigned int state = p->saved_state;

	if (state != TASK_RUNNING) {
		WRITE_ONCE(p->__state, state);
		return 1;
	}

	return 0;
}

void __thaw_task(struct task_struct *p)
{
	unsigned long flags;

	spin_lock_irqsave(&freezer_lock, flags);
	if (WARN_ON_ONCE(freezing(p)))
		goto unlock;

	if (task_call_func(p, __restore_freezer_state, NULL))
		goto unlock;

	wake_up_state(p, TASK_FROZEN);
unlock:
	spin_unlock_irqrestore(&freezer_lock, flags);
}

/**
 * set_freezable - make %current freezable
 *
 * Mark %current freezable and enter refrigerator if necessary.
 */
bool set_freezable(void)
{
	might_sleep();

	/*
	 * Modify flags while holding freezer_lock.  This ensures the
	 * freezer notices that we aren't frozen yet or the freezing
	 * condition is visible to try_to_freeze() below.
	 */
	spin_lock_irq(&freezer_lock);
	current->flags &= ~PF_NOFREEZE;
	spin_unlock_irq(&freezer_lock);

	return try_to_freeze();
}
EXPORT_SYMBOL(set_freezable);
