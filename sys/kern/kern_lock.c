/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Attilio Rao <attilio@FreeBSD.org>
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

#include "opt_ddb.h"
#include "opt_hwpmc_hooks.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/lock_profile.h>
#include <sys/lockmgr.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sleepqueue.h>
#ifdef DEBUG_LOCKS
#include <sys/stack.h>
#endif
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/cpu.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#ifdef HWPMC_HOOKS
#include <sys/pmckern.h>
PMC_SOFT_DECLARE( , , lock, failed);
#endif

CTASSERT(((LK_ADAPTIVE | LK_NOSHARE) & LO_CLASSFLAGS) ==
    (LK_ADAPTIVE | LK_NOSHARE));
CTASSERT(LK_UNLOCKED == (LK_UNLOCKED &
    ~(LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS)));

#define	SQ_EXCLUSIVE_QUEUE	0
#define	SQ_SHARED_QUEUE		1

#ifndef INVARIANTS
#define	_lockmgr_assert(lk, what, file, line)
#endif

#define	TD_SLOCKS_INC(td)	((td)->td_lk_slocks++)
#define	TD_SLOCKS_DEC(td)	((td)->td_lk_slocks--)

#ifndef DEBUG_LOCKS
#define	STACK_PRINT(lk)
#define	STACK_SAVE(lk)
#define	STACK_ZERO(lk)
#else
#define	STACK_PRINT(lk)	stack_print_ddb(&(lk)->lk_stack)
#define	STACK_SAVE(lk)	stack_save(&(lk)->lk_stack)
#define	STACK_ZERO(lk)	stack_zero(&(lk)->lk_stack)
#endif

#define	LOCK_LOG2(lk, string, arg1, arg2)				\
	if (LOCK_LOG_TEST(&(lk)->lock_object, 0))			\
		CTR2(KTR_LOCK, (string), (arg1), (arg2))
#define	LOCK_LOG3(lk, string, arg1, arg2, arg3)				\
	if (LOCK_LOG_TEST(&(lk)->lock_object, 0))			\
		CTR3(KTR_LOCK, (string), (arg1), (arg2), (arg3))

#define	GIANT_DECLARE							\
	int _i = 0;							\
	WITNESS_SAVE_DECL(Giant)
#define	GIANT_RESTORE() do {						\
	if (__predict_false(_i > 0)) {					\
		while (_i--)						\
			mtx_lock(&Giant);				\
		WITNESS_RESTORE(&Giant.lock_object, Giant);		\
	}								\
} while (0)
#define	GIANT_SAVE() do {						\
	if (__predict_false(mtx_owned(&Giant))) {			\
		WITNESS_SAVE(&Giant.lock_object, Giant);		\
		while (mtx_owned(&Giant)) {				\
			_i++;						\
			mtx_unlock(&Giant);				\
		}							\
	}								\
} while (0)

static bool __always_inline
LK_CAN_SHARE(uintptr_t x, int flags, bool fp)
{

	if ((x & (LK_SHARE | LK_EXCLUSIVE_WAITERS | LK_EXCLUSIVE_SPINNERS)) ==
	    LK_SHARE)
		return (true);
	if (fp || (!(x & LK_SHARE)))
		return (false);
	if ((curthread->td_lk_slocks != 0 && !(flags & LK_NODDLKTREAT)) ||
	    (curthread->td_pflags & TDP_DEADLKTREAT))
		return (true);
	return (false);
}

#define	LK_TRYOP(x)							\
	((x) & LK_NOWAIT)

#define	LK_CAN_WITNESS(x)						\
	(((x) & LK_NOWITNESS) == 0 && !LK_TRYOP(x))
#define	LK_TRYWIT(x)							\
	(LK_TRYOP(x) ? LOP_TRYLOCK : 0)

#define	LK_CAN_ADAPT(lk, f)						\
	(((lk)->lock_object.lo_flags & LK_ADAPTIVE) != 0 &&		\
	((f) & LK_SLEEPFAIL) == 0)

#define	lockmgr_disowned(lk)						\
	(((lk)->lk_lock & ~(LK_FLAGMASK & ~LK_SHARE)) == LK_KERNPROC)

#define	lockmgr_xlocked_v(v)						\
	(((v) & ~(LK_FLAGMASK & ~LK_SHARE)) == (uintptr_t)curthread)

#define	lockmgr_xlocked(lk) lockmgr_xlocked_v((lk)->lk_lock)

static void	assert_lockmgr(const struct lock_object *lock, int how);
#ifdef DDB
static void	db_show_lockmgr(const struct lock_object *lock);
#endif
static void	lock_lockmgr(struct lock_object *lock, uintptr_t how);
#ifdef KDTRACE_HOOKS
static int	owner_lockmgr(const struct lock_object *lock,
		    struct thread **owner);
#endif
static uintptr_t unlock_lockmgr(struct lock_object *lock);

struct lock_class lock_class_lockmgr = {
	.lc_name = "lockmgr",
	.lc_flags = LC_RECURSABLE | LC_SLEEPABLE | LC_SLEEPLOCK | LC_UPGRADABLE,
	.lc_assert = assert_lockmgr,
#ifdef DDB
	.lc_ddb_show = db_show_lockmgr,
#endif
	.lc_lock = lock_lockmgr,
	.lc_unlock = unlock_lockmgr,
#ifdef KDTRACE_HOOKS
	.lc_owner = owner_lockmgr,
#endif
};

struct lockmgr_wait {
	const char *iwmesg;
	int ipri;
	int itimo;
};

static bool __always_inline lockmgr_slock_try(struct lock *lk, uintptr_t *xp,
    int flags, bool fp);
static bool __always_inline lockmgr_sunlock_try(struct lock *lk, uintptr_t *xp);

static void
lockmgr_exit(u_int flags, struct lock_object *ilk, int wakeup_swapper)
{
	struct lock_class *class;

	if (flags & LK_INTERLOCK) {
		class = LOCK_CLASS(ilk);
		class->lc_unlock(ilk);
	}

	if (__predict_false(wakeup_swapper))
		kick_proc0();
}

static void
lockmgr_note_shared_acquire(struct lock *lk, int contested,
    uint64_t waittime, const char *file, int line, int flags)
{

	lock_profile_obtain_lock_success(&lk->lock_object, contested, waittime,
	    file, line);
	LOCK_LOG_LOCK("SLOCK", &lk->lock_object, 0, 0, file, line);
	WITNESS_LOCK(&lk->lock_object, LK_TRYWIT(flags), file, line);
	TD_LOCKS_INC(curthread);
	TD_SLOCKS_INC(curthread);
	STACK_SAVE(lk);
}

static void
lockmgr_note_shared_release(struct lock *lk, const char *file, int line)
{

	lock_profile_release_lock(&lk->lock_object);
	WITNESS_UNLOCK(&lk->lock_object, 0, file, line);
	LOCK_LOG_LOCK("SUNLOCK", &lk->lock_object, 0, 0, file, line);
	TD_LOCKS_DEC(curthread);
	TD_SLOCKS_DEC(curthread);
}

