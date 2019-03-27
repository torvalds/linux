/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Attilio Rao <attilio@freebsd.org>
 * Copyright (c) 2001 Jason Evans <jasone@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
 * Shared/exclusive locks.  This implementation attempts to ensure
 * deterministic lock granting behavior, so that slocks and xlocks are
 * interleaved.
 *
 * Priority propagation will not generally raise the priority of lock holders,
 * so should not be relied upon in combination with sx locks.
 */

#include "opt_ddb.h"
#include "opt_hwpmc_hooks.h"
#include "opt_no_adaptive_sx.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sleepqueue.h>
#include <sys/sx.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#if defined(SMP) && !defined(NO_ADAPTIVE_SX)
#include <machine/cpu.h>
#endif

#ifdef DDB
#include <ddb/ddb.h>
#endif

#if defined(SMP) && !defined(NO_ADAPTIVE_SX)
#define	ADAPTIVE_SX
#endif

#ifdef HWPMC_HOOKS
#include <sys/pmckern.h>
PMC_SOFT_DECLARE( , , lock, failed);
#endif

/* Handy macros for sleep queues. */
#define	SQ_EXCLUSIVE_QUEUE	0
#define	SQ_SHARED_QUEUE		1

/*
 * Variations on DROP_GIANT()/PICKUP_GIANT() for use in this file.  We
 * drop Giant anytime we have to sleep or if we adaptively spin.
 */
#define	GIANT_DECLARE							\
	int _giantcnt = 0;						\
	WITNESS_SAVE_DECL(Giant)					\

#define	GIANT_SAVE(work) do {						\
	if (__predict_false(mtx_owned(&Giant))) {			\
		work++;							\
		WITNESS_SAVE(&Giant.lock_object, Giant);		\
		while (mtx_owned(&Giant)) {				\
			_giantcnt++;					\
			mtx_unlock(&Giant);				\
		}							\
	}								\
} while (0)

#define GIANT_RESTORE() do {						\
	if (_giantcnt > 0) {						\
		mtx_assert(&Giant, MA_NOTOWNED);			\
		while (_giantcnt--)					\
			mtx_lock(&Giant);				\
		WITNESS_RESTORE(&Giant.lock_object, Giant);		\
	}								\
} while (0)

/*
 * Returns true if an exclusive lock is recursed.  It assumes
 * curthread currently has an exclusive lock.
 */
#define	sx_recursed(sx)		((sx)->sx_recurse != 0)

static void	assert_sx(const struct lock_object *lock, int what);
#ifdef DDB
static void	db_show_sx(const struct lock_object *lock);
#endif
static void	lock_sx(struct lock_object *lock, uintptr_t how);
#ifdef KDTRACE_HOOKS
static int	owner_sx(const struct lock_object *lock, struct thread **owner);
#endif
static uintptr_t unlock_sx(struct lock_object *lock);

struct lock_class lock_class_sx = {
	.lc_name = "sx",
	.lc_flags = LC_SLEEPLOCK | LC_SLEEPABLE | LC_RECURSABLE | LC_UPGRADABLE,
	.lc_assert = assert_sx,
#ifdef DDB
	.lc_ddb_show = db_show_sx,
#endif
	.lc_lock = lock_sx,
	.lc_unlock = unlock_sx,
#ifdef KDTRACE_HOOKS
	.lc_owner = owner_sx,
#endif
};

#ifndef INVARIANTS
#define	_sx_assert(sx, what, file, line)
#endif

#ifdef ADAPTIVE_SX
static __read_frequently u_int asx_retries;
static __read_frequently u_int asx_loops;
static SYSCTL_NODE(_debug, OID_AUTO, sx, CTLFLAG_RD, NULL, "sxlock debugging");
SYSCTL_UINT(_debug_sx, OID_AUTO, retries, CTLFLAG_RW, &asx_retries, 0, "");
SYSCTL_UINT(_debug_sx, OID_AUTO, loops, CTLFLAG_RW, &asx_loops, 0, "");

static struct lock_delay_config __read_frequently sx_delay;

SYSCTL_INT(_debug_sx, OID_AUTO, delay_base, CTLFLAG_RW, &sx_delay.base,
    0, "");
SYSCTL_INT(_debug_sx, OID_AUTO, delay_max, CTLFLAG_RW, &sx_delay.max,
    0, "");

static void
sx_lock_delay_init(void *arg __unused)
{

	lock_delay_default_init(&sx_delay);
	asx_retries = 10;
	asx_loops = max(10000, sx_delay.max);
}
LOCK_DELAY_SYSINIT(sx_lock_delay_init);
#endif

void
assert_sx(const struct lock_object *lock, int what)
{

	sx_assert((const struct sx *)lock, what);
}

void
lock_sx(struct lock_object *lock, uintptr_t how)
{
	struct sx *sx;

	sx = (struct sx *)lock;
	if (how)
		sx_slock(sx);
	else
		sx_xlock(sx);
}

uintptr_t
unlock_sx(struct lock_object *lock)
{
	struct sx *sx;

	sx = (struct sx *)lock;
	sx_assert(sx, SA_LOCKED | SA_NOTRECURSED);
	if (sx_xlocked(sx)) {
		sx_xunlock(sx);
		return (0);
	} else {
		sx_sunlock(sx);
		return (1);
	}
}

#ifdef KDTRACE_HOOKS
int
owner_sx(const struct lock_object *lock, struct thread **owner)
{
	const struct sx *sx;
	uintptr_t x;

	sx = (const struct sx *)lock;
	x = sx->sx_lock;
	*owner = NULL;
	return ((x & SX_LOCK_SHARED) != 0 ? (SX_SHARERS(x) != 0) :
	    ((*owner = (struct thread *)SX_OWNER(x)) != NULL));
}
#endif

void
sx_sysinit(void *arg)
{
	struct sx_args *sargs = arg;

	sx_init_flags(sargs->sa_sx, sargs->sa_desc, sargs->sa_flags);
}

void
sx_init_flags(struct sx *sx, const char *description, int opts)
{
	int flags;

	MPASS((opts & ~(SX_QUIET | SX_RECURSE | SX_NOWITNESS | SX_DUPOK |
	    SX_NOPROFILE | SX_NEW)) == 0);
	ASSERT_ATOMIC_LOAD_PTR(sx->sx_lock,
	    ("%s: sx_lock not aligned for %s: %p", __func__, description,
	    &sx->sx_lock));

	flags = LO_SLEEPABLE | LO_UPGRADABLE;
	if (opts & SX_DUPOK)
		flags |= LO_DUPOK;
	if (opts & SX_NOPROFILE)
		flags |= LO_NOPROFILE;
	if (!(opts & SX_NOWITNESS))
		flags |= LO_WITNESS;
	if (opts & SX_RECURSE)
		flags |= LO_RECURSABLE;
	if (opts & SX_QUIET)
		flags |= LO_QUIET;
	if (opts & SX_NEW)
		flags |= LO_NEW;

	lock_init(&sx->lock_object, &lock_class_sx, description, NULL, flags);
	sx->sx_lock = SX_LOCK_UNLOCKED;
	sx->sx_recurse = 0;
}

