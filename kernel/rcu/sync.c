// SPDX-License-Identifier: GPL-2.0+
/*
 * RCU-based infrastructure for lightweight reader-writer locking
 *
 * Copyright (c) 2015, Red Hat, Inc.
 *
 * Author: Oleg Nesterov <oleg@redhat.com>
 */

#include <linux/rcu_sync.h>
#include <linux/sched.h>

enum { GP_IDLE = 0, GP_ENTER, GP_PASSED, GP_EXIT, GP_REPLAY };

#define	rss_lock	gp_wait.lock

/**
 * rcu_sync_init() - Initialize an rcu_sync structure
 * @rsp: Pointer to rcu_sync structure to be initialized
 */
void rcu_sync_init(struct rcu_sync *rsp)
{
	memset(rsp, 0, sizeof(*rsp));
	init_waitqueue_head(&rsp->gp_wait);
}

/**
 * rcu_sync_enter_start - Force readers onto slow path for multiple updates
 * @rsp: Pointer to rcu_sync structure to use for synchronization
 *
 * Must be called after rcu_sync_init() and before first use.
 *
 * Ensures rcu_sync_is_idle() returns false and rcu_sync_{enter,exit}()
 * pairs turn into NO-OPs.
 */
void rcu_sync_enter_start(struct rcu_sync *rsp)
{
	rsp->gp_count++;
	rsp->gp_state = GP_PASSED;
}


static void rcu_sync_func(struct rcu_head *rhp);

static void rcu_sync_call(struct rcu_sync *rsp)
{
	call_rcu_hurry(&rsp->cb_head, rcu_sync_func);
}

/**
 * rcu_sync_func() - Callback function managing reader access to fastpath
 * @rhp: Pointer to rcu_head in rcu_sync structure to use for synchronization
 *
 * This function is passed to call_rcu() function by rcu_sync_enter() and
 * rcu_sync_exit(), so that it is invoked after a grace period following the
 * that invocation of enter/exit.
 *
 * If it is called by rcu_sync_enter() it signals that all the readers were
 * switched onto slow path.
 *
 * If it is called by rcu_sync_exit() it takes action based on events that
 * have taken place in the meantime, so that closely spaced rcu_sync_enter()
 * and rcu_sync_exit() pairs need not wait for a grace period.
 *
 * If another rcu_sync_enter() is invoked before the grace period
 * ended, reset state to allow the next rcu_sync_exit() to let the
 * readers back onto their fastpaths (after a grace period).  If both
 * another rcu_sync_enter() and its matching rcu_sync_exit() are invoked
 * before the grace period ended, re-invoke call_rcu() on behalf of that
 * rcu_sync_exit().  Otherwise, set all state back to idle so that readers
 * can again use their fastpaths.
 */
static void rcu_sync_func(struct rcu_head *rhp)
{
	struct rcu_sync *rsp = container_of(rhp, struct rcu_sync, cb_head);
	unsigned long flags;

	WARN_ON_ONCE(READ_ONCE(rsp->gp_state) == GP_IDLE);
	WARN_ON_ONCE(READ_ONCE(rsp->gp_state) == GP_PASSED);

	spin_lock_irqsave(&rsp->rss_lock, flags);
	if (rsp->gp_count) {
		/*
		 * We're at least a GP after the GP_IDLE->GP_ENTER transition.
		 */
		WRITE_ONCE(rsp->gp_state, GP_PASSED);
		wake_up_locked(&rsp->gp_wait);
	} else if (rsp->gp_state == GP_REPLAY) {
		/*
		 * A new rcu_sync_exit() has happened; requeue the callback to
		 * catch a later GP.
		 */
		WRITE_ONCE(rsp->gp_state, GP_EXIT);
		rcu_sync_call(rsp);
	} else {
		/*
		 * We're at least a GP after the last rcu_sync_exit(); everybody
		 * will now have observed the write side critical section.
		 * Let 'em rip!
		 */
		WRITE_ONCE(rsp->gp_state, GP_IDLE);
	}
	spin_unlock_irqrestore(&rsp->rss_lock, flags);
}