static void
lockmgr_note_exclusive_acquire(struct lock *lk, int contested,
    uint64_t waittime, const char *file, int line, int flags)
{

	lock_profile_obtain_lock_success(&lk->lock_object, contested, waittime,
	    file, line);
	LOCK_LOG_LOCK("XLOCK", &lk->lock_object, 0, lk->lk_recurse, file, line);
	WITNESS_LOCK(&lk->lock_object, LOP_EXCLUSIVE | LK_TRYWIT(flags), file,
	    line);
	TD_LOCKS_INC(curthread);
	STACK_SAVE(lk);
}

static void
lockmgr_note_exclusive_release(struct lock *lk, const char *file, int line)
{

	lock_profile_release_lock(&lk->lock_object);
	LOCK_LOG_LOCK("XUNLOCK", &lk->lock_object, 0, lk->lk_recurse, file,
	    line);
	WITNESS_UNLOCK(&lk->lock_object, LOP_EXCLUSIVE, file, line);
	TD_LOCKS_DEC(curthread);
}

static __inline struct thread *
lockmgr_xholder(const struct lock *lk)
{
	uintptr_t x;

	x = lk->lk_lock;
	return ((x & LK_SHARE) ? NULL : (struct thread *)LK_HOLDER(x));
}

/*
 * It assumes sleepq_lock held and returns with this one unheld.
 * It also assumes the generic interlock is sane and previously checked.
 * If LK_INTERLOCK is specified the interlock is not reacquired after the
 * sleep.
 */
static __inline int
sleeplk(struct lock *lk, u_int flags, struct lock_object *ilk,
    const char *wmesg, int pri, int timo, int queue)
{
	GIANT_DECLARE;
	struct lock_class *class;
	int catch, error;

	class = (flags & LK_INTERLOCK) ? LOCK_CLASS(ilk) : NULL;
	catch = pri & PCATCH;
	pri &= PRIMASK;
	error = 0;

	LOCK_LOG3(lk, "%s: %p blocking on the %s sleepqueue", __func__, lk,
	    (queue == SQ_EXCLUSIVE_QUEUE) ? "exclusive" : "shared");

	if (flags & LK_INTERLOCK)
		class->lc_unlock(ilk);
	if (queue == SQ_EXCLUSIVE_QUEUE && (flags & LK_SLEEPFAIL) != 0)
		lk->lk_exslpfail++;
	GIANT_SAVE();
	sleepq_add(&lk->lock_object, NULL, wmesg, SLEEPQ_LK | (catch ?
	    SLEEPQ_INTERRUPTIBLE : 0), queue);
	if ((flags & LK_TIMELOCK) && timo)
		sleepq_set_timeout(&lk->lock_object, timo);

	/*
	 * Decisional switch for real sleeping.
	 */
	if ((flags & LK_TIMELOCK) && timo && catch)
		error = sleepq_timedwait_sig(&lk->lock_object, pri);
	else if ((flags & LK_TIMELOCK) && timo)
		error = sleepq_timedwait(&lk->lock_object, pri);
	else if (catch)
		error = sleepq_wait_sig(&lk->lock_object, pri);
	else
		sleepq_wait(&lk->lock_object, pri);
	GIANT_RESTORE();
	if ((flags & LK_SLEEPFAIL) && error == 0)
		error = ENOLCK;

	return (error);
}

static __inline int
wakeupshlk(struct lock *lk, const char *file, int line)
{
	uintptr_t v, x, orig_x;
	u_int realexslp;
	int queue, wakeup_swapper;

	wakeup_swapper = 0;
	for (;;) {
		x = lk->lk_lock;
		if (lockmgr_sunlock_try(lk, &x))
			break;

		/*
		 * We should have a sharer with waiters, so enter the hard
		 * path in order to handle wakeups correctly.
		 */
		sleepq_lock(&lk->lock_object);
		orig_x = lk->lk_lock;
retry_sleepq:
		x = orig_x & (LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS);
		v = LK_UNLOCKED;

		/*
		 * If the lock has exclusive waiters, give them preference in
		 * order to avoid deadlock with shared runners up.
		 * If interruptible sleeps left the exclusive queue empty
		 * avoid a starvation for the threads sleeping on the shared
		 * queue by giving them precedence and cleaning up the
		 * exclusive waiters bit anyway.
		 * Please note that lk_exslpfail count may be lying about
		 * the real number of waiters with the LK_SLEEPFAIL flag on
		 * because they may be used in conjunction with interruptible
		 * sleeps so lk_exslpfail might be considered an 'upper limit'
		 * bound, including the edge cases.
		 */
		realexslp = sleepq_sleepcnt(&lk->lock_object,
		    SQ_EXCLUSIVE_QUEUE);
		if ((x & LK_EXCLUSIVE_WAITERS) != 0 && realexslp != 0) {
			if (lk->lk_exslpfail < realexslp) {
				lk->lk_exslpfail = 0;
				queue = SQ_EXCLUSIVE_QUEUE;
				v |= (x & LK_SHARED_WAITERS);
			} else {
				lk->lk_exslpfail = 0;
				LOCK_LOG2(lk,
				    "%s: %p has only LK_SLEEPFAIL sleepers",
				    __func__, lk);
				LOCK_LOG2(lk,
			    "%s: %p waking up threads on the exclusive queue",
				    __func__, lk);
				wakeup_swapper =
				    sleepq_broadcast(&lk->lock_object,
				    SLEEPQ_LK, 0, SQ_EXCLUSIVE_QUEUE);
				queue = SQ_SHARED_QUEUE;
			}
				
		} else {

			/*
			 * Exclusive waiters sleeping with LK_SLEEPFAIL on
			 * and using interruptible sleeps/timeout may have
			 * left spourious lk_exslpfail counts on, so clean
			 * it up anyway.
			 */
			lk->lk_exslpfail = 0;
			queue = SQ_SHARED_QUEUE;
		}

		if (lockmgr_sunlock_try(lk, &orig_x)) {
			sleepq_release(&lk->lock_object);
			break;
		}

		x |= LK_SHARERS_LOCK(1);
		if (!atomic_fcmpset_rel_ptr(&lk->lk_lock, &x, v)) {
			orig_x = x;
			goto retry_sleepq;
		}
		LOCK_LOG3(lk, "%s: %p waking up threads on the %s queue",
		    __func__, lk, queue == SQ_SHARED_QUEUE ? "shared" :
		    "exclusive");
		wakeup_swapper |= sleepq_broadcast(&lk->lock_object, SLEEPQ_LK,
		    0, queue);
		sleepq_release(&lk->lock_object);
		break;
	}

	lockmgr_note_shared_release(lk, file, line);
	return (wakeup_swapper);
}

static void
assert_lockmgr(const struct lock_object *lock, int what)
{

	panic("lockmgr locks do not support assertions");
}

static void
lock_lockmgr(struct lock_object *lock, uintptr_t how)
{

	panic("lockmgr locks do not support sleep interlocking");
}

static uintptr_t
unlock_lockmgr(struct lock_object *lock)
{

	panic("lockmgr locks do not support sleep interlocking");
}

#ifdef KDTRACE_HOOKS
static int
owner_lockmgr(const struct lock_object *lock, struct thread **owner)
{

	panic("lockmgr locks do not support owner inquiring");
}
#endif

