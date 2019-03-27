/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Machine independent bits of reader/writer lock implementation.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_hwpmc_hooks.h"
#include "opt_no_adaptive_rwlocks.h"

#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/ktr.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/turnstile.h>

#include <machine/cpu.h>

#if defined(SMP) && !defined(NO_ADAPTIVE_RWLOCKS)
#define	ADAPTIVE_RWLOCKS
#endif

#ifdef HWPMC_HOOKS
#include <sys/pmckern.h>
PMC_SOFT_DECLARE( , , lock, failed);
#endif

/*
 * Return the rwlock address when the lock cookie address is provided.
 * This functionality assumes that struct rwlock* have a member named rw_lock.
 */
#define	rwlock2rw(c)	(__containerof(c, struct rwlock, rw_lock))

#ifdef DDB
#include <ddb/ddb.h>

static void	db_show_rwlock(const struct lock_object *lock);
#endif
static void	assert_rw(const struct lock_object *lock, int what);
static void	lock_rw(struct lock_object *lock, uintptr_t how);
#ifdef KDTRACE_HOOKS
static int	owner_rw(const struct lock_object *lock, struct thread **owner);
#endif
static uintptr_t unlock_rw(struct lock_object *lock);

struct lock_class lock_class_rw = {
	.lc_name = "rw",
	.lc_flags = LC_SLEEPLOCK | LC_RECURSABLE | LC_UPGRADABLE,
	.lc_assert = assert_rw,
#ifdef DDB
	.lc_ddb_show = db_show_rwlock,
#endif
	.lc_lock = lock_rw,
	.lc_unlock = unlock_rw,
#ifdef KDTRACE_HOOKS
	.lc_owner = owner_rw,
#endif
};

#ifdef ADAPTIVE_RWLOCKS
static int __read_frequently rowner_retries;
static int __read_frequently rowner_loops;
static SYSCTL_NODE(_debug, OID_AUTO, rwlock, CTLFLAG_RD, NULL,
    "rwlock debugging");
SYSCTL_INT(_debug_rwlock, OID_AUTO, retry, CTLFLAG_RW, &rowner_retries, 0, "");
SYSCTL_INT(_debug_rwlock, OID_AUTO, loops, CTLFLAG_RW, &rowner_loops, 0, "");

static struct lock_delay_config __read_frequently rw_delay;

SYSCTL_INT(_debug_rwlock, OID_AUTO, delay_base, CTLFLAG_RW, &rw_delay.base,
    0, "");
SYSCTL_INT(_debug_rwlock, OID_AUTO, delay_max, CTLFLAG_RW, &rw_delay.max,
    0, "");

static void
rw_lock_delay_init(void *arg __unused)
{

	lock_delay_default_init(&rw_delay);
	rowner_retries = 10;
	rowner_loops = max(10000, rw_delay.max);
}
LOCK_DELAY_SYSINIT(rw_lock_delay_init);
#endif

/*
 * Return a pointer to the owning thread if the lock is write-locked or
 * NULL if the lock is unlocked or read-locked.
 */

#define	lv_rw_wowner(v)							\
	((v) & RW_LOCK_READ ? NULL :					\
	 (struct thread *)RW_OWNER((v)))

#define	rw_wowner(rw)	lv_rw_wowner(RW_READ_VALUE(rw))

/*
 * Returns if a write owner is recursed.  Write ownership is not assured
 * here and should be previously checked.
 */
#define	rw_recursed(rw)		((rw)->rw_recurse != 0)

/*
 * Return true if curthread helds the lock.
 */
#define	rw_wlocked(rw)		(rw_wowner((rw)) == curthread)

/*
 * Return a pointer to the owning thread for this lock who should receive
 * any priority lent by threads that block on this lock.  Currently this
 * is identical to rw_wowner().
 */
#define	rw_owner(rw)		rw_wowner(rw)

#ifndef INVARIANTS
#define	__rw_assert(c, what, file, line)
#endif

void
assert_rw(const struct lock_object *lock, int what)
{

	rw_assert((const struct rwlock *)lock, what);
}

void
lock_rw(struct lock_object *lock, uintptr_t how)
{
	struct rwlock *rw;

	rw = (struct rwlock *)lock;
	if (how)
		rw_rlock(rw);
	else
		rw_wlock(rw);
}

uintptr_t
unlock_rw(struct lock_object *lock)
{
	struct rwlock *rw;

	rw = (struct rwlock *)lock;
	rw_assert(rw, RA_LOCKED | LA_NOTRECURSED);
	if (rw->rw_lock & RW_LOCK_READ) {
		rw_runlock(rw);
		return (1);
	} else {
		rw_wunlock(rw);
		return (0);
	}
}

#ifdef KDTRACE_HOOKS
int
owner_rw(const struct lock_object *lock, struct thread **owner)
{
	const struct rwlock *rw = (const struct rwlock *)lock;
	uintptr_t x = rw->rw_lock;

	*owner = rw_wowner(rw);
	return ((x & RW_LOCK_READ) != 0 ?  (RW_READERS(x) != 0) :
	    (*owner != NULL));
}
#endif

void
_rw_init_flags(volatile uintptr_t *c, const char *name, int opts)
{
	struct rwlock *rw;
	int flags;

	rw = rwlock2rw(c);

	MPASS((opts & ~(RW_DUPOK | RW_NOPROFILE | RW_NOWITNESS | RW_QUIET |
	    RW_RECURSE | RW_NEW)) == 0);
	ASSERT_ATOMIC_LOAD_PTR(rw->rw_lock,
	    ("%s: rw_lock not aligned for %s: %p", __func__, name,
	    &rw->rw_lock));

	flags = LO_UPGRADABLE;
	if (opts & RW_DUPOK)
		flags |= LO_DUPOK;
	if (opts & RW_NOPROFILE)
		flags |= LO_NOPROFILE;
	if (!(opts & RW_NOWITNESS))
		flags |= LO_WITNESS;
	if (opts & RW_RECURSE)
		flags |= LO_RECURSABLE;
	if (opts & RW_QUIET)
		flags |= LO_QUIET;
	if (opts & RW_NEW)
		flags |= LO_NEW;

	lock_init(&rw->lock_object, &lock_class_rw, name, NULL, flags);
	rw->rw_lock = RW_UNLOCKED;
	rw->rw_recurse = 0;
}

void
_rw_destroy(volatile uintptr_t *c)
{
	struct rwlock *rw;

	rw = rwlock2rw(c);

	KASSERT(rw->rw_lock == RW_UNLOCKED, ("rw lock %p not unlocked", rw));
	KASSERT(rw->rw_recurse == 0, ("rw lock %p still recursed", rw));
	rw->rw_lock = RW_DESTROYED;
	lock_destroy(&rw->lock_object);
}