/**
 * rcu_sync_enter() - Force readers onto slowpath
 * @rsp: Pointer to rcu_sync structure to use for synchronization
 *
 * This function is used by updaters who need readers to make use of
 * a slowpath during the update.  After this function returns, all
 * subsequent calls to rcu_sync_is_idle() will return false, which
 * tells readers to stay off their fastpaths.  A later call to
 * rcu_sync_exit() re-enables reader fastpaths.
 *
 * When called in isolation, rcu_sync_enter() must wait for a grace
 * period, however, closely spaced calls to rcu_sync_enter() can
 * optimize away the grace-period wait via a state machine implemented
 * by rcu_sync_enter(), rcu_sync_exit(), and rcu_sync_func().
 */
void rcu_sync_enter(struct rcu_sync *rsp)
{
	int gp_state;

	spin_lock_irq(&rsp->rss_lock);
	gp_state = rsp->gp_state;
	if (gp_state == GP_IDLE) {
		WRITE_ONCE(rsp->gp_state, GP_ENTER);
		WARN_ON_ONCE(rsp->gp_count);
		/*
		 * Note that we could simply do rcu_sync_call(rsp) here and
		 * avoid the "if (gp_state == GP_IDLE)" block below.
		 *
		 * However, synchronize_rcu() can be faster if rcu_expedited
		 * or rcu_blocking_is_gp() is true.
		 *
		 * Another reason is that we can't wait for rcu callback if
		 * we are called at early boot time but this shouldn't happen.
		 */
	}
	rsp->gp_count++;
	spin_unlock_irq(&rsp->rss_lock);

	if (gp_state == GP_IDLE) {
		/*
		 * See the comment above, this simply does the "synchronous"
		 * call_rcu(rcu_sync_func) which does GP_ENTER -> GP_PASSED.
		 */
		synchronize_rcu();
		rcu_sync_func(&rsp->cb_head);
		/* Not really needed, wait_event() would see GP_PASSED. */
		return;
	}

	wait_event(rsp->gp_wait, READ_ONCE(rsp->gp_state) >= GP_PASSED);
}

/**
 * rcu_sync_exit() - Allow readers back onto fast path after grace period
 * @rsp: Pointer to rcu_sync structure to use for synchronization
 *
 * This function is used by updaters who have completed, and can therefore
 * now allow readers to make use of their fastpaths after a grace period
 * has elapsed.  After this grace period has completed, all subsequent
 * calls to rcu_sync_is_idle() will return true, which tells readers that
 * they can once again use their fastpaths.
 */
void rcu_sync_exit(struct rcu_sync *rsp)
{
	WARN_ON_ONCE(READ_ONCE(rsp->gp_state) == GP_IDLE);
	WARN_ON_ONCE(READ_ONCE(rsp->gp_count) == 0);

	spin_lock_irq(&rsp->rss_lock);
	if (!--rsp->gp_count) {
		if (rsp->gp_state == GP_PASSED) {
			WRITE_ONCE(rsp->gp_state, GP_EXIT);
			rcu_sync_call(rsp);
		} else if (rsp->gp_state == GP_EXIT) {
			WRITE_ONCE(rsp->gp_state, GP_REPLAY);
		}
	}
	spin_unlock_irq(&rsp->rss_lock);
}

/**
 * rcu_sync_dtor() - Clean up an rcu_sync structure
 * @rsp: Pointer to rcu_sync structure to be cleaned up
 */
void rcu_sync_dtor(struct rcu_sync *rsp)
{
	int gp_state;

	WARN_ON_ONCE(READ_ONCE(rsp->gp_count));
	WARN_ON_ONCE(READ_ONCE(rsp->gp_state) == GP_PASSED);

	spin_lock_irq(&rsp->rss_lock);
	if (rsp->gp_state == GP_REPLAY)
		WRITE_ONCE(rsp->gp_state, GP_EXIT);
	gp_state = rsp->gp_state;
	spin_unlock_irq(&rsp->rss_lock);

	if (gp_state != GP_IDLE) {
		rcu_barrier();
		WARN_ON_ONCE(rsp->gp_state != GP_IDLE);
	}
}
