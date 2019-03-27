/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

/*
 * ZTHR Infrastructure
 * ===================
 *
 * ZTHR threads are used for isolated operations that span multiple txgs
 * within a SPA. They generally exist from SPA creation/loading and until
 * the SPA is exported/destroyed. The ideal requirements for an operation
 * to be modeled with a zthr are the following:
 *
 * 1] The operation needs to run over multiple txgs.
 * 2] There is be a single point of reference in memory or on disk that
 *    indicates whether the operation should run/is running or is
 *    stopped.
 *
 * If the operation satisfies the above then the following rules guarantee
 * a certain level of correctness:
 *
 * 1] Any thread EXCEPT the zthr changes the work indicator from stopped
 *    to running but not the opposite.
 * 2] Only the zthr can change the work indicator from running to stopped
 *    (e.g. when it is done) but not the opposite.
 *
 * This way a normal zthr cycle should go like this:
 *
 * 1] An external thread changes the work indicator from stopped to
 *    running and wakes up the zthr.
 * 2] The zthr wakes up, checks the indicator and starts working.
 * 3] When the zthr is done, it changes the indicator to stopped, allowing
 *    a new cycle to start.
 *
 * Besides being awakened by other threads, a zthr can be configured
 * during creation to wakeup on it's own after a specified interval
 * [see zthr_create_timer()].
 *
 * == ZTHR creation
 *
 * Every zthr needs three inputs to start running:
 *
 * 1] A user-defined checker function (checkfunc) that decides whether
 *    the zthr should start working or go to sleep. The function should
 *    return TRUE when the zthr needs to work or FALSE to let it sleep,
 *    and should adhere to the following signature:
 *    boolean_t checkfunc_name(void *args, zthr_t *t);
 *
 * 2] A user-defined ZTHR function (func) which the zthr executes when
 *    it is not sleeping. The function should adhere to the following
 *    signature type:
 *    int func_name(void *args, zthr_t *t);
 *
 * 3] A void args pointer that will be passed to checkfunc and func
 *    implicitly by the infrastructure.
 *
 * The reason why the above API needs two different functions,
 * instead of one that both checks and does the work, has to do with
 * the zthr's internal lock (zthr_lock) and the allowed cancellation
 * windows. We want to hold the zthr_lock while running checkfunc
 * but not while running func. This way the zthr can be cancelled
 * while doing work and not while checking for work.
 *
 * To start a zthr:
 *     zthr_t *zthr_pointer = zthr_create(checkfunc, func, args);
 * or
 *     zthr_t *zthr_pointer = zthr_create_timer(checkfunc, func,
 *         args, max_sleep);
 *
 * After that you should be able to wakeup, cancel, and resume the
 * zthr from another thread using zthr_pointer.
 *
 * NOTE: ZTHR threads could potentially wake up spuriously and the
 * user should take this into account when writing a checkfunc.
 * [see ZTHR state transitions]
 *
 * == ZTHR cancellation
 *
 * ZTHR threads must be cancelled when their SPA is being exported
 * or when they need to be paused so they don't interfere with other
 * operations.
 *
 * To cancel a zthr:
 *     zthr_cancel(zthr_pointer);
 *
 * To resume it:
 *     zthr_resume(zthr_pointer);
 *
 * A zthr will implicitly check if it has received a cancellation
 * signal every time func returns and everytime it wakes up [see ZTHR
 * state transitions below].
 *
 * At times, waiting for the zthr's func to finish its job may take
 * time. This may be very time-consuming for some operations that
 * need to cancel the SPA's zthrs (e.g spa_export). For this scenario
 * the user can explicitly make their ZTHR function aware of incoming
 * cancellation signals using zthr_iscancelled(). A common pattern for
 * that looks like this:
 *
 * int
 * func_name(void *args, zthr_t *t)
 * {
 *     ... <unpack args> ...
 *     while (!work_done && !zthr_iscancelled(t)) {
 *         ... <do more work> ...
 *     }
 *     return (0);
 * }
 *
 * == ZTHR exit
 *
 * For the rare cases where the zthr wants to stop running voluntarily
 * while running its ZTHR function (func), we provide zthr_exit().
 * When a zthr has voluntarily stopped running, it can be resumed with
 * zthr_resume(), just like it would if it was cancelled by some other
 * thread.
 *
 * == ZTHR cleanup
 *
 * Cancelling a zthr doesn't clean up its metadata (internal locks,
 * function pointers to func and checkfunc, etc..). This is because
 * we want to keep them around in case we want to resume the execution
 * of the zthr later. Similarly for zthrs that exit themselves.
 *
 * To completely cleanup a zthr, cancel it first to ensure that it
 * is not running and then use zthr_destroy().
 *
 * == ZTHR state transitions
 *
 *    zthr creation
 *      +
 *      |
 *      |      woke up
 *      |   +--------------+ sleep
 *      |   |                  ^
 *      |   |                  |
 *      |   |                  | FALSE
 *      |   |                  |
 *      v   v     FALSE        +
 *   cancelled? +---------> checkfunc?
 *      +   ^                  +
 *      |   |                  |
 *      |   |                  | TRUE
 *      |   |                  |
 *      |   |  func returned   v
 *      |   +---------------+ func
 *      |
 *      | TRUE
 *      |
 *      v
 *   zthr stopped running
 *
 */