void
sx_destroy(struct sx *sx)
{

	KASSERT(sx->sx_lock == SX_LOCK_UNLOCKED, ("sx lock still held"));
	KASSERT(sx->sx_recurse == 0, ("sx lock still recursed"));
	sx->sx_lock = SX_LOCK_DESTROYED;
	lock_destroy(&sx->lock_object);
}

int
sx_try_slock_int(struct sx *sx LOCK_FILE_LINE_ARG_DEF)
{
	uintptr_t x;

	if (SCHEDULER_STOPPED())
		return (1);

	KASSERT(kdb_active != 0 || !TD_IS_IDLETHREAD(curthread),
	    ("sx_try_slock() by idle thread %p on sx %s @ %s:%d",
	    curthread, sx->lock_object.lo_name, file, line));

	x = sx->sx_lock;
	for (;;) {
		KASSERT(x != SX_LOCK_DESTROYED,
		    ("sx_try_slock() of destroyed sx @ %s:%d", file, line));
		if (!(x & SX_LOCK_SHARED))
			break;
		if (atomic_fcmpset_acq_ptr(&sx->sx_lock, &x, x + SX_ONE_SHARER)) {
			LOCK_LOG_TRY("SLOCK", &sx->lock_object, 0, 1, file, line);
			WITNESS_LOCK(&sx->lock_object, LOP_TRYLOCK, file, line);
			LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(sx__acquire,
			    sx, 0, 0, file, line, LOCKSTAT_READER);
			TD_LOCKS_INC(curthread);
			curthread->td_sx_slocks++;
			return (1);
		}
	}

	LOCK_LOG_TRY("SLOCK", &sx->lock_object, 0, 0, file, line);
	return (0);
}

int
sx_try_slock_(struct sx *sx, const char *file, int line)
{

	return (sx_try_slock_int(sx LOCK_FILE_LINE_ARG));
}

int
_sx_xlock(struct sx *sx, int opts, const char *file, int line)
{
	uintptr_t tid, x;
	int error = 0;

	KASSERT(kdb_active != 0 || SCHEDULER_STOPPED() ||
	    !TD_IS_IDLETHREAD(curthread),
	    ("sx_xlock() by idle thread %p on sx %s @ %s:%d",
	    curthread, sx->lock_object.lo_name, file, line));
	KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
	    ("sx_xlock() of destroyed sx @ %s:%d", file, line));
	WITNESS_CHECKORDER(&sx->lock_object, LOP_NEWORDER | LOP_EXCLUSIVE, file,
	    line, NULL);
	tid = (uintptr_t)curthread;
	x = SX_LOCK_UNLOCKED;
	if (!atomic_fcmpset_acq_ptr(&sx->sx_lock, &x, tid))
		error = _sx_xlock_hard(sx, x, opts LOCK_FILE_LINE_ARG);
	else
		LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(sx__acquire, sx,
		    0, 0, file, line, LOCKSTAT_WRITER);
	if (!error) {
		LOCK_LOG_LOCK("XLOCK", &sx->lock_object, 0, sx->sx_recurse,
		    file, line);
		WITNESS_LOCK(&sx->lock_object, LOP_EXCLUSIVE, file, line);
		TD_LOCKS_INC(curthread);
	}

	return (error);
}

int
sx_try_xlock_int(struct sx *sx LOCK_FILE_LINE_ARG_DEF)
{
	struct thread *td;
	uintptr_t tid, x;
	int rval;
	bool recursed;

	td = curthread;
	tid = (uintptr_t)td;
	if (SCHEDULER_STOPPED_TD(td))
		return (1);

	KASSERT(kdb_active != 0 || !TD_IS_IDLETHREAD(td),
	    ("sx_try_xlock() by idle thread %p on sx %s @ %s:%d",
	    curthread, sx->lock_object.lo_name, file, line));
	KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
	    ("sx_try_xlock() of destroyed sx @ %s:%d", file, line));

	rval = 1;
	recursed = false;
	x = SX_LOCK_UNLOCKED;
	for (;;) {
		if (atomic_fcmpset_acq_ptr(&sx->sx_lock, &x, tid))
			break;
		if (x == SX_LOCK_UNLOCKED)
			continue;
		if (x == tid && (sx->lock_object.lo_flags & LO_RECURSABLE)) {
			sx->sx_recurse++;
			atomic_set_ptr(&sx->sx_lock, SX_LOCK_RECURSED);
			break;
		}
		rval = 0;
		break;
	}

	LOCK_LOG_TRY("XLOCK", &sx->lock_object, 0, rval, file, line);
	if (rval) {
		WITNESS_LOCK(&sx->lock_object, LOP_EXCLUSIVE | LOP_TRYLOCK,
		    file, line);
		if (!recursed)
			LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(sx__acquire,
			    sx, 0, 0, file, line, LOCKSTAT_WRITER);
		TD_LOCKS_INC(curthread);
	}

	return (rval);
}

int
sx_try_xlock_(struct sx *sx, const char *file, int line)
{

	return (sx_try_xlock_int(sx LOCK_FILE_LINE_ARG));
}

void
_sx_xunlock(struct sx *sx, const char *file, int line)
{

	KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
	    ("sx_xunlock() of destroyed sx @ %s:%d", file, line));
	_sx_assert(sx, SA_XLOCKED, file, line);
	WITNESS_UNLOCK(&sx->lock_object, LOP_EXCLUSIVE, file, line);
	LOCK_LOG_LOCK("XUNLOCK", &sx->lock_object, 0, sx->sx_recurse, file,
	    line);
#if LOCK_DEBUG > 0
	_sx_xunlock_hard(sx, (uintptr_t)curthread, file, line);
#else
	__sx_xunlock(sx, curthread, file, line);
#endif
	TD_LOCKS_DEC(curthread);
}

/*
 * Try to do a non-blocking upgrade from a shared lock to an exclusive lock.
 * This will only succeed if this thread holds a single shared lock.
 * Return 1 if if the upgrade succeed, 0 otherwise.
 */