void
lockinit(struct lock *lk, int pri, const char *wmesg, int timo, int flags)
{
	int iflags;

	MPASS((flags & ~LK_INIT_MASK) == 0);
	ASSERT_ATOMIC_LOAD_PTR(lk->lk_lock,
            ("%s: lockmgr not aligned for %s: %p", __func__, wmesg,
            &lk->lk_lock));

	iflags = LO_SLEEPABLE | LO_UPGRADABLE;
	if (flags & LK_CANRECURSE)
		iflags |= LO_RECURSABLE;
	if ((flags & LK_NODUP) == 0)
		iflags |= LO_DUPOK;
	if (flags & LK_NOPROFILE)
		iflags |= LO_NOPROFILE;
	if ((flags & LK_NOWITNESS) == 0)
		iflags |= LO_WITNESS;
	if (flags & LK_QUIET)
		iflags |= LO_QUIET;
	if (flags & LK_IS_VNODE)
		iflags |= LO_IS_VNODE;
	if (flags & LK_NEW)
		iflags |= LO_NEW;
	iflags |= flags & (LK_ADAPTIVE | LK_NOSHARE);

	lock_init(&lk->lock_object, &lock_class_lockmgr, wmesg, NULL, iflags);
	lk->lk_lock = LK_UNLOCKED;
	lk->lk_recurse = 0;
	lk->lk_exslpfail = 0;
	lk->lk_timo = timo;
	lk->lk_pri = pri;
	STACK_ZERO(lk);
}

/*
 * XXX: Gross hacks to manipulate external lock flags after
 * initialization.  Used for certain vnode and buf locks.
 */
void
lockallowshare(struct lock *lk)
{

	lockmgr_assert(lk, KA_XLOCKED);
	lk->lock_object.lo_flags &= ~LK_NOSHARE;
}

void
lockdisableshare(struct lock *lk)
{

	lockmgr_assert(lk, KA_XLOCKED);
	lk->lock_object.lo_flags |= LK_NOSHARE;
}

void
lockallowrecurse(struct lock *lk)
{

	lockmgr_assert(lk, KA_XLOCKED);
	lk->lock_object.lo_flags |= LO_RECURSABLE;
}

void
lockdisablerecurse(struct lock *lk)
{

	lockmgr_assert(lk, KA_XLOCKED);
	lk->lock_object.lo_flags &= ~LO_RECURSABLE;
}

void
lockdestroy(struct lock *lk)
{

	KASSERT(lk->lk_lock == LK_UNLOCKED, ("lockmgr still held"));
	KASSERT(lk->lk_recurse == 0, ("lockmgr still recursed"));
	KASSERT(lk->lk_exslpfail == 0, ("lockmgr still exclusive waiters"));
	lock_destroy(&lk->lock_object);
}

static bool __always_inline
lockmgr_slock_try(struct lock *lk, uintptr_t *xp, int flags, bool fp)
{

	/*
	 * If no other thread has an exclusive lock, or
	 * no exclusive waiter is present, bump the count of
	 * sharers.  Since we have to preserve the state of
	 * waiters, if we fail to acquire the shared lock
	 * loop back and retry.
	 */
	*xp = lk->lk_lock;
	while (LK_CAN_SHARE(*xp, flags, fp)) {
		if (atomic_fcmpset_acq_ptr(&lk->lk_lock, xp,
		    *xp + LK_ONE_SHARER)) {
			return (true);
		}
	}
	return (false);
}

static bool __always_inline
lockmgr_sunlock_try(struct lock *lk, uintptr_t *xp)
{

	for (;;) {
		if (LK_SHARERS(*xp) > 1 || !(*xp & LK_ALL_WAITERS)) {
			if (atomic_fcmpset_rel_ptr(&lk->lk_lock, xp,
			    *xp - LK_ONE_SHARER))
				return (true);
			continue;
		}
		break;
	}
	return (false);
}

static __noinline int
lockmgr_slock_hard(struct lock *lk, u_int flags, struct lock_object *ilk,
    const char *file, int line, struct lockmgr_wait *lwa)
{
	uintptr_t tid, x;
	int error = 0;
	const char *iwmesg;
	int ipri, itimo;

#ifdef LOCK_PROFILING
	uint64_t waittime = 0;
	int contested = 0;
#endif

	if (__predict_false(panicstr != NULL))
		goto out;

	tid = (uintptr_t)curthread;

	if (LK_CAN_WITNESS(flags))
		WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER,
		    file, line, flags & LK_INTERLOCK ? ilk : NULL);
	for (;;) {
		if (lockmgr_slock_try(lk, &x, flags, false))
			break;
#ifdef HWPMC_HOOKS
		PMC_SOFT_CALL( , , lock, failed);
#endif
		lock_profile_obtain_lock_failed(&lk->lock_object,
		    &contested, &waittime);

		/*
		 * If the lock is already held by curthread in
		 * exclusive way avoid a deadlock.
		 */
		if (LK_HOLDER(x) == tid) {
			LOCK_LOG2(lk,
			    "%s: %p already held in exclusive mode",
			    __func__, lk);
			error = EDEADLK;
			break;
		}

		/*
		 * If the lock is expected to not sleep just give up
		 * and return.
		 */
		if (LK_TRYOP(flags)) {
			LOCK_LOG2(lk, "%s: %p fails the try operation",
			    __func__, lk);
			error = EBUSY;
			break;
		}

		/*
		 * Acquire the sleepqueue chain lock because we
		 * probabilly will need to manipulate waiters flags.
		 */
		sleepq_lock(&lk->lock_object);
		x = lk->lk_lock;
retry_sleepq:

		/*
		 * if the lock can be acquired in shared mode, try
		 * again.
		 */
		if (LK_CAN_SHARE(x, flags, false)) {
			sleepq_release(&lk->lock_object);
			continue;
		}

		/*
		 * Try to set the LK_SHARED_WAITERS flag.  If we fail,
		 * loop back and retry.
		 */
		if ((x & LK_SHARED_WAITERS) == 0) {
			if (!atomic_fcmpset_acq_ptr(&lk->lk_lock, &x,
			    x | LK_SHARED_WAITERS)) {
				goto retry_sleepq;
			}
			LOCK_LOG2(lk, "%s: %p set shared waiters flag",
			    __func__, lk);
		}

		if (lwa == NULL) {
			iwmesg = lk->lock_object.lo_name;
			ipri = lk->lk_pri;
			itimo = lk->lk_timo;
		} else {
			iwmesg = lwa->iwmesg;
			ipri = lwa->ipri;
			itimo = lwa->itimo;
		}

		/*
		 * As far as we have been unable to acquire the
		 * shared lock and the shared waiters flag is set,
		 * we will sleep.
		 */
		error = sleeplk(lk, flags, ilk, iwmesg, ipri, itimo,
		    SQ_SHARED_QUEUE);
		flags &= ~LK_INTERLOCK;
		if (error) {
			LOCK_LOG3(lk,
			    "%s: interrupted sleep for %p with %d",
			    __func__, lk, error);
			break;
		}
		LOCK_LOG2(lk, "%s: %p resuming from the sleep queue",
		    __func__, lk);
	}
	if (error == 0) {
#ifdef LOCK_PROFILING
		lockmgr_note_shared_acquire(lk, contested, waittime,
		    file, line, flags);
#else
		lockmgr_note_shared_acquire(lk, 0, 0, file, line,
		    flags);
#endif
	}

