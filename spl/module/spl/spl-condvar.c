/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Solaris Porting Layer (SPL) Credential Implementation.
 */

#include <sys/condvar.h>
#include <sys/time.h>

void
__cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
	ASSERT(cvp);
	ASSERT(name == NULL);
	ASSERT(type == CV_DEFAULT);
	ASSERT(arg == NULL);

	cvp->cv_magic = CV_MAGIC;
	init_waitqueue_head(&cvp->cv_event);
	init_waitqueue_head(&cvp->cv_destroy);
	atomic_set(&cvp->cv_waiters, 0);
	atomic_set(&cvp->cv_refs, 1);
	cvp->cv_mutex = NULL;
}
EXPORT_SYMBOL(__cv_init);

static int
cv_destroy_wakeup(kcondvar_t *cvp)
{
	if (!atomic_read(&cvp->cv_waiters) && !atomic_read(&cvp->cv_refs)) {
		ASSERT(cvp->cv_mutex == NULL);
		ASSERT(!waitqueue_active(&cvp->cv_event));
		return (1);
	}

	return (0);
}

void
__cv_destroy(kcondvar_t *cvp)
{
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);

	cvp->cv_magic = CV_DESTROY;
	atomic_dec(&cvp->cv_refs);

	/* Block until all waiters are woken and references dropped. */
	while (cv_destroy_wakeup(cvp) == 0)
		wait_event_timeout(cvp->cv_destroy, cv_destroy_wakeup(cvp), 1);

	ASSERT3P(cvp->cv_mutex, ==, NULL);
	ASSERT3S(atomic_read(&cvp->cv_refs), ==, 0);
	ASSERT3S(atomic_read(&cvp->cv_waiters), ==, 0);
	ASSERT3S(waitqueue_active(&cvp->cv_event), ==, 0);
}
EXPORT_SYMBOL(__cv_destroy);