#include <sys/zfs_context.h>
#include <sys/zthr.h>

void
zthr_exit(zthr_t *t, int rc)
{
	ASSERT3P(t->zthr_thread, ==, curthread);
	mutex_enter(&t->zthr_lock);
	t->zthr_thread = NULL;
	t->zthr_rc = rc;
	cv_broadcast(&t->zthr_cv);
	mutex_exit(&t->zthr_lock);
	thread_exit();
}

static void
zthr_procedure(void *arg)
{
	zthr_t *t = arg;
	int rc = 0;

	mutex_enter(&t->zthr_lock);
	while (!t->zthr_cancel) {
		if (t->zthr_checkfunc(t->zthr_arg, t)) {
			mutex_exit(&t->zthr_lock);
			rc = t->zthr_func(t->zthr_arg, t);
			mutex_enter(&t->zthr_lock);
		} else {
			/* go to sleep */
			if (t->zthr_wait_time == 0) {
				cv_wait(&t->zthr_cv, &t->zthr_lock);
			} else {
				(void) cv_timedwait_hires(&t->zthr_cv,
				    &t->zthr_lock, t->zthr_wait_time,
				    MSEC2NSEC(1), 0);
			}
		}
	}
	mutex_exit(&t->zthr_lock);

	zthr_exit(t, rc);
}

zthr_t *
zthr_create(zthr_checkfunc_t *checkfunc, zthr_func_t *func, void *arg)
{
	return (zthr_create_timer(checkfunc, func, arg, (hrtime_t)0));
}

/*
 * Create a zthr with specified maximum sleep time.  If the time
 * in sleeping state exceeds max_sleep, a wakeup(do the check and
 * start working if required) will be triggered.
 */
zthr_t *
zthr_create_timer(zthr_checkfunc_t *checkfunc, zthr_func_t *func,
    void *arg, hrtime_t max_sleep)
{
	zthr_t *t = kmem_zalloc(sizeof (*t), KM_SLEEP);
	mutex_init(&t->zthr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&t->zthr_cv, NULL, CV_DEFAULT, NULL);

	mutex_enter(&t->zthr_lock);
	t->zthr_checkfunc = checkfunc;
	t->zthr_func = func;
	t->zthr_arg = arg;
	t->zthr_wait_time = max_sleep;

	t->zthr_thread = thread_create(NULL, 0, zthr_procedure, t,
	    0, &p0, TS_RUN, minclsyspri);
	mutex_exit(&t->zthr_lock);

	return (t);
}

void
zthr_destroy(zthr_t *t)
{
	VERIFY3P(t->zthr_thread, ==, NULL);
	mutex_destroy(&t->zthr_lock);
	cv_destroy(&t->zthr_cv);
	kmem_free(t, sizeof (*t));
}

/*
 * Note: If the zthr is not sleeping and misses the wakeup
 * (e.g it is running its ZTHR function), it will check if
 * there is work to do before going to sleep using its checker
 * function [see ZTHR state transition in ZTHR block comment].
 * Thus, missing the wakeup still yields the expected behavior.
 */
void
zthr_wakeup(zthr_t *t)
{
	mutex_enter(&t->zthr_lock);
	cv_broadcast(&t->zthr_cv);
	mutex_exit(&t->zthr_lock);
}

/*
 * Note: If the zthr is not running (e.g. has been cancelled
 * already), this is a no-op.
 */
int
zthr_cancel(zthr_t *t)
{
	int rc = 0;

	mutex_enter(&t->zthr_lock);

	/* broadcast in case the zthr is sleeping */
	cv_broadcast(&t->zthr_cv);

	t->zthr_cancel = B_TRUE;
	while (t->zthr_thread != NULL)
		cv_wait(&t->zthr_cv, &t->zthr_lock);
	t->zthr_cancel = B_FALSE;
	rc = t->zthr_rc;
	mutex_exit(&t->zthr_lock);

	return (rc);
}

void
zthr_resume(zthr_t *t)
{
	ASSERT3P(t->zthr_thread, ==, NULL);

	mutex_enter(&t->zthr_lock);

	ASSERT3P(&t->zthr_checkfunc, !=, NULL);
	ASSERT3P(&t->zthr_func, !=, NULL);
	ASSERT(!t->zthr_cancel);

	t->zthr_thread = thread_create(NULL, 0, zthr_procedure, t,
	    0, &p0, TS_RUN, minclsyspri);

	mutex_exit(&t->zthr_lock);
}

/*
 * This function is intended to be used by the zthr itself
 * to check if another thread has signal it to stop running.
 *
 * returns TRUE if we are in the middle of trying to cancel
 *     this thread.
 *
 * returns FALSE otherwise.
 */
boolean_t
zthr_iscancelled(zthr_t *t)
{
	boolean_t cancelled;

	ASSERT3P(t->zthr_thread, ==, curthread);

	mutex_enter(&t->zthr_lock);
	cancelled = t->zthr_cancel;
	mutex_exit(&t->zthr_lock);

	return (cancelled);
}

boolean_t
zthr_isrunning(zthr_t *t)
{
	boolean_t running;

	mutex_enter(&t->zthr_lock);
	running = (t->zthr_thread != NULL);
	mutex_exit(&t->zthr_lock);

	return (running);
}