out:
	lockmgr_exit(flags, ilk, 0);
	return (error);
}

static __noinline int
lockmgr_xlock_hard(struct lock *lk, u_int flags, struct lock_object *ilk,
    const char *file, int line, struct lockmgr_wait *lwa)
{
	struct lock_class *class;
	uintptr_t tid, x, v;
	int error = 0;
	const char *iwmesg;
	int ipri, itimo;

#ifdef LOCK_PROFILING
	uint64_t waittime = 0;
	int contested = 0;
#endif

	if (__predict_false(panicstr != NULL))
		goto out;

	tid = (uintptr_t)curthread;

	if (LK_CAN_WITNESS(flags))
		WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER |
		    LOP_EXCLUSIVE, file, line, flags & LK_INTERLOCK ?
		    ilk : NULL);

	/*
	 * If curthread already holds the lock and this one is
	 * allowed to recurse, simply recurse on it.
	 */
	if (lockmgr_xlocked(lk)) {
		if ((flags & LK_CANRECURSE) == 0 &&
		    (lk->lock_object.lo_flags & LO_RECURSABLE) == 0) {
			/*
			 * If the lock is expected to not panic just
			 * give up and return.
			 */
			if (LK_TRYOP(flags)) {
				LOCK_LOG2(lk,
				    "%s: %p fails the try operation",
				    __func__, lk);
				error = EBUSY;
				goto out;
			}
			if (flags & LK_INTERLOCK) {
				class = LOCK_CLASS(ilk);
				class->lc_unlock(ilk);
			}
			panic("%s: recursing on non recursive lockmgr %p "
			    "@ %s:%d\n", __func__, lk, file, line);
		}
		lk->lk_recurse++;
		LOCK_LOG2(lk, "%s: %p recursing", __func__, lk);
		LOCK_LOG_LOCK("XLOCK", &lk->lock_object, 0,
		    lk->lk_recurse, file, line);
		WITNESS_LOCK(&lk->lock_object, LOP_EXCLUSIVE |
		    LK_TRYWIT(flags), file, line);
		TD_LOCKS_INC(curthread);
		goto out;
	}

	for (;;) {
		if (lk->lk_lock == LK_UNLOCKED &&
		    atomic_cmpset_acq_ptr(&lk->lk_lock, LK_UNLOCKED, tid))
			break;
#ifdef HWPMC_HOOKS
		PMC_SOFT_CALL( , , lock, failed);
#endif
		lock_profile_obtain_lock_failed(&lk->lock_object,
		    &contested, &waittime);

		/*
		 * If the lock is expected to not sleep just give up
		 * and return.
		 */
		if (LK_TRYOP(flags)) {
			LOCK_LOG2(lk, "%s: %p fails the try operation",
			    __func__, lk);
			error = EBUSY;
			break;
		}

		/*
		 * Acquire the sleepqueue chain lock because we
		 * probabilly will need to manipulate waiters flags.
		 */
		sleepq_lock(&lk->lock_object);
		x = lk->lk_lock;
retry_sleepq:

		/*
		 * if the lock has been released while we spun on
		 * the sleepqueue chain lock just try again.
		 */
		if (x == LK_UNLOCKED) {
			sleepq_release(&lk->lock_object);
			continue;
		}

		/*
		 * The lock can be in the state where there is a
		 * pending queue of waiters, but still no owner.
		 * This happens when the lock is contested and an
		 * owner is going to claim the lock.
		 * If curthread is the one successfully acquiring it
		 * claim lock ownership and return, preserving waiters
		 * flags.
		 */
		v = x & (LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS);
		if ((x & ~v) == LK_UNLOCKED) {
			v &= ~LK_EXCLUSIVE_SPINNERS;
			if (atomic_fcmpset_acq_ptr(&lk->lk_lock, &x,
			    tid | v)) {
				sleepq_release(&lk->lock_object);
				LOCK_LOG2(lk,
				    "%s: %p claimed by a new writer",
				    __func__, lk);
				break;
			}
			goto retry_sleepq;
		}

		/*
		 * Try to set the LK_EXCLUSIVE_WAITERS flag.  If we
		 * fail, loop back and retry.
		 */
		if ((x & LK_EXCLUSIVE_WAITERS) == 0) {
			if (!atomic_fcmpset_ptr(&lk->lk_lock, &x,
			    x | LK_EXCLUSIVE_WAITERS)) {
				goto retry_sleepq;
			}
			LOCK_LOG2(lk, "%s: %p set excl waiters flag",
			    __func__, lk);
		}

		if (lwa == NULL) {
			iwmesg = lk->lock_object.lo_name;
			ipri = lk->lk_pri;
			itimo = lk->lk_timo;
		} else {
			iwmesg = lwa->iwmesg;
			ipri = lwa->ipri;
			itimo = lwa->itimo;
		}

		/*
		 * As far as we have been unable to acquire the
		 * exclusive lock and the exclusive waiters flag
		 * is set, we will sleep.
		 */
		error = sleeplk(lk, flags, ilk, iwmesg, ipri, itimo,
		    SQ_EXCLUSIVE_QUEUE);
		flags &= ~LK_INTERLOCK;
		if (error) {
			LOCK_LOG3(lk,
			    "%s: interrupted sleep for %p with %d",
			    __func__, lk, error);
			break;
		}
		LOCK_LOG2(lk, "%s: %p resuming from the sleep queue",
		    __func__, lk);
	}
	if (error == 0) {
#ifdef LOCK_PROFILING
		lockmgr_note_exclusive_acquire(lk, contested, waittime,
		    file, line, flags);
#else
		lockmgr_note_exclusive_acquire(lk, 0, 0, file, line,
		    flags);
#endif
	}

out:
	lockmgr_exit(flags, ilk, 0);
	return (error);
}

static __noinline int
lockmgr_upgrade(struct lock *lk, u_int flags, struct lock_object *ilk,
    const char *file, int line, struct lockmgr_wait *lwa)
{
	uintptr_t tid, x, v;
	int error = 0;
	int wakeup_swapper = 0;
	int op;

	if (__predict_false(panicstr != NULL))
		goto out;

	tid = (uintptr_t)curthread;

	_lockmgr_assert(lk, KA_SLOCKED, file, line);
	v = lk->lk_lock;
	x = v & LK_ALL_WAITERS;
	v &= LK_EXCLUSIVE_SPINNERS;

	/*
	 * Try to switch from one shared lock to an exclusive one.
	 * We need to preserve waiters flags during the operation.
	 */
	if (atomic_cmpset_ptr(&lk->lk_lock, LK_SHARERS_LOCK(1) | x | v,
	    tid | x)) {
		LOCK_LOG_LOCK("XUPGRADE", &lk->lock_object, 0, 0, file,
		    line);
		WITNESS_UPGRADE(&lk->lock_object, LOP_EXCLUSIVE |
		    LK_TRYWIT(flags), file, line);
		TD_SLOCKS_DEC(curthread);
		goto out;
	}

	op = flags & LK_TYPE_MASK;

	/*
	 * In LK_TRYUPGRADE mode, do not drop the lock,
	 * returning EBUSY instead.
	 */
	if (op == LK_TRYUPGRADE) {
		LOCK_LOG2(lk, "%s: %p failed the nowait upgrade",
		    __func__, lk);
		error = EBUSY;
		goto out;
	}

	/*
	 * We have been unable to succeed in upgrading, so just
	 * give up the shared lock.
	 */
	wakeup_swapper |= wakeupshlk(lk, file, line);
	error = lockmgr_xlock_hard(lk, flags, ilk, file, line, lwa);
	flags &= ~LK_INTERLOCK;
out:
	lockmgr_exit(flags, ilk, wakeup_swapper);
	return (error);
}