int
sx_try_upgrade_int(struct sx *sx LOCK_FILE_LINE_ARG_DEF)
{
	uintptr_t x;
	uintptr_t waiters;
	int success;

	if (SCHEDULER_STOPPED())
		return (1);

	KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
	    ("sx_try_upgrade() of destroyed sx @ %s:%d", file, line));
	_sx_assert(sx, SA_SLOCKED, file, line);

	/*
	 * Try to switch from one shared lock to an exclusive lock.  We need
	 * to maintain the SX_LOCK_EXCLUSIVE_WAITERS flag if set so that
	 * we will wake up the exclusive waiters when we drop the lock.
	 */
	success = 0;
	x = SX_READ_VALUE(sx);
	for (;;) {
		if (SX_SHARERS(x) > 1)
			break;
		waiters = (x & SX_LOCK_WAITERS);
		if (atomic_fcmpset_acq_ptr(&sx->sx_lock, &x,
		    (uintptr_t)curthread | waiters)) {
			success = 1;
			break;
		}
	}
	LOCK_LOG_TRY("XUPGRADE", &sx->lock_object, 0, success, file, line);
	if (success) {
		curthread->td_sx_slocks--;
		WITNESS_UPGRADE(&sx->lock_object, LOP_EXCLUSIVE | LOP_TRYLOCK,
		    file, line);
		LOCKSTAT_RECORD0(sx__upgrade, sx);
	}
	return (success);
}

int
sx_try_upgrade_(struct sx *sx, const char *file, int line)
{

	return (sx_try_upgrade_int(sx LOCK_FILE_LINE_ARG));
}

/*
 * Downgrade an unrecursed exclusive lock into a single shared lock.
 */
void
sx_downgrade_int(struct sx *sx LOCK_FILE_LINE_ARG_DEF)
{
	uintptr_t x;
	int wakeup_swapper;

	if (SCHEDULER_STOPPED())
		return;

	KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
	    ("sx_downgrade() of destroyed sx @ %s:%d", file, line));
	_sx_assert(sx, SA_XLOCKED | SA_NOTRECURSED, file, line);
#ifndef INVARIANTS
	if (sx_recursed(sx))
		panic("downgrade of a recursed lock");
#endif

	WITNESS_DOWNGRADE(&sx->lock_object, 0, file, line);

	/*
	 * Try to switch from an exclusive lock with no shared waiters
	 * to one sharer with no shared waiters.  If there are
	 * exclusive waiters, we don't need to lock the sleep queue so
	 * long as we preserve the flag.  We do one quick try and if
	 * that fails we grab the sleepq lock to keep the flags from
	 * changing and do it the slow way.
	 *
	 * We have to lock the sleep queue if there are shared waiters
	 * so we can wake them up.
	 */
	x = sx->sx_lock;
	if (!(x & SX_LOCK_SHARED_WAITERS) &&
	    atomic_cmpset_rel_ptr(&sx->sx_lock, x, SX_SHARERS_LOCK(1) |
	    (x & SX_LOCK_EXCLUSIVE_WAITERS)))
		goto out;

	/*
	 * Lock the sleep queue so we can read the waiters bits
	 * without any races and wakeup any shared waiters.
	 */
	sleepq_lock(&sx->lock_object);

	/*
	 * Preserve SX_LOCK_EXCLUSIVE_WAITERS while downgraded to a single
	 * shared lock.  If there are any shared waiters, wake them up.
	 */
	wakeup_swapper = 0;
	x = sx->sx_lock;
	atomic_store_rel_ptr(&sx->sx_lock, SX_SHARERS_LOCK(1) |
	    (x & SX_LOCK_EXCLUSIVE_WAITERS));
	if (x & SX_LOCK_SHARED_WAITERS)
		wakeup_swapper = sleepq_broadcast(&sx->lock_object, SLEEPQ_SX,
		    0, SQ_SHARED_QUEUE);
	sleepq_release(&sx->lock_object);

	if (wakeup_swapper)
		kick_proc0();

out:
	curthread->td_sx_slocks++;
	LOCK_LOG_LOCK("XDOWNGRADE", &sx->lock_object, 0, 0, file, line);
	LOCKSTAT_RECORD0(sx__downgrade, sx);
}

void
sx_downgrade_(struct sx *sx, const char *file, int line)
{

	sx_downgrade_int(sx LOCK_FILE_LINE_ARG);
}

#ifdef	ADAPTIVE_SX
static inline void
sx_drop_critical(uintptr_t x, bool *in_critical, int *extra_work)
{

	if (x & SX_LOCK_WRITE_SPINNER)
		return;
	if (*in_critical) {
		critical_exit();
		*in_critical = false;
		(*extra_work)--;
	}
}
#else
#define sx_drop_critical(x, in_critical, extra_work) do { } while(0)
#endif

/*
 * This function represents the so-called 'hard case' for sx_xlock
 * operation.  All 'easy case' failures are redirected to this.  Note
 * that ideally this would be a static function, but it needs to be
 * accessible from at least sx.h.
 */