void
rw_sysinit(void *arg)
{
	struct rw_args *args;

	args = arg;
	rw_init_flags((struct rwlock *)args->ra_rw, args->ra_desc,
	    args->ra_flags);
}

int
_rw_wowned(const volatile uintptr_t *c)
{

	return (rw_wowner(rwlock2rw(c)) == curthread);
}

void
_rw_wlock_cookie(volatile uintptr_t *c, const char *file, int line)
{
	struct rwlock *rw;
	uintptr_t tid, v;

	rw = rwlock2rw(c);

	KASSERT(kdb_active != 0 || SCHEDULER_STOPPED() ||
	    !TD_IS_IDLETHREAD(curthread),
	    ("rw_wlock() by idle thread %p on rwlock %s @ %s:%d",
	    curthread, rw->lock_object.lo_name, file, line));
	KASSERT(rw->rw_lock != RW_DESTROYED,
	    ("rw_wlock() of destroyed rwlock @ %s:%d", file, line));
	WITNESS_CHECKORDER(&rw->lock_object, LOP_NEWORDER | LOP_EXCLUSIVE, file,
	    line, NULL);
	tid = (uintptr_t)curthread;
	v = RW_UNLOCKED;
	if (!_rw_write_lock_fetch(rw, &v, tid))
		_rw_wlock_hard(rw, v, file, line);
	else
		LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(rw__acquire, rw,
		    0, 0, file, line, LOCKSTAT_WRITER);

	LOCK_LOG_LOCK("WLOCK", &rw->lock_object, 0, rw->rw_recurse, file, line);
	WITNESS_LOCK(&rw->lock_object, LOP_EXCLUSIVE, file, line);
	TD_LOCKS_INC(curthread);
}

int
__rw_try_wlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF)
{
	struct thread *td;
	uintptr_t tid, v;
	int rval;
	bool recursed;

	td = curthread;
	tid = (uintptr_t)td;
	if (SCHEDULER_STOPPED_TD(td))
		return (1);

	KASSERT(kdb_active != 0 || !TD_IS_IDLETHREAD(td),
	    ("rw_try_wlock() by idle thread %p on rwlock %s @ %s:%d",
	    curthread, rw->lock_object.lo_name, file, line));
	KASSERT(rw->rw_lock != RW_DESTROYED,
	    ("rw_try_wlock() of destroyed rwlock @ %s:%d", file, line));

	rval = 1;
	recursed = false;
	v = RW_UNLOCKED;
	for (;;) {
		if (atomic_fcmpset_acq_ptr(&rw->rw_lock, &v, tid))
			break;
		if (v == RW_UNLOCKED)
			continue;
		if (v == tid && (rw->lock_object.lo_flags & LO_RECURSABLE)) {
			rw->rw_recurse++;
			atomic_set_ptr(&rw->rw_lock, RW_LOCK_WRITER_RECURSED);
			break;
		}
		rval = 0;
		break;
	}

	LOCK_LOG_TRY("WLOCK", &rw->lock_object, 0, rval, file, line);
	if (rval) {
		WITNESS_LOCK(&rw->lock_object, LOP_EXCLUSIVE | LOP_TRYLOCK,
		    file, line);
		if (!recursed)
			LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(rw__acquire,
			    rw, 0, 0, file, line, LOCKSTAT_WRITER);
		TD_LOCKS_INC(curthread);
	}
	return (rval);
}

int
__rw_try_wlock(volatile uintptr_t *c, const char *file, int line)
{
	struct rwlock *rw;

	rw = rwlock2rw(c);
	return (__rw_try_wlock_int(rw LOCK_FILE_LINE_ARG));
}

void
_rw_wunlock_cookie(volatile uintptr_t *c, const char *file, int line)
{
	struct rwlock *rw;

	rw = rwlock2rw(c);

	KASSERT(rw->rw_lock != RW_DESTROYED,
	    ("rw_wunlock() of destroyed rwlock @ %s:%d", file, line));
	__rw_assert(c, RA_WLOCKED, file, line);
	WITNESS_UNLOCK(&rw->lock_object, LOP_EXCLUSIVE, file, line);
	LOCK_LOG_LOCK("WUNLOCK", &rw->lock_object, 0, rw->rw_recurse, file,
	    line);

#ifdef LOCK_PROFILING
	_rw_wunlock_hard(rw, (uintptr_t)curthread, file, line);
#else
	__rw_wunlock(rw, curthread, file, line);
#endif

	TD_LOCKS_DEC(curthread);
}

/*
 * Determines whether a new reader can acquire a lock.  Succeeds if the
 * reader already owns a read lock and the lock is locked for read to
 * prevent deadlock from reader recursion.  Also succeeds if the lock
 * is unlocked and has no writer waiters or spinners.  Failing otherwise
 * prioritizes writers before readers.
 */
static bool __always_inline
__rw_can_read(struct thread *td, uintptr_t v, bool fp)
{

	if ((v & (RW_LOCK_READ | RW_LOCK_WRITE_WAITERS | RW_LOCK_WRITE_SPINNER))
	    == RW_LOCK_READ)
		return (true);
	if (!fp && td->td_rw_rlocks && (v & RW_LOCK_READ))
		return (true);
	return (false);
}

static bool __always_inline
__rw_rlock_try(struct rwlock *rw, struct thread *td, uintptr_t *vp, bool fp
    LOCK_FILE_LINE_ARG_DEF)
{

	/*
	 * Handle the easy case.  If no other thread has a write
	 * lock, then try to bump up the count of read locks.  Note
	 * that we have to preserve the current state of the
	 * RW_LOCK_WRITE_WAITERS flag.  If we fail to acquire a
	 * read lock, then rw_lock must have changed, so restart
	 * the loop.  Note that this handles the case of a
	 * completely unlocked rwlock since such a lock is encoded
	 * as a read lock with no waiters.
	 */
	while (__rw_can_read(td, *vp, fp)) {
		if (atomic_fcmpset_acq_ptr(&rw->rw_lock, vp,
			*vp + RW_ONE_READER)) {
			if (LOCK_LOG_TEST(&rw->lock_object, 0))
				CTR4(KTR_LOCK,
				    "%s: %p succeed %p -> %p", __func__,
				    rw, (void *)*vp,
				    (void *)(*vp + RW_ONE_READER));
			td->td_rw_rlocks++;
			return (true);
		}
	}
	return (false);
}