int
lockmgr_lock_fast_path(struct lock *lk, u_int flags, struct lock_object *ilk,
    const char *file, int line)
{
	struct lock_class *class;
	uintptr_t x, tid;
	u_int op;
	bool locked;

	if (__predict_false(panicstr != NULL))
		return (0);

	op = flags & LK_TYPE_MASK;
	locked = false;
	switch (op) {
	case LK_SHARED:
		if (LK_CAN_WITNESS(flags))
			WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER,
			    file, line, flags & LK_INTERLOCK ? ilk : NULL);
		if (__predict_false(lk->lock_object.lo_flags & LK_NOSHARE))
			break;
		if (lockmgr_slock_try(lk, &x, flags, true)) {
			lockmgr_note_shared_acquire(lk, 0, 0,
			    file, line, flags);
			locked = true;
		} else {
			return (lockmgr_slock_hard(lk, flags, ilk, file, line,
			    NULL));
		}
		break;
	case LK_EXCLUSIVE:
		if (LK_CAN_WITNESS(flags))
			WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER |
			    LOP_EXCLUSIVE, file, line, flags & LK_INTERLOCK ?
			    ilk : NULL);
		tid = (uintptr_t)curthread;
		if (lk->lk_lock == LK_UNLOCKED &&
		    atomic_cmpset_acq_ptr(&lk->lk_lock, LK_UNLOCKED, tid)) {
			lockmgr_note_exclusive_acquire(lk, 0, 0, file, line,
			    flags);
			locked = true;
		} else {
			return (lockmgr_xlock_hard(lk, flags, ilk, file, line,
			    NULL));
		}
		break;
	case LK_UPGRADE:
	case LK_TRYUPGRADE:
		return (lockmgr_upgrade(lk, flags, ilk, file, line, NULL));
	default:
		break;
	}
	if (__predict_true(locked)) {
		if (__predict_false(flags & LK_INTERLOCK)) {
			class = LOCK_CLASS(ilk);
			class->lc_unlock(ilk);
		}
		return (0);
	} else {
		return (__lockmgr_args(lk, flags, ilk, LK_WMESG_DEFAULT,
		    LK_PRIO_DEFAULT, LK_TIMO_DEFAULT, file, line));
	}
}

static __noinline int
lockmgr_sunlock_hard(struct lock *lk, uintptr_t x, u_int flags, struct lock_object *ilk,
    const char *file, int line)

{
	int wakeup_swapper = 0;

	if (__predict_false(panicstr != NULL))
		goto out;

	wakeup_swapper = wakeupshlk(lk, file, line);

out:
	lockmgr_exit(flags, ilk, wakeup_swapper);
	return (0);
}

static __noinline int
lockmgr_xunlock_hard(struct lock *lk, uintptr_t x, u_int flags, struct lock_object *ilk,
    const char *file, int line)
{
	uintptr_t tid, v;
	int wakeup_swapper = 0;
	u_int realexslp;
	int queue;

	if (__predict_false(panicstr != NULL))
		goto out;

	tid = (uintptr_t)curthread;

	/*
	 * As first option, treact the lock as if it has not
	 * any waiter.
	 * Fix-up the tid var if the lock has been disowned.
	 */
	if (LK_HOLDER(x) == LK_KERNPROC)
		tid = LK_KERNPROC;
	else {
		WITNESS_UNLOCK(&lk->lock_object, LOP_EXCLUSIVE, file, line);
		TD_LOCKS_DEC(curthread);
	}
	LOCK_LOG_LOCK("XUNLOCK", &lk->lock_object, 0, lk->lk_recurse, file, line);

	/*
	 * The lock is held in exclusive mode.
	 * If the lock is recursed also, then unrecurse it.
	 */
	if (lockmgr_xlocked_v(x) && lockmgr_recursed(lk)) {
		LOCK_LOG2(lk, "%s: %p unrecursing", __func__, lk);
		lk->lk_recurse--;
		goto out;
	}
	if (tid != LK_KERNPROC)
		lock_profile_release_lock(&lk->lock_object);

	if (x == tid && atomic_cmpset_rel_ptr(&lk->lk_lock, tid, LK_UNLOCKED))
		goto out;

	sleepq_lock(&lk->lock_object);
	x = lk->lk_lock;
	v = LK_UNLOCKED;

	/*
	 * If the lock has exclusive waiters, give them
	 * preference in order to avoid deadlock with
	 * shared runners up.
	 * If interruptible sleeps left the exclusive queue
	 * empty avoid a starvation for the threads sleeping
	 * on the shared queue by giving them precedence
	 * and cleaning up the exclusive waiters bit anyway.
	 * Please note that lk_exslpfail count may be lying
	 * about the real number of waiters with the
	 * LK_SLEEPFAIL flag on because they may be used in
	 * conjunction with interruptible sleeps so
	 * lk_exslpfail might be considered an 'upper limit'
	 * bound, including the edge cases.
	 */
	MPASS((x & LK_EXCLUSIVE_SPINNERS) == 0);
	realexslp = sleepq_sleepcnt(&lk->lock_object, SQ_EXCLUSIVE_QUEUE);
	if ((x & LK_EXCLUSIVE_WAITERS) != 0 && realexslp != 0) {
		if (lk->lk_exslpfail < realexslp) {
			lk->lk_exslpfail = 0;
			queue = SQ_EXCLUSIVE_QUEUE;
			v |= (x & LK_SHARED_WAITERS);
		} else {
			lk->lk_exslpfail = 0;
			LOCK_LOG2(lk,
			    "%s: %p has only LK_SLEEPFAIL sleepers",
			    __func__, lk);
			LOCK_LOG2(lk,
			    "%s: %p waking up threads on the exclusive queue",
			    __func__, lk);
			wakeup_swapper = sleepq_broadcast(&lk->lock_object,
			    SLEEPQ_LK, 0, SQ_EXCLUSIVE_QUEUE);
			queue = SQ_SHARED_QUEUE;
		}
	} else {

		/*
		 * Exclusive waiters sleeping with LK_SLEEPFAIL
		 * on and using interruptible sleeps/timeout
		 * may have left spourious lk_exslpfail counts
		 * on, so clean it up anyway.
		 */
		lk->lk_exslpfail = 0;
		queue = SQ_SHARED_QUEUE;
	}

	LOCK_LOG3(lk, "%s: %p waking up threads on the %s queue",
	    __func__, lk, queue == SQ_SHARED_QUEUE ? "shared" :
	    "exclusive");
	atomic_store_rel_ptr(&lk->lk_lock, v);
	wakeup_swapper |= sleepq_broadcast(&lk->lock_object, SLEEPQ_LK, 0, queue);
	sleepq_release(&lk->lock_object);

out:
	lockmgr_exit(flags, ilk, wakeup_swapper);
	return (0);
}