int
_sx_xlock_hard(struct sx *sx, uintptr_t x, int opts LOCK_FILE_LINE_ARG_DEF)
{
	GIANT_DECLARE;
	uintptr_t tid, setx;
#ifdef ADAPTIVE_SX
	volatile struct thread *owner;
	u_int i, n, spintries = 0;
	enum { READERS, WRITER } sleep_reason = READERS;
	bool in_critical = false;
#endif
#ifdef LOCK_PROFILING
	uint64_t waittime = 0;
	int contested = 0;
#endif
	int error = 0;
#if defined(ADAPTIVE_SX) || defined(KDTRACE_HOOKS)
	struct lock_delay_arg lda;
#endif
#ifdef	KDTRACE_HOOKS
	u_int sleep_cnt = 0;
	int64_t sleep_time = 0;
	int64_t all_time = 0;
#endif
#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	uintptr_t state = 0;
	int doing_lockprof = 0;
#endif
	int extra_work = 0;

	tid = (uintptr_t)curthread;

#ifdef KDTRACE_HOOKS
	if (LOCKSTAT_PROFILE_ENABLED(sx__acquire)) {
		while (x == SX_LOCK_UNLOCKED) {
			if (atomic_fcmpset_acq_ptr(&sx->sx_lock, &x, tid))
				goto out_lockstat;
		}
		extra_work = 1;
		doing_lockprof = 1;
		all_time -= lockstat_nsecs(&sx->lock_object);
		state = x;
	}
#endif
#ifdef LOCK_PROFILING
	extra_work = 1;
	doing_lockprof = 1;
	state = x;
#endif

	if (SCHEDULER_STOPPED())
		return (0);

#if defined(ADAPTIVE_SX)
	lock_delay_arg_init(&lda, &sx_delay);
#elif defined(KDTRACE_HOOKS)
	lock_delay_arg_init(&lda, NULL);
#endif

	if (__predict_false(x == SX_LOCK_UNLOCKED))
		x = SX_READ_VALUE(sx);

	/* If we already hold an exclusive lock, then recurse. */
	if (__predict_false(lv_sx_owner(x) == (struct thread *)tid)) {
		KASSERT((sx->lock_object.lo_flags & LO_RECURSABLE) != 0,
	    ("_sx_xlock_hard: recursed on non-recursive sx %s @ %s:%d\n",
		    sx->lock_object.lo_name, file, line));
		sx->sx_recurse++;
		atomic_set_ptr(&sx->sx_lock, SX_LOCK_RECURSED);
		if (LOCK_LOG_TEST(&sx->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p recursing", __func__, sx);
		return (0);
	}

	if (LOCK_LOG_TEST(&sx->lock_object, 0))
		CTR5(KTR_LOCK, "%s: %s contested (lock=%p) at %s:%d", __func__,
		    sx->lock_object.lo_name, (void *)sx->sx_lock, file, line);

#ifdef HWPMC_HOOKS
	PMC_SOFT_CALL( , , lock, failed);
#endif
	lock_profile_obtain_lock_failed(&sx->lock_object, &contested,
	    &waittime);

#ifndef INVARIANTS
	GIANT_SAVE(extra_work);
#endif

	for (;;) {
		if (x == SX_LOCK_UNLOCKED) {
			if (atomic_fcmpset_acq_ptr(&sx->sx_lock, &x, tid))
				break;
			continue;
		}
#ifdef INVARIANTS
		GIANT_SAVE(extra_work);
#endif
#ifdef KDTRACE_HOOKS
		lda.spin_cnt++;
#endif
#ifdef ADAPTIVE_SX
		/*
		 * If the lock is write locked and the owner is
		 * running on another CPU, spin until the owner stops
		 * running or the state of the lock changes.
		 */
		if ((x & SX_LOCK_SHARED) == 0) {
			sx_drop_critical(x, &in_critical, &extra_work);
			sleep_reason = WRITER;
			owner = lv_sx_owner(x);
			if (!TD_IS_RUNNING(owner))
				goto sleepq;
			if (LOCK_LOG_TEST(&sx->lock_object, 0))
				CTR3(KTR_LOCK, "%s: spinning on %p held by %p",
				    __func__, sx, owner);
			KTR_STATE1(KTR_SCHED, "thread", sched_tdname(curthread),
			    "spinning", "lockname:\"%s\"",
			    sx->lock_object.lo_name);
			do {
				lock_delay(&lda);
				x = SX_READ_VALUE(sx);
				owner = lv_sx_owner(x);
			} while (owner != NULL && TD_IS_RUNNING(owner));
			KTR_STATE0(KTR_SCHED, "thread", sched_tdname(curthread),
			    "running");
			continue;
		} else if (SX_SHARERS(x) > 0) {
			sleep_reason = READERS;
			if (spintries == asx_retries)
				goto sleepq;
			if (!(x & SX_LOCK_WRITE_SPINNER)) {
				if (!in_critical) {
					critical_enter();
					in_critical = true;
					extra_work++;
				}
				if (!atomic_fcmpset_ptr(&sx->sx_lock, &x,
				    x | SX_LOCK_WRITE_SPINNER)) {
					critical_exit();
					in_critical = false;
					extra_work--;
					continue;
				}
			}
			spintries++;
			KTR_STATE1(KTR_SCHED, "thread", sched_tdname(curthread),
			    "spinning", "lockname:\"%s\"",
			    sx->lock_object.lo_name);
			n = SX_SHARERS(x);
			for (i = 0; i < asx_loops; i += n) {
				lock_delay_spin(n);
				x = SX_READ_VALUE(sx);
				if (!(x & SX_LOCK_WRITE_SPINNER))
					break;
				if (!(x & SX_LOCK_SHARED))
					break;
				n = SX_SHARERS(x);
				if (n == 0)
					break;
			}
#ifdef KDTRACE_HOOKS
			lda.spin_cnt += i;
#endif
			KTR_STATE0(KTR_SCHED, "thread", sched_tdname(curthread),
			    "running");
			if (i < asx_loops)
				continue;
		}
sleepq:
#endif
		sleepq_lock(&sx->lock_object);
		x = SX_READ_VALUE(sx);
retry_sleepq:

		/*
		 * If the lock was released while spinning on the
		 * sleep queue chain lock, try again.
		 */
		if (x == SX_LOCK_UNLOCKED) {
			sleepq_release(&sx->lock_object);
			sx_drop_critical(x, &in_critical, &extra_work);
			continue;
		}

#ifdef ADAPTIVE_SX
		/*
		 * The current lock owner might have started executing
		 * on another CPU (or the lock could have changed
		 * owners) while we were waiting on the sleep queue
		 * chain lock.  If so, drop the sleep queue lock and try
		 * again.
		 */
		if (!(x & SX_LOCK_SHARED)) {
			owner = (struct thread *)SX_OWNER(x);
			if (TD_IS_RUNNING(owner)) {
				sleepq_release(&sx->lock_object);
				sx_drop_critical(x, &in_critical,
				    &extra_work);
				continue;
			}
		} else if (SX_SHARERS(x) > 0 && sleep_reason == WRITER) {
			sleepq_release(&sx->lock_object);
			sx_drop_critical(x, &in_critical, &extra_work);
			continue;
		}
#endif

		/*
		 * If an exclusive lock was released with both shared
		 * and exclusive waiters and a shared waiter hasn't
		 * woken up and acquired the lock yet, sx_lock will be
		 * set to SX_LOCK_UNLOCKED | SX_LOCK_EXCLUSIVE_WAITERS.
		 * If we see that value, try to acquire it once.  Note
		 * that we have to preserve SX_LOCK_EXCLUSIVE_WAITERS
		 * as there are other exclusive waiters still.  If we
		 * fail, restart the loop.
		 */
		setx = x & (SX_LOCK_WAITERS | SX_LOCK_WRITE_SPINNER);
		if ((x & ~setx) == SX_LOCK_SHARED) {
			setx &= ~SX_LOCK_WRITE_SPINNER;
			if (!atomic_fcmpset_acq_ptr(&sx->sx_lock, &x, tid | setx))
				goto retry_sleepq;
			sleepq_release(&sx->lock_object);
			CTR2(KTR_LOCK, "%s: %p claimed by new writer",
			    __func__, sx);
			break;
		}

#ifdef ADAPTIVE_SX
		/*
		 * It is possible we set the SX_LOCK_WRITE_SPINNER bit.
		 * It is an invariant that when the bit is set, there is
		 * a writer ready to grab the lock. Thus clear the bit since
		 * we are going to sleep.
		 */
		if (in_critical) {
			if ((x & SX_LOCK_WRITE_SPINNER) ||
			    !((x & SX_LOCK_EXCLUSIVE_WAITERS))) {
				setx = x & ~SX_LOCK_WRITE_SPINNER;
				setx |= SX_LOCK_EXCLUSIVE_WAITERS;
				if (!atomic_fcmpset_ptr(&sx->sx_lock, &x,
				    setx)) {
					goto retry_sleepq;
				}
			}
			critical_exit();
			in_critical = false;
		} else {
#endif
			/*
			 * Try to set the SX_LOCK_EXCLUSIVE_WAITERS.  If we fail,
			 * than loop back and retry.
			 */
			if (!(x & SX_LOCK_EXCLUSIVE_WAITERS)) {
				if (!atomic_fcmpset_ptr(&sx->sx_lock, &x,
				    x | SX_LOCK_EXCLUSIVE_WAITERS)) {
					goto retry_sleepq;
				}
				if (LOCK_LOG_TEST(&sx->lock_object, 0))
					CTR2(KTR_LOCK, "%s: %p set excl waiters flag",
					    __func__, sx);
			}
#ifdef ADAPTIVE_SX
		}
#endif

		/*
		 * Since we have been unable to acquire the exclusive
		 * lock and the exclusive waiters flag is set, we have
		 * to sleep.
		 */
		if (LOCK_LOG_TEST(&sx->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p blocking on sleep queue",
			    __func__, sx);

#ifdef KDTRACE_HOOKS
		sleep_time -= lockstat_nsecs(&sx->lock_object);
#endif
		sleepq_add(&sx->lock_object, NULL, sx->lock_object.lo_name,
		    SLEEPQ_SX | ((opts & SX_INTERRUPTIBLE) ?
		    SLEEPQ_INTERRUPTIBLE : 0), SQ_EXCLUSIVE_QUEUE);
		if (!(opts & SX_INTERRUPTIBLE))
			sleepq_wait(&sx->lock_object, 0);
		else
			error = sleepq_wait_sig(&sx->lock_object, 0);
#ifdef KDTRACE_HOOKS
		sleep_time += lockstat_nsecs(&sx->lock_object);
		sleep_cnt++;
#endif
		if (error) {
			if (LOCK_LOG_TEST(&sx->lock_object, 0))
				CTR2(KTR_LOCK,
			"%s: interruptible sleep by %p suspended by signal",
				    __func__, sx);
			break;
		}
		if (LOCK_LOG_TEST(&sx->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p resuming from sleep queue",
			    __func__, sx);
		x = SX_READ_VALUE(sx);
	}
	if (__predict_true(!extra_work))
		return (error);
#ifdef ADAPTIVE_SX
	if (in_critical)
		critical_exit();
#endif
	GIANT_RESTORE();
#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	if (__predict_true(!doing_lockprof))
		return (error);
#endif
#ifdef KDTRACE_HOOKS
	all_time += lockstat_nsecs(&sx->lock_object);
	if (sleep_time)
		LOCKSTAT_RECORD4(sx__block, sx, sleep_time,
		    LOCKSTAT_WRITER, (state & SX_LOCK_SHARED) == 0,
		    (state & SX_LOCK_SHARED) == 0 ? 0 : SX_SHARERS(state));
	if (lda.spin_cnt > sleep_cnt)
		LOCKSTAT_RECORD4(sx__spin, sx, all_time - sleep_time,
		    LOCKSTAT_WRITER, (state & SX_LOCK_SHARED) == 0,
		    (state & SX_LOCK_SHARED) == 0 ? 0 : SX_SHARERS(state));
out_lockstat:
#endif
	if (!error)
		LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(sx__acquire, sx,
		    contested, waittime, file, line, LOCKSTAT_WRITER);
	return (error);
}

/*
 * This function represents the so-called 'hard case' for sx_xunlock
 * operation.  All 'easy case' failures are redirected to this.  Note
 * that ideally this would be a static function, but it needs to be
 * accessible from at least sx.h.
 */
void
_sx_xunlock_hard(struct sx *sx, uintptr_t x LOCK_FILE_LINE_ARG_DEF)
{
	uintptr_t tid, setx;
	int queue, wakeup_swapper;

	if (SCHEDULER_STOPPED())
		return;

	tid = (uintptr_t)curthread;

	if (__predict_false(x == tid))
		x = SX_READ_VALUE(sx);

	MPASS(!(x & SX_LOCK_SHARED));

	if (__predict_false(x & SX_LOCK_RECURSED)) {
		/* The lock is recursed, unrecurse one level. */
		if ((--sx->sx_recurse) == 0)
			atomic_clear_ptr(&sx->sx_lock, SX_LOCK_RECURSED);
		if (LOCK_LOG_TEST(&sx->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p unrecursing", __func__, sx);
		return;
	}

	LOCKSTAT_PROFILE_RELEASE_RWLOCK(sx__release, sx, LOCKSTAT_WRITER);
	if (x == tid &&
	    atomic_cmpset_rel_ptr(&sx->sx_lock, tid, SX_LOCK_UNLOCKED))
		return;

	if (LOCK_LOG_TEST(&sx->lock_object, 0))
		CTR2(KTR_LOCK, "%s: %p contested", __func__, sx);

	sleepq_lock(&sx->lock_object);
	x = SX_READ_VALUE(sx);
	MPASS(x & (SX_LOCK_SHARED_WAITERS | SX_LOCK_EXCLUSIVE_WAITERS));

	/*
	 * The wake up algorithm here is quite simple and probably not
	 * ideal.  It gives precedence to shared waiters if they are
	 * present.  For this condition, we have to preserve the
	 * state of the exclusive waiters flag.
	 * If interruptible sleeps left the shared queue empty avoid a
	 * starvation for the threads sleeping on the exclusive queue by giving
	 * them precedence and cleaning up the shared waiters bit anyway.
	 */
	setx = SX_LOCK_UNLOCKED;
	queue = SQ_SHARED_QUEUE;
	if ((x & SX_LOCK_EXCLUSIVE_WAITERS) != 0 &&
	    sleepq_sleepcnt(&sx->lock_object, SQ_EXCLUSIVE_QUEUE) != 0) {
		queue = SQ_EXCLUSIVE_QUEUE;
		setx |= (x & SX_LOCK_SHARED_WAITERS);
	}
	atomic_store_rel_ptr(&sx->sx_lock, setx);

	/* Wake up all the waiters for the specific queue. */
	if (LOCK_LOG_TEST(&sx->lock_object, 0))
		CTR3(KTR_LOCK, "%s: %p waking up all threads on %s queue",
		    __func__, sx, queue == SQ_SHARED_QUEUE ? "shared" :
		    "exclusive");

	wakeup_swapper = sleepq_broadcast(&sx->lock_object, SLEEPQ_SX, 0,
	    queue);
	sleepq_release(&sx->lock_object);
	if (wakeup_swapper)
		kick_proc0();
}

static bool __always_inline
__sx_can_read(struct thread *td, uintptr_t x, bool fp)
{

	if ((x & (SX_LOCK_SHARED | SX_LOCK_EXCLUSIVE_WAITERS | SX_LOCK_WRITE_SPINNER))
			== SX_LOCK_SHARED)
		return (true);
	if (!fp && td->td_sx_slocks && (x & SX_LOCK_SHARED))
		return (true);
	return (false);
}

static bool __always_inline
__sx_slock_try(struct sx *sx, struct thread *td, uintptr_t *xp, bool fp
    LOCK_FILE_LINE_ARG_DEF)
{

	/*
	 * If no other thread has an exclusive lock then try to bump up
	 * the count of sharers.  Since we have to preserve the state
	 * of SX_LOCK_EXCLUSIVE_WAITERS, if we fail to acquire the
	 * shared lock loop back and retry.
	 */
	while (__sx_can_read(td, *xp, fp)) {
		if (atomic_fcmpset_acq_ptr(&sx->sx_lock, xp,
		    *xp + SX_ONE_SHARER)) {
			if (LOCK_LOG_TEST(&sx->lock_object, 0))
				CTR4(KTR_LOCK, "%s: %p succeed %p -> %p",
				    __func__, sx, (void *)*xp,
				    (void *)(*xp + SX_ONE_SHARER));
			td->td_sx_slocks++;
			return (true);
		}
	}
	return (false);
}

static int __noinline
_sx_slock_hard(struct sx *sx, int opts, uintptr_t x LOCK_FILE_LINE_ARG_DEF)
{
	GIANT_DECLARE;
	struct thread *td;
#ifdef ADAPTIVE_SX
	volatile struct thread *owner;
	u_int i, n, spintries = 0;
#endif
#ifdef LOCK_PROFILING
	uint64_t waittime = 0;
	int contested = 0;
#endif
	int error = 0;
#if defined(ADAPTIVE_SX) || defined(KDTRACE_HOOKS)
	struct lock_delay_arg lda;
#endif
#ifdef KDTRACE_HOOKS
	u_int sleep_cnt = 0;
	int64_t sleep_time = 0;
	int64_t all_time = 0;
#endif
#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	uintptr_t state = 0;
#endif
	int extra_work = 0;

	td = curthread;

#ifdef KDTRACE_HOOKS
	if (LOCKSTAT_PROFILE_ENABLED(sx__acquire)) {
		if (__sx_slock_try(sx, td, &x, false LOCK_FILE_LINE_ARG))
			goto out_lockstat;
		extra_work = 1;
		all_time -= lockstat_nsecs(&sx->lock_object);
		state = x;
	}
#endif
#ifdef LOCK_PROFILING
	extra_work = 1;
	state = x;
#endif

	if (SCHEDULER_STOPPED())
		return (0);

#if defined(ADAPTIVE_SX)
	lock_delay_arg_init(&lda, &sx_delay);
#elif defined(KDTRACE_HOOKS)
	lock_delay_arg_init(&lda, NULL);
#endif

#ifdef HWPMC_HOOKS
	PMC_SOFT_CALL( , , lock, failed);
#endif
	lock_profile_obtain_lock_failed(&sx->lock_object, &contested,
	    &waittime);

#ifndef INVARIANTS
	GIANT_SAVE(extra_work);
#endif

	/*
	 * As with rwlocks, we don't make any attempt to try to block
	 * shared locks once there is an exclusive waiter.
	 */
	for (;;) {
		if (__sx_slock_try(sx, td, &x, false LOCK_FILE_LINE_ARG))
			break;
#ifdef INVARIANTS
		GIANT_SAVE(extra_work);
#endif
#ifdef KDTRACE_HOOKS
		lda.spin_cnt++;
#endif

#ifdef ADAPTIVE_SX
		/*
		 * If the owner is running on another CPU, spin until
		 * the owner stops running or the state of the lock
		 * changes.
		 */
		if ((x & SX_LOCK_SHARED) == 0) {
			owner = lv_sx_owner(x);
			if (TD_IS_RUNNING(owner)) {
				if (LOCK_LOG_TEST(&sx->lock_object, 0))
					CTR3(KTR_LOCK,
					    "%s: spinning on %p held by %p",
					    __func__, sx, owner);
				KTR_STATE1(KTR_SCHED, "thread",
				    sched_tdname(curthread), "spinning",
				    "lockname:\"%s\"", sx->lock_object.lo_name);
				do {
					lock_delay(&lda);
					x = SX_READ_VALUE(sx);
					owner = lv_sx_owner(x);
				} while (owner != NULL && TD_IS_RUNNING(owner));
				KTR_STATE0(KTR_SCHED, "thread",
				    sched_tdname(curthread), "running");
				continue;
			}
		} else {
			if ((x & SX_LOCK_WRITE_SPINNER) && SX_SHARERS(x) == 0) {
				MPASS(!__sx_can_read(td, x, false));
				lock_delay_spin(2);
				x = SX_READ_VALUE(sx);
				continue;
			}
			if (spintries < asx_retries) {
				KTR_STATE1(KTR_SCHED, "thread", sched_tdname(curthread),
				    "spinning", "lockname:\"%s\"",
				    sx->lock_object.lo_name);
				n = SX_SHARERS(x);
				for (i = 0; i < asx_loops; i += n) {
					lock_delay_spin(n);
					x = SX_READ_VALUE(sx);
					if (!(x & SX_LOCK_SHARED))
						break;
					n = SX_SHARERS(x);
					if (n == 0)
						break;
					if (__sx_can_read(td, x, false))
						break;
				}
#ifdef KDTRACE_HOOKS
				lda.spin_cnt += i;
#endif
				KTR_STATE0(KTR_SCHED, "thread", sched_tdname(curthread),
				    "running");
				if (i < asx_loops)
					continue;
			}
		}
#endif

		/*
		 * Some other thread already has an exclusive lock, so
		 * start the process of blocking.
		 */
		sleepq_lock(&sx->lock_object);
		x = SX_READ_VALUE(sx);
retry_sleepq:
		if (((x & SX_LOCK_WRITE_SPINNER) && SX_SHARERS(x) == 0) ||
		    __sx_can_read(td, x, false)) {
			sleepq_release(&sx->lock_object);
			continue;
		}

#ifdef ADAPTIVE_SX
		/*
		 * If the owner is running on another CPU, spin until
		 * the owner stops running or the state of the lock
		 * changes.
		 */
		if (!(x & SX_LOCK_SHARED)) {
			owner = (struct thread *)SX_OWNER(x);
			if (TD_IS_RUNNING(owner)) {
				sleepq_release(&sx->lock_object);
				x = SX_READ_VALUE(sx);
				continue;
			}
		}
#endif

		/*
		 * Try to set the SX_LOCK_SHARED_WAITERS flag.  If we
		 * fail to set it drop the sleep queue lock and loop
		 * back.
		 */
		if (!(x & SX_LOCK_SHARED_WAITERS)) {
			if (!atomic_fcmpset_ptr(&sx->sx_lock, &x,
			    x | SX_LOCK_SHARED_WAITERS))
				goto retry_sleepq;
			if (LOCK_LOG_TEST(&sx->lock_object, 0))
				CTR2(KTR_LOCK, "%s: %p set shared waiters flag",
				    __func__, sx);
		}

		/*
		 * Since we have been unable to acquire the shared lock,
		 * we have to sleep.
		 */
		if (LOCK_LOG_TEST(&sx->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p blocking on sleep queue",
			    __func__, sx);

#ifdef KDTRACE_HOOKS
		sleep_time -= lockstat_nsecs(&sx->lock_object);
#endif
		sleepq_add(&sx->lock_object, NULL, sx->lock_object.lo_name,
		    SLEEPQ_SX | ((opts & SX_INTERRUPTIBLE) ?
		    SLEEPQ_INTERRUPTIBLE : 0), SQ_SHARED_QUEUE);
		if (!(opts & SX_INTERRUPTIBLE))
			sleepq_wait(&sx->lock_object, 0);
		else
			error = sleepq_wait_sig(&sx->lock_object, 0);
#ifdef KDTRACE_HOOKS
		sleep_time += lockstat_nsecs(&sx->lock_object);
		sleep_cnt++;
#endif
		if (error) {
			if (LOCK_LOG_TEST(&sx->lock_object, 0))
				CTR2(KTR_LOCK,
			"%s: interruptible sleep by %p suspended by signal",
				    __func__, sx);
			break;
		}
		if (LOCK_LOG_TEST(&sx->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p resuming from sleep queue",
			    __func__, sx);
		x = SX_READ_VALUE(sx);
	}
#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	if (__predict_true(!extra_work))
		return (error);
#endif
#ifdef KDTRACE_HOOKS
	all_time += lockstat_nsecs(&sx->lock_object);
	if (sleep_time)
		LOCKSTAT_RECORD4(sx__block, sx, sleep_time,
		    LOCKSTAT_READER, (state & SX_LOCK_SHARED) == 0,
		    (state & SX_LOCK_SHARED) == 0 ? 0 : SX_SHARERS(state));
	if (lda.spin_cnt > sleep_cnt)
		LOCKSTAT_RECORD4(sx__spin, sx, all_time - sleep_time,
		    LOCKSTAT_READER, (state & SX_LOCK_SHARED) == 0,
		    (state & SX_LOCK_SHARED) == 0 ? 0 : SX_SHARERS(state));
out_lockstat:
#endif
	if (error == 0) {
		LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(sx__acquire, sx,
		    contested, waittime, file, line, LOCKSTAT_READER);
	}
	GIANT_RESTORE();
	return (error);
}

int
_sx_slock_int(struct sx *sx, int opts LOCK_FILE_LINE_ARG_DEF)
{
	struct thread *td;
	uintptr_t x;
	int error;

	KASSERT(kdb_active != 0 || SCHEDULER_STOPPED() ||
	    !TD_IS_IDLETHREAD(curthread),
	    ("sx_slock() by idle thread %p on sx %s @ %s:%d",
	    curthread, sx->lock_object.lo_name, file, line));
	KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
	    ("sx_slock() of destroyed sx @ %s:%d", file, line));
	WITNESS_CHECKORDER(&sx->lock_object, LOP_NEWORDER, file, line, NULL);

	error = 0;
	td = curthread;
	x = SX_READ_VALUE(sx);
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(sx__acquire) ||
	    !__sx_slock_try(sx, td, &x, true LOCK_FILE_LINE_ARG)))
		error = _sx_slock_hard(sx, opts, x LOCK_FILE_LINE_ARG);
	else
		lock_profile_obtain_lock_success(&sx->lock_object, 0, 0,
		    file, line);
	if (error == 0) {
		LOCK_LOG_LOCK("SLOCK", &sx->lock_object, 0, 0, file, line);
		WITNESS_LOCK(&sx->lock_object, 0, file, line);
		TD_LOCKS_INC(curthread);
	}
	return (error);
}

int
_sx_slock(struct sx *sx, int opts, const char *file, int line)
{

	return (_sx_slock_int(sx, opts LOCK_FILE_LINE_ARG));
}

static bool __always_inline
_sx_sunlock_try(struct sx *sx, struct thread *td, uintptr_t *xp)
{

	for (;;) {
		if (SX_SHARERS(*xp) > 1 || !(*xp & SX_LOCK_WAITERS)) {
			if (atomic_fcmpset_rel_ptr(&sx->sx_lock, xp,
			    *xp - SX_ONE_SHARER)) {
				if (LOCK_LOG_TEST(&sx->lock_object, 0))
					CTR4(KTR_LOCK,
					    "%s: %p succeeded %p -> %p",
					    __func__, sx, (void *)*xp,
					    (void *)(*xp - SX_ONE_SHARER));
				td->td_sx_slocks--;
				return (true);
			}
			continue;
		}
		break;
	}
	return (false);
}

static void __noinline
_sx_sunlock_hard(struct sx *sx, struct thread *td, uintptr_t x
    LOCK_FILE_LINE_ARG_DEF)
{
	int wakeup_swapper = 0;
	uintptr_t setx, queue;

	if (SCHEDULER_STOPPED())
		return;

	if (_sx_sunlock_try(sx, td, &x))
		goto out_lockstat;

	sleepq_lock(&sx->lock_object);
	x = SX_READ_VALUE(sx);
	for (;;) {
		if (_sx_sunlock_try(sx, td, &x))
			break;

		/*
		 * Wake up semantic here is quite simple:
		 * Just wake up all the exclusive waiters.
		 * Note that the state of the lock could have changed,
		 * so if it fails loop back and retry.
		 */
		setx = SX_LOCK_UNLOCKED;
		queue = SQ_SHARED_QUEUE;
		if (x & SX_LOCK_EXCLUSIVE_WAITERS) {
			setx |= (x & SX_LOCK_SHARED_WAITERS);
			queue = SQ_EXCLUSIVE_QUEUE;
		}
		setx |= (x & SX_LOCK_WRITE_SPINNER);
		if (!atomic_fcmpset_rel_ptr(&sx->sx_lock, &x, setx))
			continue;
		if (LOCK_LOG_TEST(&sx->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p waking up all thread on"
			    "exclusive queue", __func__, sx);
		wakeup_swapper = sleepq_broadcast(&sx->lock_object, SLEEPQ_SX,
		    0, queue);
		td->td_sx_slocks--;
		break;
	}
	sleepq_release(&sx->lock_object);
	if (wakeup_swapper)
		kick_proc0();
out_lockstat:
	LOCKSTAT_PROFILE_RELEASE_RWLOCK(sx__release, sx, LOCKSTAT_READER);
}

void
_sx_sunlock_int(struct sx *sx LOCK_FILE_LINE_ARG_DEF)
{
	struct thread *td;
	uintptr_t x;

	KASSERT(sx->sx_lock != SX_LOCK_DESTROYED,
	    ("sx_sunlock() of destroyed sx @ %s:%d", file, line));
	_sx_assert(sx, SA_SLOCKED, file, line);
	WITNESS_UNLOCK(&sx->lock_object, 0, file, line);
	LOCK_LOG_LOCK("SUNLOCK", &sx->lock_object, 0, 0, file, line);

	td = curthread;
	x = SX_READ_VALUE(sx);
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(sx__release) ||
	    !_sx_sunlock_try(sx, td, &x)))
		_sx_sunlock_hard(sx, td, x LOCK_FILE_LINE_ARG);
	else
		lock_profile_release_lock(&sx->lock_object);

	TD_LOCKS_DEC(curthread);
}

void
_sx_sunlock(struct sx *sx, const char *file, int line)
{

	_sx_sunlock_int(sx LOCK_FILE_LINE_ARG);
}

#ifdef INVARIANT_SUPPORT
#ifndef INVARIANTS
#undef	_sx_assert
#endif

/*
 * In the non-WITNESS case, sx_assert() can only detect that at least
 * *some* thread owns an slock, but it cannot guarantee that *this*
 * thread owns an slock.
 */
void
_sx_assert(const struct sx *sx, int what, const char *file, int line)
{
#ifndef WITNESS
	int slocked = 0;
#endif

	if (SCHEDULER_STOPPED())
		return;
	switch (what) {
	case SA_SLOCKED:
	case SA_SLOCKED | SA_NOTRECURSED:
	case SA_SLOCKED | SA_RECURSED:
#ifndef WITNESS
		slocked = 1;
		/* FALLTHROUGH */
#endif
	case SA_LOCKED:
	case SA_LOCKED | SA_NOTRECURSED:
	case SA_LOCKED | SA_RECURSED:
#ifdef WITNESS
		witness_assert(&sx->lock_object, what, file, line);
#else
		/*
		 * If some other thread has an exclusive lock or we
		 * have one and are asserting a shared lock, fail.
		 * Also, if no one has a lock at all, fail.
		 */
		if (sx->sx_lock == SX_LOCK_UNLOCKED ||
		    (!(sx->sx_lock & SX_LOCK_SHARED) && (slocked ||
		    sx_xholder(sx) != curthread)))
			panic("Lock %s not %slocked @ %s:%d\n",
			    sx->lock_object.lo_name, slocked ? "share " : "",
			    file, line);

		if (!(sx->sx_lock & SX_LOCK_SHARED)) {
			if (sx_recursed(sx)) {
				if (what & SA_NOTRECURSED)
					panic("Lock %s recursed @ %s:%d\n",
					    sx->lock_object.lo_name, file,
					    line);
			} else if (what & SA_RECURSED)
				panic("Lock %s not recursed @ %s:%d\n",
				    sx->lock_object.lo_name, file, line);
		}
#endif
		break;
	case SA_XLOCKED:
	case SA_XLOCKED | SA_NOTRECURSED:
	case SA_XLOCKED | SA_RECURSED:
		if (sx_xholder(sx) != curthread)
			panic("Lock %s not exclusively locked @ %s:%d\n",
			    sx->lock_object.lo_name, file, line);
		if (sx_recursed(sx)) {
			if (what & SA_NOTRECURSED)
				panic("Lock %s recursed @ %s:%d\n",
				    sx->lock_object.lo_name, file, line);
		} else if (what & SA_RECURSED)
			panic("Lock %s not recursed @ %s:%d\n",
			    sx->lock_object.lo_name, file, line);
		break;
	case SA_UNLOCKED:
#ifdef WITNESS
		witness_assert(&sx->lock_object, what, file, line);
#else
		/*
		 * If we hold an exclusve lock fail.  We can't
		 * reliably check to see if we hold a shared lock or
		 * not.
		 */
		if (sx_xholder(sx) == curthread)
			panic("Lock %s exclusively locked @ %s:%d\n",
			    sx->lock_object.lo_name, file, line);
#endif
		break;
	default:
		panic("Unknown sx lock assertion: %d @ %s:%d", what, file,
		    line);
	}
}
#endif	/* INVARIANT_SUPPORT */

#ifdef DDB
static void
db_show_sx(const struct lock_object *lock)
{
	struct thread *td;
	const struct sx *sx;

	sx = (const struct sx *)lock;

	db_printf(" state: ");
	if (sx->sx_lock == SX_LOCK_UNLOCKED)
		db_printf("UNLOCKED\n");
	else if (sx->sx_lock == SX_LOCK_DESTROYED) {
		db_printf("DESTROYED\n");
		return;
	} else if (sx->sx_lock & SX_LOCK_SHARED)
		db_printf("SLOCK: %ju\n", (uintmax_t)SX_SHARERS(sx->sx_lock));
	else {
		td = sx_xholder(sx);
		db_printf("XLOCK: %p (tid %d, pid %d, \"%s\")\n", td,
		    td->td_tid, td->td_proc->p_pid, td->td_name);
		if (sx_recursed(sx))
			db_printf(" recursed: %d\n", sx->sx_recurse);
	}

	db_printf(" waiters: ");
	switch(sx->sx_lock &
	    (SX_LOCK_SHARED_WAITERS | SX_LOCK_EXCLUSIVE_WAITERS)) {
	case SX_LOCK_SHARED_WAITERS:
		db_printf("shared\n");
		break;
	case SX_LOCK_EXCLUSIVE_WAITERS:
		db_printf("exclusive\n");
		break;
	case SX_LOCK_SHARED_WAITERS | SX_LOCK_EXCLUSIVE_WAITERS:
		db_printf("exclusive and shared\n");
		break;
	default:
		db_printf("none\n");
	}
}

/*
 * Check to see if a thread that is blocked on a sleep queue is actually
 * blocked on an sx lock.  If so, output some details and return true.
 * If the lock has an exclusive owner, return that in *ownerp.
 */
int
sx_chain(struct thread *td, struct thread **ownerp)
{
	struct sx *sx;

	/*
	 * Check to see if this thread is blocked on an sx lock.
	 * First, we check the lock class.  If that is ok, then we
	 * compare the lock name against the wait message.
	 */
	sx = td->td_wchan;
	if (LOCK_CLASS(&sx->lock_object) != &lock_class_sx ||
	    sx->lock_object.lo_name != td->td_wmesg)
		return (0);

	/* We think we have an sx lock, so output some details. */
	db_printf("blocked on sx \"%s\" ", td->td_wmesg);
	*ownerp = sx_xholder(sx);
	if (sx->sx_lock & SX_LOCK_SHARED)
		db_printf("SLOCK (count %ju)\n",
		    (uintmax_t)SX_SHARERS(sx->sx_lock));
	else
		db_printf("XLOCK\n");
	return (1);
}
#endif