static void __noinline
__rw_rlock_hard(struct rwlock *rw, struct thread *td, uintptr_t v
    LOCK_FILE_LINE_ARG_DEF)
{
	struct turnstile *ts;
	struct thread *owner;
#ifdef ADAPTIVE_RWLOCKS
	int spintries = 0;
	int i, n;
#endif
#ifdef LOCK_PROFILING
	uint64_t waittime = 0;
	int contested = 0;
#endif
#if defined(ADAPTIVE_RWLOCKS) || defined(KDTRACE_HOOKS)
	struct lock_delay_arg lda;
#endif
#ifdef KDTRACE_HOOKS
	u_int sleep_cnt = 0;
	int64_t sleep_time = 0;
	int64_t all_time = 0;
#endif
#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	uintptr_t state = 0;
	int doing_lockprof = 0;
#endif

#ifdef KDTRACE_HOOKS
	if (LOCKSTAT_PROFILE_ENABLED(rw__acquire)) {
		if (__rw_rlock_try(rw, td, &v, false LOCK_FILE_LINE_ARG))
			goto out_lockstat;
		doing_lockprof = 1;
		all_time -= lockstat_nsecs(&rw->lock_object);
		state = v;
	}
#endif
#ifdef LOCK_PROFILING
	doing_lockprof = 1;
	state = v;
#endif

	if (SCHEDULER_STOPPED())
		return;

#if defined(ADAPTIVE_RWLOCKS)
	lock_delay_arg_init(&lda, &rw_delay);
#elif defined(KDTRACE_HOOKS)
	lock_delay_arg_init(&lda, NULL);
#endif

#ifdef HWPMC_HOOKS
	PMC_SOFT_CALL( , , lock, failed);
#endif
	lock_profile_obtain_lock_failed(&rw->lock_object,
	    &contested, &waittime);

	for (;;) {
		if (__rw_rlock_try(rw, td, &v, false LOCK_FILE_LINE_ARG))
			break;
#ifdef KDTRACE_HOOKS
		lda.spin_cnt++;
#endif

#ifdef ADAPTIVE_RWLOCKS
		/*
		 * If the owner is running on another CPU, spin until
		 * the owner stops running or the state of the lock
		 * changes.
		 */
		if ((v & RW_LOCK_READ) == 0) {
			owner = (struct thread *)RW_OWNER(v);
			if (TD_IS_RUNNING(owner)) {
				if (LOCK_LOG_TEST(&rw->lock_object, 0))
					CTR3(KTR_LOCK,
					    "%s: spinning on %p held by %p",
					    __func__, rw, owner);
				KTR_STATE1(KTR_SCHED, "thread",
				    sched_tdname(curthread), "spinning",
				    "lockname:\"%s\"", rw->lock_object.lo_name);
				do {
					lock_delay(&lda);
					v = RW_READ_VALUE(rw);
					owner = lv_rw_wowner(v);
				} while (owner != NULL && TD_IS_RUNNING(owner));
				KTR_STATE0(KTR_SCHED, "thread",
				    sched_tdname(curthread), "running");
				continue;
			}
		} else {
			if ((v & RW_LOCK_WRITE_SPINNER) && RW_READERS(v) == 0) {
				MPASS(!__rw_can_read(td, v, false));
				lock_delay_spin(2);
				v = RW_READ_VALUE(rw);
				continue;
			}
			if (spintries < rowner_retries) {
				spintries++;
				KTR_STATE1(KTR_SCHED, "thread", sched_tdname(curthread),
				    "spinning", "lockname:\"%s\"",
				    rw->lock_object.lo_name);
				n = RW_READERS(v);
				for (i = 0; i < rowner_loops; i += n) {
					lock_delay_spin(n);
					v = RW_READ_VALUE(rw);
					if (!(v & RW_LOCK_READ))
						break;
					n = RW_READERS(v);
					if (n == 0)
						break;
					if (__rw_can_read(td, v, false))
						break;
				}
#ifdef KDTRACE_HOOKS
				lda.spin_cnt += rowner_loops - i;
#endif
				KTR_STATE0(KTR_SCHED, "thread", sched_tdname(curthread),
				    "running");
				if (i < rowner_loops)
					continue;
			}
		}
#endif

		/*
		 * Okay, now it's the hard case.  Some other thread already
		 * has a write lock or there are write waiters present,
		 * acquire the turnstile lock so we can begin the process
		 * of blocking.
		 */
		ts = turnstile_trywait(&rw->lock_object);

		/*
		 * The lock might have been released while we spun, so
		 * recheck its state and restart the loop if needed.
		 */
		v = RW_READ_VALUE(rw);
retry_ts:
		if (((v & RW_LOCK_WRITE_SPINNER) && RW_READERS(v) == 0) ||
		    __rw_can_read(td, v, false)) {
			turnstile_cancel(ts);
			continue;
		}

		owner = lv_rw_wowner(v);

#ifdef ADAPTIVE_RWLOCKS
		/*
		 * The current lock owner might have started executing
		 * on another CPU (or the lock could have changed
		 * owners) while we were waiting on the turnstile
		 * chain lock.  If so, drop the turnstile lock and try
		 * again.
		 */
		if (owner != NULL) {
			if (TD_IS_RUNNING(owner)) {
				turnstile_cancel(ts);
				continue;
			}
		}
#endif

		/*
		 * The lock is held in write mode or it already has waiters.
		 */
		MPASS(!__rw_can_read(td, v, false));

		/*
		 * If the RW_LOCK_READ_WAITERS flag is already set, then
		 * we can go ahead and block.  If it is not set then try
		 * to set it.  If we fail to set it drop the turnstile
		 * lock and restart the loop.
		 */
		if (!(v & RW_LOCK_READ_WAITERS)) {
			if (!atomic_fcmpset_ptr(&rw->rw_lock, &v,
			    v | RW_LOCK_READ_WAITERS))
				goto retry_ts;
			if (LOCK_LOG_TEST(&rw->lock_object, 0))
				CTR2(KTR_LOCK, "%s: %p set read waiters flag",
				    __func__, rw);
		}

		/*
		 * We were unable to acquire the lock and the read waiters
		 * flag is set, so we must block on the turnstile.
		 */
		if (LOCK_LOG_TEST(&rw->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p blocking on turnstile", __func__,
			    rw);
#ifdef KDTRACE_HOOKS
		sleep_time -= lockstat_nsecs(&rw->lock_object);
#endif
		MPASS(owner == rw_owner(rw));
		turnstile_wait(ts, owner, TS_SHARED_QUEUE);
#ifdef KDTRACE_HOOKS
		sleep_time += lockstat_nsecs(&rw->lock_object);
		sleep_cnt++;
#endif
		if (LOCK_LOG_TEST(&rw->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p resuming from turnstile",
			    __func__, rw);
		v = RW_READ_VALUE(rw);
	}
#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	if (__predict_true(!doing_lockprof))
		return;
#endif
#ifdef KDTRACE_HOOKS
	all_time += lockstat_nsecs(&rw->lock_object);
	if (sleep_time)
		LOCKSTAT_RECORD4(rw__block, rw, sleep_time,
		    LOCKSTAT_READER, (state & RW_LOCK_READ) == 0,
		    (state & RW_LOCK_READ) == 0 ? 0 : RW_READERS(state));

	/* Record only the loops spinning and not sleeping. */
	if (lda.spin_cnt > sleep_cnt)
		LOCKSTAT_RECORD4(rw__spin, rw, all_time - sleep_time,
		    LOCKSTAT_READER, (state & RW_LOCK_READ) == 0,
		    (state & RW_LOCK_READ) == 0 ? 0 : RW_READERS(state));
out_lockstat:
#endif
	/*
	 * TODO: acquire "owner of record" here.  Here be turnstile dragons
	 * however.  turnstiles don't like owners changing between calls to
	 * turnstile_wait() currently.
	 */
	LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(rw__acquire, rw, contested,
	    waittime, file, line, LOCKSTAT_READER);
}

void
__rw_rlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF)
{
	struct thread *td;
	uintptr_t v;

	td = curthread;

	KASSERT(kdb_active != 0 || SCHEDULER_STOPPED_TD(td) ||
	    !TD_IS_IDLETHREAD(td),
	    ("rw_rlock() by idle thread %p on rwlock %s @ %s:%d",
	    td, rw->lock_object.lo_name, file, line));
	KASSERT(rw->rw_lock != RW_DESTROYED,
	    ("rw_rlock() of destroyed rwlock @ %s:%d", file, line));
	KASSERT(rw_wowner(rw) != td,
	    ("rw_rlock: wlock already held for %s @ %s:%d",
	    rw->lock_object.lo_name, file, line));
	WITNESS_CHECKORDER(&rw->lock_object, LOP_NEWORDER, file, line, NULL);

	v = RW_READ_VALUE(rw);
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(rw__acquire) ||
	    !__rw_rlock_try(rw, td, &v, true LOCK_FILE_LINE_ARG)))
		__rw_rlock_hard(rw, td, v LOCK_FILE_LINE_ARG);
	else
		lock_profile_obtain_lock_success(&rw->lock_object, 0, 0,
		    file, line);

	LOCK_LOG_LOCK("RLOCK", &rw->lock_object, 0, 0, file, line);
	WITNESS_LOCK(&rw->lock_object, 0, file, line);
	TD_LOCKS_INC(curthread);
}