int
lockmgr_unlock_fast_path(struct lock *lk, u_int flags, struct lock_object *ilk)
{
	struct lock_class *class;
	uintptr_t x, tid;
	const char *file;
	int line;

	if (__predict_false(panicstr != NULL))
		return (0);

	file = __FILE__;
	line = __LINE__;

	_lockmgr_assert(lk, KA_LOCKED, file, line);
	x = lk->lk_lock;
	if (__predict_true(x & LK_SHARE) != 0) {
		if (lockmgr_sunlock_try(lk, &x)) {
			lockmgr_note_shared_release(lk, file, line);
		} else {
			return (lockmgr_sunlock_hard(lk, x, flags, ilk, file, line));
		}
	} else {
		tid = (uintptr_t)curthread;
		if (!lockmgr_recursed(lk) &&
		    atomic_cmpset_rel_ptr(&lk->lk_lock, tid, LK_UNLOCKED)) {
			lockmgr_note_exclusive_release(lk, file, line);
		} else {
			return (lockmgr_xunlock_hard(lk, x, flags, ilk, file, line));
		}
	}
	if (__predict_false(flags & LK_INTERLOCK)) {
		class = LOCK_CLASS(ilk);
		class->lc_unlock(ilk);
	}
	return (0);
}

int
__lockmgr_args(struct lock *lk, u_int flags, struct lock_object *ilk,
    const char *wmesg, int pri, int timo, const char *file, int line)
{
	GIANT_DECLARE;
	struct lockmgr_wait lwa;
	struct lock_class *class;
	const char *iwmesg;
	uintptr_t tid, v, x;
	u_int op, realexslp;
	int error, ipri, itimo, queue, wakeup_swapper;
#ifdef LOCK_PROFILING
	uint64_t waittime = 0;
	int contested = 0;
#endif

	if (panicstr != NULL)
		return (0);

	error = 0;
	tid = (uintptr_t)curthread;
	op = (flags & LK_TYPE_MASK);
	iwmesg = (wmesg == LK_WMESG_DEFAULT) ? lk->lock_object.lo_name : wmesg;
	ipri = (pri == LK_PRIO_DEFAULT) ? lk->lk_pri : pri;
	itimo = (timo == LK_TIMO_DEFAULT) ? lk->lk_timo : timo;

	lwa.iwmesg = iwmesg;
	lwa.ipri = ipri;
	lwa.itimo = itimo;

	MPASS((flags & ~LK_TOTAL_MASK) == 0);
	KASSERT((op & (op - 1)) == 0,
	    ("%s: Invalid requested operation @ %s:%d", __func__, file, line));
	KASSERT((flags & (LK_NOWAIT | LK_SLEEPFAIL)) == 0 ||
	    (op != LK_DOWNGRADE && op != LK_RELEASE),
	    ("%s: Invalid flags in regard of the operation desired @ %s:%d",
	    __func__, file, line));
	KASSERT((flags & LK_INTERLOCK) == 0 || ilk != NULL,
	    ("%s: LK_INTERLOCK passed without valid interlock @ %s:%d",
	    __func__, file, line));
	KASSERT(kdb_active != 0 || !TD_IS_IDLETHREAD(curthread),
	    ("%s: idle thread %p on lockmgr %s @ %s:%d", __func__, curthread,
	    lk->lock_object.lo_name, file, line));

	class = (flags & LK_INTERLOCK) ? LOCK_CLASS(ilk) : NULL;

	if (lk->lock_object.lo_flags & LK_NOSHARE) {
		switch (op) {
		case LK_SHARED:
			op = LK_EXCLUSIVE;
			break;
		case LK_UPGRADE:
		case LK_TRYUPGRADE:
		case LK_DOWNGRADE:
			_lockmgr_assert(lk, KA_XLOCKED | KA_NOTRECURSED,
			    file, line);
			if (flags & LK_INTERLOCK)
				class->lc_unlock(ilk);
			return (0);
		}
	}

	wakeup_swapper = 0;
	switch (op) {
	case LK_SHARED:
		return (lockmgr_slock_hard(lk, flags, ilk, file, line, &lwa));
		break;
	case LK_UPGRADE:
	case LK_TRYUPGRADE:
		return (lockmgr_upgrade(lk, flags, ilk, file, line, &lwa));
		break;
	case LK_EXCLUSIVE:
		return (lockmgr_xlock_hard(lk, flags, ilk, file, line, &lwa));
		break;
	case LK_DOWNGRADE:
		_lockmgr_assert(lk, KA_XLOCKED, file, line);
		LOCK_LOG_LOCK("XDOWNGRADE", &lk->lock_object, 0, 0, file, line);
		WITNESS_DOWNGRADE(&lk->lock_object, 0, file, line);

		/*
		 * Panic if the lock is recursed.
		 */
		if (lockmgr_xlocked(lk) && lockmgr_recursed(lk)) {
			if (flags & LK_INTERLOCK)
				class->lc_unlock(ilk);
			panic("%s: downgrade a recursed lockmgr %s @ %s:%d\n",
			    __func__, iwmesg, file, line);
		}
		TD_SLOCKS_INC(curthread);

		/*
		 * In order to preserve waiters flags, just spin.
		 */
		for (;;) {
			x = lk->lk_lock;
			MPASS((x & LK_EXCLUSIVE_SPINNERS) == 0);
			x &= LK_ALL_WAITERS;
			if (atomic_cmpset_rel_ptr(&lk->lk_lock, tid | x,
			    LK_SHARERS_LOCK(1) | x))
				break;
			cpu_spinwait();
		}
		break;
	case LK_RELEASE:
		_lockmgr_assert(lk, KA_LOCKED, file, line);
		x = lk->lk_lock;

		if (__predict_true(x & LK_SHARE) != 0) {
			return (lockmgr_sunlock_hard(lk, x, flags, ilk, file, line));
		} else {
			return (lockmgr_xunlock_hard(lk, x, flags, ilk, file, line));
		}
		break;
	case LK_DRAIN:
		if (LK_CAN_WITNESS(flags))
			WITNESS_CHECKORDER(&lk->lock_object, LOP_NEWORDER |
			    LOP_EXCLUSIVE, file, line, flags & LK_INTERLOCK ?
			    ilk : NULL);

		/*
		 * Trying to drain a lock we already own will result in a
		 * deadlock.
		 */
		if (lockmgr_xlocked(lk)) {
			if (flags & LK_INTERLOCK)
				class->lc_unlock(ilk);
			panic("%s: draining %s with the lock held @ %s:%d\n",
			    __func__, iwmesg, file, line);
		}

		for (;;) {
			if (lk->lk_lock == LK_UNLOCKED &&
			    atomic_cmpset_acq_ptr(&lk->lk_lock, LK_UNLOCKED, tid))
				break;

#ifdef HWPMC_HOOKS
			PMC_SOFT_CALL( , , lock, failed);
#endif
			lock_profile_obtain_lock_failed(&lk->lock_object,
			    &contested, &waittime);

			/*
			 * If the lock is expected to not sleep just give up
			 * and return.
			 */
			if (LK_TRYOP(flags)) {
				LOCK_LOG2(lk, "%s: %p fails the try operation",
				    __func__, lk);
				error = EBUSY;
				break;
			}

			/*
			 * Acquire the sleepqueue chain lock because we
			 * probabilly will need to manipulate waiters flags.
			 */
			sleepq_lock(&lk->lock_object);
			x = lk->lk_lock;

			/*
			 * if the lock has been released while we spun on
			 * the sleepqueue chain lock just try again.
			 */
			if (x == LK_UNLOCKED) {
				sleepq_release(&lk->lock_object);
				continue;
			}

			v = x & (LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS);
			if ((x & ~v) == LK_UNLOCKED) {
				v = (x & ~LK_EXCLUSIVE_SPINNERS);

				/*
				 * If interruptible sleeps left the exclusive
				 * queue empty avoid a starvation for the
				 * threads sleeping on the shared queue by
				 * giving them precedence and cleaning up the
				 * exclusive waiters bit anyway.
				 * Please note that lk_exslpfail count may be
				 * lying about the real number of waiters with
				 * the LK_SLEEPFAIL flag on because they may
				 * be used in conjunction with interruptible
				 * sleeps so lk_exslpfail might be considered
				 * an 'upper limit' bound, including the edge
				 * cases.
				 */
				if (v & LK_EXCLUSIVE_WAITERS) {
					queue = SQ_EXCLUSIVE_QUEUE;
					v &= ~LK_EXCLUSIVE_WAITERS;
				} else {

					/*
					 * Exclusive waiters sleeping with
					 * LK_SLEEPFAIL on and using
					 * interruptible sleeps/timeout may
					 * have left spourious lk_exslpfail
					 * counts on, so clean it up anyway.
					 */
					MPASS(v & LK_SHARED_WAITERS);
					lk->lk_exslpfail = 0;
					queue = SQ_SHARED_QUEUE;
					v &= ~LK_SHARED_WAITERS;
				}
				if (queue == SQ_EXCLUSIVE_QUEUE) {
					realexslp =
					    sleepq_sleepcnt(&lk->lock_object,
					    SQ_EXCLUSIVE_QUEUE);
					if (lk->lk_exslpfail >= realexslp) {
						lk->lk_exslpfail = 0;
						queue = SQ_SHARED_QUEUE;
						v &= ~LK_SHARED_WAITERS;
						if (realexslp != 0) {
							LOCK_LOG2(lk,
					"%s: %p has only LK_SLEEPFAIL sleepers",
							    __func__, lk);
							LOCK_LOG2(lk,
			"%s: %p waking up threads on the exclusive queue",
							    __func__, lk);
							wakeup_swapper =
							    sleepq_broadcast(
							    &lk->lock_object,
							    SLEEPQ_LK, 0,
							    SQ_EXCLUSIVE_QUEUE);
						}
					} else
						lk->lk_exslpfail = 0;
				}
				if (!atomic_cmpset_ptr(&lk->lk_lock, x, v)) {
					sleepq_release(&lk->lock_object);
					continue;
				}
				LOCK_LOG3(lk,
				"%s: %p waking up all threads on the %s queue",
				    __func__, lk, queue == SQ_SHARED_QUEUE ?
				    "shared" : "exclusive");
				wakeup_swapper |= sleepq_broadcast(
				    &lk->lock_object, SLEEPQ_LK, 0, queue);

				/*
				 * If shared waiters have been woken up we need
				 * to wait for one of them to acquire the lock
				 * before to set the exclusive waiters in
				 * order to avoid a deadlock.
				 */
				if (queue == SQ_SHARED_QUEUE) {
					for (v = lk->lk_lock;
					    (v & LK_SHARE) && !LK_SHARERS(v);
					    v = lk->lk_lock)
						cpu_spinwait();
				}
			}

			/*
			 * Try to set the LK_EXCLUSIVE_WAITERS flag.  If we
			 * fail, loop back and retry.
			 */
			if ((x & LK_EXCLUSIVE_WAITERS) == 0) {
				if (!atomic_cmpset_ptr(&lk->lk_lock, x,
				    x | LK_EXCLUSIVE_WAITERS)) {
					sleepq_release(&lk->lock_object);
					continue;
				}
				LOCK_LOG2(lk, "%s: %p set drain waiters flag",
				    __func__, lk);
			}

			/*
			 * As far as we have been unable to acquire the
			 * exclusive lock and the exclusive waiters flag
			 * is set, we will sleep.
			 */
			if (flags & LK_INTERLOCK) {
				class->lc_unlock(ilk);
				flags &= ~LK_INTERLOCK;
			}
			GIANT_SAVE();
			sleepq_add(&lk->lock_object, NULL, iwmesg, SLEEPQ_LK,
			    SQ_EXCLUSIVE_QUEUE);
			sleepq_wait(&lk->lock_object, ipri & PRIMASK);
			GIANT_RESTORE();
			LOCK_LOG2(lk, "%s: %p resuming from the sleep queue",
			    __func__, lk);
		}

		if (error == 0) {
			lock_profile_obtain_lock_success(&lk->lock_object,
			    contested, waittime, file, line);
			LOCK_LOG_LOCK("DRAIN", &lk->lock_object, 0,
			    lk->lk_recurse, file, line);
			WITNESS_LOCK(&lk->lock_object, LOP_EXCLUSIVE |
			    LK_TRYWIT(flags), file, line);
			TD_LOCKS_INC(curthread);
			STACK_SAVE(lk);
		}
		break;
	default:
		if (flags & LK_INTERLOCK)
			class->lc_unlock(ilk);
		panic("%s: unknown lockmgr request 0x%x\n", __func__, op);
	}

	if (flags & LK_INTERLOCK)
		class->lc_unlock(ilk);
	if (wakeup_swapper)
		kick_proc0();

	return (error);
}