static void
cv_wait_common(kcondvar_t *cvp, kmutex_t *mp, int state, int io)
{
	DEFINE_WAIT(wait);
	kmutex_t *m;

	ASSERT(cvp);
	ASSERT(mp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	ASSERT(mutex_owned(mp));
	atomic_inc(&cvp->cv_refs);

	m = ACCESS_ONCE(cvp->cv_mutex);
	if (!m)
		m = xchg(&cvp->cv_mutex, mp);
	/* Ensure the same mutex is used by all callers */
	ASSERT(m == NULL || m == mp);

	prepare_to_wait_exclusive(&cvp->cv_event, &wait, state);
	atomic_inc(&cvp->cv_waiters);

	/*
	 * Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty.
	 */
	mutex_exit(mp);
	if (io)
		io_schedule();
	else
		schedule();

	/* No more waiters a different mutex could be used */
	if (atomic_dec_and_test(&cvp->cv_waiters)) {
		/*
		 * This is set without any lock, so it's racy. But this is
		 * just for debug anyway, so make it best-effort
		 */
		cvp->cv_mutex = NULL;
		wake_up(&cvp->cv_destroy);
	}

	finish_wait(&cvp->cv_event, &wait);
	atomic_dec(&cvp->cv_refs);

	/*
	 * Hold mutex after we release the cvp, otherwise we could dead lock
	 * with a thread holding the mutex and call cv_destroy.
	 */
	mutex_enter(mp);
}

void
__cv_wait(kcondvar_t *cvp, kmutex_t *mp)
{
	cv_wait_common(cvp, mp, TASK_UNINTERRUPTIBLE, 0);
}
EXPORT_SYMBOL(__cv_wait);

void
__cv_wait_sig(kcondvar_t *cvp, kmutex_t *mp)
{
	cv_wait_common(cvp, mp, TASK_INTERRUPTIBLE, 0);
}
EXPORT_SYMBOL(__cv_wait_sig);

void
__cv_wait_io(kcondvar_t *cvp, kmutex_t *mp)
{
	cv_wait_common(cvp, mp, TASK_UNINTERRUPTIBLE, 1);
}
EXPORT_SYMBOL(__cv_wait_io);

/*
 * 'expire_time' argument is an absolute wall clock time in jiffies.
 * Return value is time left (expire_time - now) or -1 if timeout occurred.
 */
static clock_t
__cv_timedwait_common(kcondvar_t *cvp, kmutex_t *mp, clock_t expire_time,
    int state)
{
	DEFINE_WAIT(wait);
	kmutex_t *m;
	clock_t time_left;

	ASSERT(cvp);
	ASSERT(mp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	ASSERT(mutex_owned(mp));
	atomic_inc(&cvp->cv_refs);

	m = ACCESS_ONCE(cvp->cv_mutex);
	if (!m)
		m = xchg(&cvp->cv_mutex, mp);
	/* Ensure the same mutex is used by all callers */
	ASSERT(m == NULL || m == mp);

	/* XXX - Does not handle jiffie wrap properly */
	time_left = expire_time - jiffies;
	if (time_left <= 0) {
		/* XXX - doesn't reset cv_mutex */
		atomic_dec(&cvp->cv_refs);
		return (-1);
	}

	prepare_to_wait_exclusive(&cvp->cv_event, &wait, state);
	atomic_inc(&cvp->cv_waiters);

	/*
	 * Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty.
	 */
	mutex_exit(mp);
	time_left = schedule_timeout(time_left);

	/* No more waiters a different mutex could be used */
	if (atomic_dec_and_test(&cvp->cv_waiters)) {
		/*
		 * This is set without any lock, so it's racy. But this is
		 * just for debug anyway, so make it best-effort
		 */
		cvp->cv_mutex = NULL;
		wake_up(&cvp->cv_destroy);
	}

	finish_wait(&cvp->cv_event, &wait);
	atomic_dec(&cvp->cv_refs);

	/*
	 * Hold mutex after we release the cvp, otherwise we could dead lock
	 * with a thread holding the mutex and call cv_destroy.
	 */
	mutex_enter(mp);
	return (time_left > 0 ? time_left : -1);
}

clock_t
__cv_timedwait(kcondvar_t *cvp, kmutex_t *mp, clock_t exp_time)
{
	return (__cv_timedwait_common(cvp, mp, exp_time, TASK_UNINTERRUPTIBLE));
}
EXPORT_SYMBOL(__cv_timedwait);

clock_t
__cv_timedwait_sig(kcondvar_t *cvp, kmutex_t *mp, clock_t exp_time)
{
	return (__cv_timedwait_common(cvp, mp, exp_time, TASK_INTERRUPTIBLE));
}
EXPORT_SYMBOL(__cv_timedwait_sig);

/*
 * 'expire_time' argument is an absolute clock time in nanoseconds.
 * Return value is time left (expire_time - now) or -1 if timeout occurred.
 */
static clock_t
__cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t expire_time,
    int state)
{
	DEFINE_WAIT(wait);
	kmutex_t *m;
	hrtime_t time_left, now;
	unsigned long time_left_us;

	ASSERT(cvp);
	ASSERT(mp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	ASSERT(mutex_owned(mp));
	atomic_inc(&cvp->cv_refs);

	m = ACCESS_ONCE(cvp->cv_mutex);
	if (!m)
		m = xchg(&cvp->cv_mutex, mp);
	/* Ensure the same mutex is used by all callers */
	ASSERT(m == NULL || m == mp);

	now = gethrtime();
	time_left = expire_time - now;
	if (time_left <= 0) {
		atomic_dec(&cvp->cv_refs);
		return (-1);
	}
	time_left_us = time_left / NSEC_PER_USEC;

	prepare_to_wait_exclusive(&cvp->cv_event, &wait, state);
	atomic_inc(&cvp->cv_waiters);

	/*
	 * Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty.
	 */
	mutex_exit(mp);
	/*
	 * Allow a 100 us range to give kernel an opportunity to coalesce
	 * interrupts
	 */
	usleep_range(time_left_us, time_left_us + 100);

	/* No more waiters a different mutex could be used */
	if (atomic_dec_and_test(&cvp->cv_waiters)) {
		/*
		 * This is set without any lock, so it's racy. But this is
		 * just for debug anyway, so make it best-effort
		 */
		cvp->cv_mutex = NULL;
		wake_up(&cvp->cv_destroy);
	}

	finish_wait(&cvp->cv_event, &wait);
	atomic_dec(&cvp->cv_refs);

	mutex_enter(mp);
	time_left = expire_time - gethrtime();
	return (time_left > 0 ? time_left : -1);
}

/*
 * Compatibility wrapper for the cv_timedwait_hires() Illumos interface.
 */
clock_t
cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim, hrtime_t res,
    int flag)
{
	if (res > 1) {
		/*
		 * Align expiration to the specified resolution.
		 */
		if (flag & CALLOUT_FLAG_ROUNDUP)
			tim += res - 1;
		tim = (tim / res) * res;
	}

	if (!(flag & CALLOUT_FLAG_ABSOLUTE))
		tim += gethrtime();

	return (__cv_timedwait_hires(cvp, mp, tim, TASK_UNINTERRUPTIBLE));
}
EXPORT_SYMBOL(cv_timedwait_hires);

void
__cv_signal(kcondvar_t *cvp)
{
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	atomic_inc(&cvp->cv_refs);

	/*
	 * All waiters are added with WQ_FLAG_EXCLUSIVE so only one
	 * waiter will be set runable with each call to wake_up().
	 * Additionally wake_up() holds a spin_lock assoicated with
	 * the wait queue to ensure we don't race waking up processes.
	 */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up(&cvp->cv_event);

	atomic_dec(&cvp->cv_refs);
}
EXPORT_SYMBOL(__cv_signal);

void
__cv_broadcast(kcondvar_t *cvp)
{
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	atomic_inc(&cvp->cv_refs);

	/*
	 * Wake_up_all() will wake up all waiters even those which
	 * have the WQ_FLAG_EXCLUSIVE flag set.
	 */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up_all(&cvp->cv_event);

	atomic_dec(&cvp->cv_refs);
}
EXPORT_SYMBOL(__cv_broadcast);