void
__rw_rlock(volatile uintptr_t *c, const char *file, int line)
{
	struct rwlock *rw;

	rw = rwlock2rw(c);
	__rw_rlock_int(rw LOCK_FILE_LINE_ARG);
}

int
__rw_try_rlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF)
{
	uintptr_t x;

	if (SCHEDULER_STOPPED())
		return (1);

	KASSERT(kdb_active != 0 || !TD_IS_IDLETHREAD(curthread),
	    ("rw_try_rlock() by idle thread %p on rwlock %s @ %s:%d",
	    curthread, rw->lock_object.lo_name, file, line));

	x = rw->rw_lock;
	for (;;) {
		KASSERT(rw->rw_lock != RW_DESTROYED,
		    ("rw_try_rlock() of destroyed rwlock @ %s:%d", file, line));
		if (!(x & RW_LOCK_READ))
			break;
		if (atomic_fcmpset_acq_ptr(&rw->rw_lock, &x, x + RW_ONE_READER)) {
			LOCK_LOG_TRY("RLOCK", &rw->lock_object, 0, 1, file,
			    line);
			WITNESS_LOCK(&rw->lock_object, LOP_TRYLOCK, file, line);
			LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(rw__acquire,
			    rw, 0, 0, file, line, LOCKSTAT_READER);
			TD_LOCKS_INC(curthread);
			curthread->td_rw_rlocks++;
			return (1);
		}
	}

	LOCK_LOG_TRY("RLOCK", &rw->lock_object, 0, 0, file, line);
	return (0);
}

int
__rw_try_rlock(volatile uintptr_t *c, const char *file, int line)
{
	struct rwlock *rw;

	rw = rwlock2rw(c);
	return (__rw_try_rlock_int(rw LOCK_FILE_LINE_ARG));
}

static bool __always_inline
__rw_runlock_try(struct rwlock *rw, struct thread *td, uintptr_t *vp)
{

	for (;;) {
		if (RW_READERS(*vp) > 1 || !(*vp & RW_LOCK_WAITERS)) {
			if (atomic_fcmpset_rel_ptr(&rw->rw_lock, vp,
			    *vp - RW_ONE_READER)) {
				if (LOCK_LOG_TEST(&rw->lock_object, 0))
					CTR4(KTR_LOCK,
					    "%s: %p succeeded %p -> %p",
					    __func__, rw, (void *)*vp,
					    (void *)(*vp - RW_ONE_READER));
				td->td_rw_rlocks--;
				return (true);
			}
			continue;
		}
		break;
	}
	return (false);
}