void
_lockmgr_disown(struct lock *lk, const char *file, int line)
{
	uintptr_t tid, x;

	if (SCHEDULER_STOPPED())
		return;

	tid = (uintptr_t)curthread;
	_lockmgr_assert(lk, KA_XLOCKED, file, line);

	/*
	 * Panic if the lock is recursed.
	 */
	if (lockmgr_xlocked(lk) && lockmgr_recursed(lk))
		panic("%s: disown a recursed lockmgr @ %s:%d\n",
		    __func__,  file, line);

	/*
	 * If the owner is already LK_KERNPROC just skip the whole operation.
	 */
	if (LK_HOLDER(lk->lk_lock) != tid)
		return;
	lock_profile_release_lock(&lk->lock_object);
	LOCK_LOG_LOCK("XDISOWN", &lk->lock_object, 0, 0, file, line);
	WITNESS_UNLOCK(&lk->lock_object, LOP_EXCLUSIVE, file, line);
	TD_LOCKS_DEC(curthread);
	STACK_SAVE(lk);

	/*
	 * In order to preserve waiters flags, just spin.
	 */
	for (;;) {
		x = lk->lk_lock;
		MPASS((x & LK_EXCLUSIVE_SPINNERS) == 0);
		x &= LK_ALL_WAITERS;
		if (atomic_cmpset_rel_ptr(&lk->lk_lock, tid | x,
		    LK_KERNPROC | x))
			return;
		cpu_spinwait();
	}
}