static void __noinline
__rw_runlock_hard(struct rwlock *rw, struct thread *td, uintptr_t v
    LOCK_FILE_LINE_ARG_DEF)
{
	struct turnstile *ts;
	uintptr_t setv, queue;

	if (SCHEDULER_STOPPED())
		return;

	if (__rw_runlock_try(rw, td, &v))
		goto out_lockstat;

	/*
	 * Ok, we know we have waiters and we think we are the
	 * last reader, so grab the turnstile lock.
	 */
	turnstile_chain_lock(&rw->lock_object);
	v = RW_READ_VALUE(rw);
	for (;;) {
		if (__rw_runlock_try(rw, td, &v))
			break;

		MPASS(v & RW_LOCK_WAITERS);

		/*
		 * Try to drop our lock leaving the lock in a unlocked
		 * state.
		 *
		 * If you wanted to do explicit lock handoff you'd have to
		 * do it here.  You'd also want to use turnstile_signal()
		 * and you'd have to handle the race where a higher
		 * priority thread blocks on the write lock before the
		 * thread you wakeup actually runs and have the new thread
		 * "steal" the lock.  For now it's a lot simpler to just
		 * wakeup all of the waiters.
		 *
		 * As above, if we fail, then another thread might have
		 * acquired a read lock, so drop the turnstile lock and
		 * restart.
		 */
		setv = RW_UNLOCKED;
		queue = TS_SHARED_QUEUE;
		if (v & RW_LOCK_WRITE_WAITERS) {
			queue = TS_EXCLUSIVE_QUEUE;
			setv |= (v & RW_LOCK_READ_WAITERS);
		}
		setv |= (v & RW_LOCK_WRITE_SPINNER);
		if (!atomic_fcmpset_rel_ptr(&rw->rw_lock, &v, setv))
			continue;
		if (LOCK_LOG_TEST(&rw->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p last succeeded with waiters",
			    __func__, rw);

		/*
		 * Ok.  The lock is released and all that's left is to
		 * wake up the waiters.  Note that the lock might not be
		 * free anymore, but in that case the writers will just
		 * block again if they run before the new lock holder(s)
		 * release the lock.
		 */
		ts = turnstile_lookup(&rw->lock_object);
		MPASS(ts != NULL);
		turnstile_broadcast(ts, queue);
		turnstile_unpend(ts);
		td->td_rw_rlocks--;
		break;
	}
	turnstile_chain_unlock(&rw->lock_object);
out_lockstat:
	LOCKSTAT_PROFILE_RELEASE_RWLOCK(rw__release, rw, LOCKSTAT_READER);
}

void
_rw_runlock_cookie_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF)
{
	struct thread *td;
	uintptr_t v;

	KASSERT(rw->rw_lock != RW_DESTROYED,
	    ("rw_runlock() of destroyed rwlock @ %s:%d", file, line));
	__rw_assert(&rw->rw_lock, RA_RLOCKED, file, line);
	WITNESS_UNLOCK(&rw->lock_object, 0, file, line);
	LOCK_LOG_LOCK("RUNLOCK", &rw->lock_object, 0, 0, file, line);

	td = curthread;
	v = RW_READ_VALUE(rw);

	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(rw__release) ||
	    !__rw_runlock_try(rw, td, &v)))
		__rw_runlock_hard(rw, td, v LOCK_FILE_LINE_ARG);
	else
		lock_profile_release_lock(&rw->lock_object);

	TD_LOCKS_DEC(curthread);
}

void
_rw_runlock_cookie(volatile uintptr_t *c, const char *file, int line)
{
	struct rwlock *rw;

	rw = rwlock2rw(c);
	_rw_runlock_cookie_int(rw LOCK_FILE_LINE_ARG);
}

#ifdef ADAPTIVE_RWLOCKS
static inline void
rw_drop_critical(uintptr_t v, bool *in_critical, int *extra_work)
{

	if (v & RW_LOCK_WRITE_SPINNER)
		return;
	if (*in_critical) {
		critical_exit();
		*in_critical = false;
		(*extra_work)--;
	}
}
#else
#define rw_drop_critical(v, in_critical, extra_work) do { } while (0)
#endif

/*
 * This function is called when we are unable to obtain a write lock on the
 * first try.  This means that at least one other thread holds either a
 * read or write lock.
 */