void
lockmgr_printinfo(const struct lock *lk)
{
	struct thread *td;
	uintptr_t x;

	if (lk->lk_lock == LK_UNLOCKED)
		printf("lock type %s: UNLOCKED\n", lk->lock_object.lo_name);
	else if (lk->lk_lock & LK_SHARE)
		printf("lock type %s: SHARED (count %ju)\n",
		    lk->lock_object.lo_name,
		    (uintmax_t)LK_SHARERS(lk->lk_lock));
	else {
		td = lockmgr_xholder(lk);
		if (td == (struct thread *)LK_KERNPROC)
			printf("lock type %s: EXCL by KERNPROC\n",
			    lk->lock_object.lo_name);
		else
			printf("lock type %s: EXCL by thread %p "
			    "(pid %d, %s, tid %d)\n", lk->lock_object.lo_name,
			    td, td->td_proc->p_pid, td->td_proc->p_comm,
			    td->td_tid);
	}

	x = lk->lk_lock;
	if (x & LK_EXCLUSIVE_WAITERS)
		printf(" with exclusive waiters pending\n");
	if (x & LK_SHARED_WAITERS)
		printf(" with shared waiters pending\n");
	if (x & LK_EXCLUSIVE_SPINNERS)
		printf(" with exclusive spinners pending\n");

	STACK_PRINT(lk);
}

int
lockstatus(const struct lock *lk)
{
	uintptr_t v, x;
	int ret;

	ret = LK_SHARED;
	x = lk->lk_lock;
	v = LK_HOLDER(x);

	if ((x & LK_SHARE) == 0) {
		if (v == (uintptr_t)curthread || v == LK_KERNPROC)
			ret = LK_EXCLUSIVE;
		else
			ret = LK_EXCLOTHER;
	} else if (x == LK_UNLOCKED)
		ret = 0;

	return (ret);
}

#ifdef INVARIANT_SUPPORT

FEATURE(invariant_support,
    "Support for modules compiled with INVARIANTS option");

#ifndef INVARIANTS
#undef	_lockmgr_assert
#endif

void
_lockmgr_assert(const struct lock *lk, int what, const char *file, int line)
{
	int slocked = 0;

	if (panicstr != NULL)
		return;
	switch (what) {
	case KA_SLOCKED:
	case KA_SLOCKED | KA_NOTRECURSED:
	case KA_SLOCKED | KA_RECURSED:
		slocked = 1;
	case KA_LOCKED:
	case KA_LOCKED | KA_NOTRECURSED:
	case KA_LOCKED | KA_RECURSED:
#ifdef WITNESS

		/*
		 * We cannot trust WITNESS if the lock is held in exclusive
		 * mode and a call to lockmgr_disown() happened.
		 * Workaround this skipping the check if the lock is held in
		 * exclusive mode even for the KA_LOCKED case.
		 */
		if (slocked || (lk->lk_lock & LK_SHARE)) {
			witness_assert(&lk->lock_object, what, file, line);
			break;
		}
#endif
		if (lk->lk_lock == LK_UNLOCKED ||
		    ((lk->lk_lock & LK_SHARE) == 0 && (slocked ||
		    (!lockmgr_xlocked(lk) && !lockmgr_disowned(lk)))))
			panic("Lock %s not %slocked @ %s:%d\n",
			    lk->lock_object.lo_name, slocked ? "share" : "",
			    file, line);

		if ((lk->lk_lock & LK_SHARE) == 0) {
			if (lockmgr_recursed(lk)) {
				if (what & KA_NOTRECURSED)
					panic("Lock %s recursed @ %s:%d\n",
					    lk->lock_object.lo_name, file,
					    line);
			} else if (what & KA_RECURSED)
				panic("Lock %s not recursed @ %s:%d\n",
				    lk->lock_object.lo_name, file, line);
		}
		break;
	case KA_XLOCKED:
	case KA_XLOCKED | KA_NOTRECURSED:
	case KA_XLOCKED | KA_RECURSED:
		if (!lockmgr_xlocked(lk) && !lockmgr_disowned(lk))
			panic("Lock %s not exclusively locked @ %s:%d\n",
			    lk->lock_object.lo_name, file, line);
		if (lockmgr_recursed(lk)) {
			if (what & KA_NOTRECURSED)
				panic("Lock %s recursed @ %s:%d\n",
				    lk->lock_object.lo_name, file, line);
		} else if (what & KA_RECURSED)
			panic("Lock %s not recursed @ %s:%d\n",
			    lk->lock_object.lo_name, file, line);
		break;
	case KA_UNLOCKED:
		if (lockmgr_xlocked(lk) || lockmgr_disowned(lk))
			panic("Lock %s exclusively locked @ %s:%d\n",
			    lk->lock_object.lo_name, file, line);
		break;
	default:
		panic("Unknown lockmgr assertion: %d @ %s:%d\n", what, file,
		    line);
	}
}
#endif

#ifdef DDB
int
lockmgr_chain(struct thread *td, struct thread **ownerp)
{
	struct lock *lk;

	lk = td->td_wchan;

	if (LOCK_CLASS(&lk->lock_object) != &lock_class_lockmgr)
		return (0);
	db_printf("blocked on lockmgr %s", lk->lock_object.lo_name);
	if (lk->lk_lock & LK_SHARE)
		db_printf("SHARED (count %ju)\n",
		    (uintmax_t)LK_SHARERS(lk->lk_lock));
	else
		db_printf("EXCL\n");
	*ownerp = lockmgr_xholder(lk);

	return (1);
}

static void
db_show_lockmgr(const struct lock_object *lock)
{
	struct thread *td;
	const struct lock *lk;

	lk = (const struct lock *)lock;

	db_printf(" state: ");
	if (lk->lk_lock == LK_UNLOCKED)
		db_printf("UNLOCKED\n");
	else if (lk->lk_lock & LK_SHARE)
		db_printf("SLOCK: %ju\n", (uintmax_t)LK_SHARERS(lk->lk_lock));
	else {
		td = lockmgr_xholder(lk);
		if (td == (struct thread *)LK_KERNPROC)
			db_printf("XLOCK: LK_KERNPROC\n");
		else
			db_printf("XLOCK: %p (tid %d, pid %d, \"%s\")\n", td,
			    td->td_tid, td->td_proc->p_pid,
			    td->td_proc->p_comm);
		if (lockmgr_recursed(lk))
			db_printf(" recursed: %d\n", lk->lk_recurse);
	}
	db_printf(" waiters: ");
	switch (lk->lk_lock & LK_ALL_WAITERS) {
	case LK_SHARED_WAITERS:
		db_printf("shared\n");
		break;
	case LK_EXCLUSIVE_WAITERS:
		db_printf("exclusive\n");
		break;
	case LK_ALL_WAITERS:
		db_printf("shared and exclusive\n");
		break;
	default:
		db_printf("none\n");
	}
	db_printf(" spinners: ");
	if (lk->lk_lock & LK_EXCLUSIVE_SPINNERS)
		db_printf("exclusive\n");
	else
		db_printf("none\n");
}
#endif