void
__rw_wlock_hard(volatile uintptr_t *c, uintptr_t v LOCK_FILE_LINE_ARG_DEF)
{
	uintptr_t tid;
	struct rwlock *rw;
	struct turnstile *ts;
	struct thread *owner;
#ifdef ADAPTIVE_RWLOCKS
	int spintries = 0;
	int i, n;
	enum { READERS, WRITER } sleep_reason = READERS;
	bool in_critical = false;
#endif
	uintptr_t setv;
#ifdef LOCK_PROFILING
	uint64_t waittime = 0;
	int contested = 0;
#endif
#if defined(ADAPTIVE_RWLOCKS) || defined(KDTRACE_HOOKS)
	struct lock_delay_arg lda;
#endif
#ifdef KDTRACE_HOOKS
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
	rw = rwlock2rw(c);

#ifdef KDTRACE_HOOKS
	if (LOCKSTAT_PROFILE_ENABLED(rw__acquire)) {
		while (v == RW_UNLOCKED) {
			if (_rw_write_lock_fetch(rw, &v, tid))
				goto out_lockstat;
		}
		extra_work = 1;
		doing_lockprof = 1;
		all_time -= lockstat_nsecs(&rw->lock_object);
		state = v;
	}
#endif
#ifdef LOCK_PROFILING
	extra_work = 1;
	doing_lockprof = 1;
	state = v;
#endif

	if (SCHEDULER_STOPPED())
		return;

#if defined(ADAPTIVE_RWLOCKS)
	lock_delay_arg_init(&lda, &rw_delay);
#elif defined(KDTRACE_HOOKS)
	lock_delay_arg_init(&lda, NULL);
#endif
	if (__predict_false(v == RW_UNLOCKED))
		v = RW_READ_VALUE(rw);

	if (__predict_false(lv_rw_wowner(v) == (struct thread *)tid)) {
		KASSERT(rw->lock_object.lo_flags & LO_RECURSABLE,
		    ("%s: recursing but non-recursive rw %s @ %s:%d\n",
		    __func__, rw->lock_object.lo_name, file, line));
		rw->rw_recurse++;
		atomic_set_ptr(&rw->rw_lock, RW_LOCK_WRITER_RECURSED);
		if (LOCK_LOG_TEST(&rw->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p recursing", __func__, rw);
		return;
	}

	if (LOCK_LOG_TEST(&rw->lock_object, 0))
		CTR5(KTR_LOCK, "%s: %s contested (lock=%p) at %s:%d", __func__,
		    rw->lock_object.lo_name, (void *)rw->rw_lock, file, line);

#ifdef HWPMC_HOOKS
	PMC_SOFT_CALL( , , lock, failed);
#endif
	lock_profile_obtain_lock_failed(&rw->lock_object,
	    &contested, &waittime);

	for (;;) {
		if (v == RW_UNLOCKED) {
			if (_rw_write_lock_fetch(rw, &v, tid))
				break;
			continue;
		}
#ifdef KDTRACE_HOOKS
		lda.spin_cnt++;
#endif

#ifdef ADAPTIVE_RWLOCKS
		if (v == (RW_LOCK_READ | RW_LOCK_WRITE_SPINNER)) {
			if (atomic_fcmpset_acq_ptr(&rw->rw_lock, &v, tid))
				break;
			continue;
		}

		/*
		 * If the lock is write locked and the owner is
		 * running on another CPU, spin until the owner stops
		 * running or the state of the lock changes.
		 */
		if (!(v & RW_LOCK_READ)) {
			rw_drop_critical(v, &in_critical, &extra_work);
			sleep_reason = WRITER;
			owner = lv_rw_wowner(v);
			if (!TD_IS_RUNNING(owner))
				goto ts;
			if (LOCK_LOG_TEST(&rw->lock_object, 0))
				CTR3(KTR_LOCK, "%s: spinning on %p held by %p",
				    __func__, rw, owner);
			KTR_STATE1(KTR_SCHED, "thread", sched_tdname(curthread),
			    "spinning", "lockname:\"%s\"",
			    rw->lock_object.lo_name);
			do {
				lock_delay(&lda);
				v = RW_READ_VALUE(rw);
				owner = lv_rw_wowner(v);
			} while (owner != NULL && TD_IS_RUNNING(owner));
			KTR_STATE0(KTR_SCHED, "thread", sched_tdname(curthread),
			    "running");
			continue;
		} else if (RW_READERS(v) > 0) {
			sleep_reason = READERS;
			if (spintries == rowner_retries)
				goto ts;
			if (!(v & RW_LOCK_WRITE_SPINNER)) {
				if (!in_critical) {
					critical_enter();
					in_critical = true;
					extra_work++;
				}
				if (!atomic_fcmpset_ptr(&rw->rw_lock, &v,
				    v | RW_LOCK_WRITE_SPINNER)) {
					critical_exit();
					in_critical = false;
					extra_work--;
					continue;
				}
			}
			spintries++;
			KTR_STATE1(KTR_SCHED, "thread", sched_tdname(curthread),
			    "spinning", "lockname:\"%s\"",
			    rw->lock_object.lo_name);
			n = RW_READERS(v);
			for (i = 0; i < rowner_loops; i += n) {
				lock_delay_spin(n);
				v = RW_READ_VALUE(rw);
				if (!(v & RW_LOCK_WRITE_SPINNER))
					break;
				if (!(v & RW_LOCK_READ))
					break;
				n = RW_READERS(v);
				if (n == 0)
					break;
			}
#ifdef KDTRACE_HOOKS
			lda.spin_cnt += i;
#endif
			KTR_STATE0(KTR_SCHED, "thread", sched_tdname(curthread),
			    "running");
			if (i < rowner_loops)
				continue;
		}
ts:
#endif
		ts = turnstile_trywait(&rw->lock_object);
		v = RW_READ_VALUE(rw);
retry_ts:
		owner = lv_rw_wowner(v);

#ifdef ADAPTIVE_RWLOCKS
		/*
		 * The current lock owner might have started executing
		 * on another CPU (or the lock could have changed
		 * owners) while we were waiting on the turnstile
		 * chain lock.  If so, drop the turnstile lock and try
		 * again.
		 */
		if (owner != NULL) {
			if (TD_IS_RUNNING(owner)) {
				turnstile_cancel(ts);
				rw_drop_critical(v, &in_critical, &extra_work);
				continue;
			}
		} else if (RW_READERS(v) > 0 && sleep_reason == WRITER) {
			turnstile_cancel(ts);
			rw_drop_critical(v, &in_critical, &extra_work);
			continue;
		}
#endif
		/*
		 * Check for the waiters flags about this rwlock.
		 * If the lock was released, without maintain any pending
		 * waiters queue, simply try to acquire it.
		 * If a pending waiters queue is present, claim the lock
		 * ownership and maintain the pending queue.
		 */
		setv = v & (RW_LOCK_WAITERS | RW_LOCK_WRITE_SPINNER);
		if ((v & ~setv) == RW_UNLOCKED) {
			setv &= ~RW_LOCK_WRITE_SPINNER;
			if (atomic_fcmpset_acq_ptr(&rw->rw_lock, &v, tid | setv)) {
				if (setv)
					turnstile_claim(ts);
				else
					turnstile_cancel(ts);
				break;
			}
			goto retry_ts;
		}

#ifdef ADAPTIVE_RWLOCKS
		if (in_critical) {
			if ((v & RW_LOCK_WRITE_SPINNER) ||
			    !((v & RW_LOCK_WRITE_WAITERS))) {
				setv = v & ~RW_LOCK_WRITE_SPINNER;
				setv |= RW_LOCK_WRITE_WAITERS;
				if (!atomic_fcmpset_ptr(&rw->rw_lock, &v, setv))
					goto retry_ts;
			}
			critical_exit();
			in_critical = false;
			extra_work--;
		} else {
#endif
			/*
			 * If the RW_LOCK_WRITE_WAITERS flag isn't set, then try to
			 * set it.  If we fail to set it, then loop back and try
			 * again.
			 */
			if (!(v & RW_LOCK_WRITE_WAITERS)) {
				if (!atomic_fcmpset_ptr(&rw->rw_lock, &v,
				    v | RW_LOCK_WRITE_WAITERS))
					goto retry_ts;
				if (LOCK_LOG_TEST(&rw->lock_object, 0))
					CTR2(KTR_LOCK, "%s: %p set write waiters flag",
					    __func__, rw);
			}
#ifdef ADAPTIVE_RWLOCKS
		}
#endif
		/*
		 * We were unable to acquire the lock and the write waiters
		 * flag is set, so we must block on the turnstile.
		 */
		if (LOCK_LOG_TEST(&rw->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p blocking on turnstile", __func__,
			    rw);
#ifdef KDTRACE_HOOKS
		sleep_time -= lockstat_nsecs(&rw->lock_object);
#endif
		MPASS(owner == rw_owner(rw));
		turnstile_wait(ts, owner, TS_EXCLUSIVE_QUEUE);
#ifdef KDTRACE_HOOKS
		sleep_time += lockstat_nsecs(&rw->lock_object);
		sleep_cnt++;
#endif
		if (LOCK_LOG_TEST(&rw->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p resuming from turnstile",
			    __func__, rw);
#ifdef ADAPTIVE_RWLOCKS
		spintries = 0;
#endif
		v = RW_READ_VALUE(rw);
	}
	if (__predict_true(!extra_work))
		return;
#ifdef ADAPTIVE_RWLOCKS
	if (in_critical)
		critical_exit();
#endif
#if defined(KDTRACE_HOOKS) || defined(LOCK_PROFILING)
	if (__predict_true(!doing_lockprof))
		return;
#endif
#ifdef KDTRACE_HOOKS
	all_time += lockstat_nsecs(&rw->lock_object);
	if (sleep_time)
		LOCKSTAT_RECORD4(rw__block, rw, sleep_time,
		    LOCKSTAT_WRITER, (state & RW_LOCK_READ) == 0,
		    (state & RW_LOCK_READ) == 0 ? 0 : RW_READERS(state));

	/* Record only the loops spinning and not sleeping. */
	if (lda.spin_cnt > sleep_cnt)
		LOCKSTAT_RECORD4(rw__spin, rw, all_time - sleep_time,
		    LOCKSTAT_WRITER, (state & RW_LOCK_READ) == 0,
		    (state & RW_LOCK_READ) == 0 ? 0 : RW_READERS(state));
out_lockstat:
#endif
	LOCKSTAT_PROFILE_OBTAIN_RWLOCK_SUCCESS(rw__acquire, rw, contested,
	    waittime, file, line, LOCKSTAT_WRITER);
}

/*
 * This function is called if lockstat is active or the first try at releasing
 * a write lock failed.  The latter means that the lock is recursed or one of
 * the 2 waiter bits must be set indicating that at least one thread is waiting
 * on this lock.
 */
void
__rw_wunlock_hard(volatile uintptr_t *c, uintptr_t v LOCK_FILE_LINE_ARG_DEF)
{
	struct rwlock *rw;
	struct turnstile *ts;
	uintptr_t tid, setv;
	int queue;

	tid = (uintptr_t)curthread;
	if (SCHEDULER_STOPPED())
		return;

	rw = rwlock2rw(c);
	if (__predict_false(v == tid))
		v = RW_READ_VALUE(rw);

	if (v & RW_LOCK_WRITER_RECURSED) {
		if (--(rw->rw_recurse) == 0)
			atomic_clear_ptr(&rw->rw_lock, RW_LOCK_WRITER_RECURSED);
		if (LOCK_LOG_TEST(&rw->lock_object, 0))
			CTR2(KTR_LOCK, "%s: %p unrecursing", __func__, rw);
		return;
	}

	LOCKSTAT_PROFILE_RELEASE_RWLOCK(rw__release, rw, LOCKSTAT_WRITER);
	if (v == tid && _rw_write_unlock(rw, tid))
		return;

	KASSERT(rw->rw_lock & (RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS),
	    ("%s: neither of the waiter flags are set", __func__));

	if (LOCK_LOG_TEST(&rw->lock_object, 0))
		CTR2(KTR_LOCK, "%s: %p contested", __func__, rw);

	turnstile_chain_lock(&rw->lock_object);

	/*
	 * Use the same algo as sx locks for now.  Prefer waking up shared
	 * waiters if we have any over writers.  This is probably not ideal.
	 *
	 * 'v' is the value we are going to write back to rw_lock.  If we
	 * have waiters on both queues, we need to preserve the state of
	 * the waiter flag for the queue we don't wake up.  For now this is
	 * hardcoded for the algorithm mentioned above.
	 *
	 * In the case of both readers and writers waiting we wakeup the
	 * readers but leave the RW_LOCK_WRITE_WAITERS flag set.  If a
	 * new writer comes in before a reader it will claim the lock up
	 * above.  There is probably a potential priority inversion in
	 * there that could be worked around either by waking both queues
	 * of waiters or doing some complicated lock handoff gymnastics.
	 */
	setv = RW_UNLOCKED;
	v = RW_READ_VALUE(rw);
	queue = TS_SHARED_QUEUE;
	if (v & RW_LOCK_WRITE_WAITERS) {
		queue = TS_EXCLUSIVE_QUEUE;
		setv |= (v & RW_LOCK_READ_WAITERS);
	}
	atomic_store_rel_ptr(&rw->rw_lock, setv);

	/* Wake up all waiters for the specific queue. */
	if (LOCK_LOG_TEST(&rw->lock_object, 0))
		CTR3(KTR_LOCK, "%s: %p waking up %s waiters", __func__, rw,
		    queue == TS_SHARED_QUEUE ? "read" : "write");

	ts = turnstile_lookup(&rw->lock_object);
	MPASS(ts != NULL);
	turnstile_broadcast(ts, queue);
	turnstile_unpend(ts);
	turnstile_chain_unlock(&rw->lock_object);
}

/*
 * Attempt to do a non-blocking upgrade from a read lock to a write
 * lock.  This will only succeed if this thread holds a single read
 * lock.  Returns true if the upgrade succeeded and false otherwise.
 */
int
__rw_try_upgrade_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF)
{
	uintptr_t v, setv, tid;
	struct turnstile *ts;
	int success;

	if (SCHEDULER_STOPPED())
		return (1);

	KASSERT(rw->rw_lock != RW_DESTROYED,
	    ("rw_try_upgrade() of destroyed rwlock @ %s:%d", file, line));
	__rw_assert(&rw->rw_lock, RA_RLOCKED, file, line);

	/*
	 * Attempt to switch from one reader to a writer.  If there
	 * are any write waiters, then we will have to lock the
	 * turnstile first to prevent races with another writer
	 * calling turnstile_wait() before we have claimed this
	 * turnstile.  So, do the simple case of no waiters first.
	 */
	tid = (uintptr_t)curthread;
	success = 0;
	v = RW_READ_VALUE(rw);
	for (;;) {
		if (RW_READERS(v) > 1)
			break;
		if (!(v & RW_LOCK_WAITERS)) {
			success = atomic_fcmpset_acq_ptr(&rw->rw_lock, &v, tid);
			if (!success)
				continue;
			break;
		}

		/*
		 * Ok, we think we have waiters, so lock the turnstile.
		 */
		ts = turnstile_trywait(&rw->lock_object);
		v = RW_READ_VALUE(rw);
retry_ts:
		if (RW_READERS(v) > 1) {
			turnstile_cancel(ts);
			break;
		}
		/*
		 * Try to switch from one reader to a writer again.  This time
		 * we honor the current state of the waiters flags.
		 * If we obtain the lock with the flags set, then claim
		 * ownership of the turnstile.
		 */
		setv = tid | (v & RW_LOCK_WAITERS);
		success = atomic_fcmpset_ptr(&rw->rw_lock, &v, setv);
		if (success) {
			if (v & RW_LOCK_WAITERS)
				turnstile_claim(ts);
			else
				turnstile_cancel(ts);
			break;
		}
		goto retry_ts;
	}
	LOCK_LOG_TRY("WUPGRADE", &rw->lock_object, 0, success, file, line);
	if (success) {
		curthread->td_rw_rlocks--;
		WITNESS_UPGRADE(&rw->lock_object, LOP_EXCLUSIVE | LOP_TRYLOCK,
		    file, line);
		LOCKSTAT_RECORD0(rw__upgrade, rw);
	}
	return (success);
}

int
__rw_try_upgrade(volatile uintptr_t *c, const char *file, int line)
{
	struct rwlock *rw;

	rw = rwlock2rw(c);
	return (__rw_try_upgrade_int(rw LOCK_FILE_LINE_ARG));
}

/*
 * Downgrade a write lock into a single read lock.
 */
void
__rw_downgrade_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF)
{
	struct turnstile *ts;
	uintptr_t tid, v;
	int rwait, wwait;

	if (SCHEDULER_STOPPED())
		return;

	KASSERT(rw->rw_lock != RW_DESTROYED,
	    ("rw_downgrade() of destroyed rwlock @ %s:%d", file, line));
	__rw_assert(&rw->rw_lock, RA_WLOCKED | RA_NOTRECURSED, file, line);
#ifndef INVARIANTS
	if (rw_recursed(rw))
		panic("downgrade of a recursed lock");
#endif

	WITNESS_DOWNGRADE(&rw->lock_object, 0, file, line);

	/*
	 * Convert from a writer to a single reader.  First we handle
	 * the easy case with no waiters.  If there are any waiters, we
	 * lock the turnstile and "disown" the lock.
	 */
	tid = (uintptr_t)curthread;
	if (atomic_cmpset_rel_ptr(&rw->rw_lock, tid, RW_READERS_LOCK(1)))
		goto out;

	/*
	 * Ok, we think we have waiters, so lock the turnstile so we can
	 * read the waiter flags without any races.
	 */
	turnstile_chain_lock(&rw->lock_object);
	v = rw->rw_lock & RW_LOCK_WAITERS;
	rwait = v & RW_LOCK_READ_WAITERS;
	wwait = v & RW_LOCK_WRITE_WAITERS;
	MPASS(rwait | wwait);

	/*
	 * Downgrade from a write lock while preserving waiters flag
	 * and give up ownership of the turnstile.
	 */
	ts = turnstile_lookup(&rw->lock_object);
	MPASS(ts != NULL);
	if (!wwait)
		v &= ~RW_LOCK_READ_WAITERS;
	atomic_store_rel_ptr(&rw->rw_lock, RW_READERS_LOCK(1) | v);
	/*
	 * Wake other readers if there are no writers pending.  Otherwise they
	 * won't be able to acquire the lock anyway.
	 */
	if (rwait && !wwait) {
		turnstile_broadcast(ts, TS_SHARED_QUEUE);
		turnstile_unpend(ts);
	} else
		turnstile_disown(ts);
	turnstile_chain_unlock(&rw->lock_object);
out:
	curthread->td_rw_rlocks++;
	LOCK_LOG_LOCK("WDOWNGRADE", &rw->lock_object, 0, 0, file, line);
	LOCKSTAT_RECORD0(rw__downgrade, rw);
}

void
__rw_downgrade(volatile uintptr_t *c, const char *file, int line)
{
	struct rwlock *rw;

	rw = rwlock2rw(c);
	__rw_downgrade_int(rw LOCK_FILE_LINE_ARG);
}

#ifdef INVARIANT_SUPPORT
#ifndef INVARIANTS
#undef __rw_assert
#endif

/*
 * In the non-WITNESS case, rw_assert() can only detect that at least
 * *some* thread owns an rlock, but it cannot guarantee that *this*
 * thread owns an rlock.
 */
void
__rw_assert(const volatile uintptr_t *c, int what, const char *file, int line)
{
	const struct rwlock *rw;

	if (SCHEDULER_STOPPED())
		return;

	rw = rwlock2rw(c);

	switch (what) {
	case RA_LOCKED:
	case RA_LOCKED | RA_RECURSED:
	case RA_LOCKED | RA_NOTRECURSED:
	case RA_RLOCKED:
	case RA_RLOCKED | RA_RECURSED:
	case RA_RLOCKED | RA_NOTRECURSED:
#ifdef WITNESS
		witness_assert(&rw->lock_object, what, file, line);
#else
		/*
		 * If some other thread has a write lock or we have one
		 * and are asserting a read lock, fail.  Also, if no one
		 * has a lock at all, fail.
		 */
		if (rw->rw_lock == RW_UNLOCKED ||
		    (!(rw->rw_lock & RW_LOCK_READ) && (what & RA_RLOCKED ||
		    rw_wowner(rw) != curthread)))
			panic("Lock %s not %slocked @ %s:%d\n",
			    rw->lock_object.lo_name, (what & RA_RLOCKED) ?
			    "read " : "", file, line);

		if (!(rw->rw_lock & RW_LOCK_READ) && !(what & RA_RLOCKED)) {
			if (rw_recursed(rw)) {
				if (what & RA_NOTRECURSED)
					panic("Lock %s recursed @ %s:%d\n",
					    rw->lock_object.lo_name, file,
					    line);
			} else if (what & RA_RECURSED)
				panic("Lock %s not recursed @ %s:%d\n",
				    rw->lock_object.lo_name, file, line);
		}
#endif
		break;
	case RA_WLOCKED:
	case RA_WLOCKED | RA_RECURSED:
	case RA_WLOCKED | RA_NOTRECURSED:
		if (rw_wowner(rw) != curthread)
			panic("Lock %s not exclusively locked @ %s:%d\n",
			    rw->lock_object.lo_name, file, line);
		if (rw_recursed(rw)) {
			if (what & RA_NOTRECURSED)
				panic("Lock %s recursed @ %s:%d\n",
				    rw->lock_object.lo_name, file, line);
		} else if (what & RA_RECURSED)
			panic("Lock %s not recursed @ %s:%d\n",
			    rw->lock_object.lo_name, file, line);
		break;
	case RA_UNLOCKED:
#ifdef WITNESS
		witness_assert(&rw->lock_object, what, file, line);
#else
		/*
		 * If we hold a write lock fail.  We can't reliably check
		 * to see if we hold a read lock or not.
		 */
		if (rw_wowner(rw) == curthread)
			panic("Lock %s exclusively locked @ %s:%d\n",
			    rw->lock_object.lo_name, file, line);
#endif
		break;
	default:
		panic("Unknown rw lock assertion: %d @ %s:%d", what, file,
		    line);
	}
}
#endif /* INVARIANT_SUPPORT */

#ifdef DDB
void
db_show_rwlock(const struct lock_object *lock)
{
	const struct rwlock *rw;
	struct thread *td;

	rw = (const struct rwlock *)lock;

	db_printf(" state: ");
	if (rw->rw_lock == RW_UNLOCKED)
		db_printf("UNLOCKED\n");
	else if (rw->rw_lock == RW_DESTROYED) {
		db_printf("DESTROYED\n");
		return;
	} else if (rw->rw_lock & RW_LOCK_READ)
		db_printf("RLOCK: %ju locks\n",
		    (uintmax_t)(RW_READERS(rw->rw_lock)));
	else {
		td = rw_wowner(rw);
		db_printf("WLOCK: %p (tid %d, pid %d, \"%s\")\n", td,
		    td->td_tid, td->td_proc->p_pid, td->td_name);
		if (rw_recursed(rw))
			db_printf(" recursed: %u\n", rw->rw_recurse);
	}
	db_printf(" waiters: ");
	switch (rw->rw_lock & (RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS)) {
	case RW_LOCK_READ_WAITERS:
		db_printf("readers\n");
		break;
	case RW_LOCK_WRITE_WAITERS:
		db_printf("writers\n");
		break;
	case RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS:
		db_printf("readers and writers\n");
		break;
	default:
		db_printf("none\n");
		break;
	}
}

#endif
