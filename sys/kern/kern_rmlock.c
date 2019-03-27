/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 Stephan Uphoff <ups@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/kernel.h>
#include <sys/kdb.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rmlock.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/turnstile.h>
#include <sys/lock_profile.h>
#include <machine/cpu.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

/*
 * A cookie to mark destroyed rmlocks.  This is stored in the head of
 * rm_activeReaders.
 */
#define	RM_DESTROYED	((void *)0xdead)

#define	rm_destroyed(rm)						\
	(LIST_FIRST(&(rm)->rm_activeReaders) == RM_DESTROYED)

#define RMPF_ONQUEUE	1
#define RMPF_SIGNAL	2

#ifndef INVARIANTS
#define	_rm_assert(c, what, file, line)
#endif

static void	assert_rm(const struct lock_object *lock, int what);
#ifdef DDB
static void	db_show_rm(const struct lock_object *lock);
#endif
static void	lock_rm(struct lock_object *lock, uintptr_t how);
#ifdef KDTRACE_HOOKS
static int	owner_rm(const struct lock_object *lock, struct thread **owner);
#endif
static uintptr_t unlock_rm(struct lock_object *lock);

struct lock_class lock_class_rm = {
	.lc_name = "rm",
	.lc_flags = LC_SLEEPLOCK | LC_RECURSABLE,
	.lc_assert = assert_rm,
#ifdef DDB
	.lc_ddb_show = db_show_rm,
#endif
	.lc_lock = lock_rm,
	.lc_unlock = unlock_rm,
#ifdef KDTRACE_HOOKS
	.lc_owner = owner_rm,
#endif
};

struct lock_class lock_class_rm_sleepable = {
	.lc_name = "sleepable rm",
	.lc_flags = LC_SLEEPLOCK | LC_SLEEPABLE | LC_RECURSABLE,
	.lc_assert = assert_rm,
#ifdef DDB
	.lc_ddb_show = db_show_rm,
#endif
	.lc_lock = lock_rm,
	.lc_unlock = unlock_rm,
#ifdef KDTRACE_HOOKS
	.lc_owner = owner_rm,
#endif
};

static void
assert_rm(const struct lock_object *lock, int what)
{

	rm_assert((const struct rmlock *)lock, what);
}

static void
lock_rm(struct lock_object *lock, uintptr_t how)
{
	struct rmlock *rm;
	struct rm_priotracker *tracker;

	rm = (struct rmlock *)lock;
	if (how == 0)
		rm_wlock(rm);
	else {
		tracker = (struct rm_priotracker *)how;
		rm_rlock(rm, tracker);
	}
}

static uintptr_t
unlock_rm(struct lock_object *lock)
{
	struct thread *td;
	struct pcpu *pc;
	struct rmlock *rm;
	struct rm_queue *queue;
	struct rm_priotracker *tracker;
	uintptr_t how;

	rm = (struct rmlock *)lock;
	tracker = NULL;
	how = 0;
	rm_assert(rm, RA_LOCKED | RA_NOTRECURSED);
	if (rm_wowned(rm))
		rm_wunlock(rm);
	else {
		/*
		 * Find the right rm_priotracker structure for curthread.
		 * The guarantee about its uniqueness is given by the fact
		 * we already asserted the lock wasn't recursively acquired.
		 */
		critical_enter();
		td = curthread;
		pc = get_pcpu();
		for (queue = pc->pc_rm_queue.rmq_next;
		    queue != &pc->pc_rm_queue; queue = queue->rmq_next) {
			tracker = (struct rm_priotracker *)queue;
				if ((tracker->rmp_rmlock == rm) &&
				    (tracker->rmp_thread == td)) {
					how = (uintptr_t)tracker;
					break;
				}
		}
		KASSERT(tracker != NULL,
		    ("rm_priotracker is non-NULL when lock held in read mode"));
		critical_exit();
		rm_runlock(rm, tracker);
	}
	return (how);
}

#ifdef KDTRACE_HOOKS
static int
owner_rm(const struct lock_object *lock, struct thread **owner)
{
	const struct rmlock *rm;
	struct lock_class *lc;

	rm = (const struct rmlock *)lock;
	lc = LOCK_CLASS(&rm->rm_wlock_object);
	return (lc->lc_owner(&rm->rm_wlock_object, owner));
}
#endif

static struct mtx rm_spinlock;

MTX_SYSINIT(rm_spinlock, &rm_spinlock, "rm_spinlock", MTX_SPIN);

/*
 * Add or remove tracker from per-cpu list.
 *
 * The per-cpu list can be traversed at any time in forward direction from an
 * interrupt on the *local* cpu.
 */
static void inline
rm_tracker_add(struct pcpu *pc, struct rm_priotracker *tracker)
{
	struct rm_queue *next;

	/* Initialize all tracker pointers */
	tracker->rmp_cpuQueue.rmq_prev = &pc->pc_rm_queue;
	next = pc->pc_rm_queue.rmq_next;
	tracker->rmp_cpuQueue.rmq_next = next;

	/* rmq_prev is not used during froward traversal. */
	next->rmq_prev = &tracker->rmp_cpuQueue;

	/* Update pointer to first element. */
	pc->pc_rm_queue.rmq_next = &tracker->rmp_cpuQueue;
}

/*
 * Return a count of the number of trackers the thread 'td' already
 * has on this CPU for the lock 'rm'.
 */
static int
rm_trackers_present(const struct pcpu *pc, const struct rmlock *rm,
    const struct thread *td)
{
	struct rm_queue *queue;
	struct rm_priotracker *tracker;
	int count;

	count = 0;
	for (queue = pc->pc_rm_queue.rmq_next; queue != &pc->pc_rm_queue;
	    queue = queue->rmq_next) {
		tracker = (struct rm_priotracker *)queue;
		if ((tracker->rmp_rmlock == rm) && (tracker->rmp_thread == td))
			count++;
	}
	return (count);
}

static void inline
rm_tracker_remove(struct pcpu *pc, struct rm_priotracker *tracker)
{
	struct rm_queue *next, *prev;

	next = tracker->rmp_cpuQueue.rmq_next;
	prev = tracker->rmp_cpuQueue.rmq_prev;

	/* Not used during forward traversal. */
	next->rmq_prev = prev;

	/* Remove from list. */
	prev->rmq_next = next;
}

static void
rm_cleanIPI(void *arg)
{
	struct pcpu *pc;
	struct rmlock *rm = arg;
	struct rm_priotracker *tracker;
	struct rm_queue *queue;
	pc = get_pcpu();

	for (queue = pc->pc_rm_queue.rmq_next; queue != &pc->pc_rm_queue;
	    queue = queue->rmq_next) {
		tracker = (struct rm_priotracker *)queue;
		if (tracker->rmp_rmlock == rm && tracker->rmp_flags == 0) {
			tracker->rmp_flags = RMPF_ONQUEUE;
			mtx_lock_spin(&rm_spinlock);
			LIST_INSERT_HEAD(&rm->rm_activeReaders, tracker,
			    rmp_qentry);
			mtx_unlock_spin(&rm_spinlock);
		}
	}
}

void
rm_init_flags(struct rmlock *rm, const char *name, int opts)
{
	struct lock_class *lc;
	int liflags, xflags;

	liflags = 0;
	if (!(opts & RM_NOWITNESS))
		liflags |= LO_WITNESS;
	if (opts & RM_RECURSE)
		liflags |= LO_RECURSABLE;
	if (opts & RM_NEW)
		liflags |= LO_NEW;
	rm->rm_writecpus = all_cpus;
	LIST_INIT(&rm->rm_activeReaders);
	if (opts & RM_SLEEPABLE) {
		liflags |= LO_SLEEPABLE;
		lc = &lock_class_rm_sleepable;
		xflags = (opts & RM_NEW ? SX_NEW : 0);
		sx_init_flags(&rm->rm_lock_sx, "rmlock_sx",
		    xflags | SX_NOWITNESS);
	} else {
		lc = &lock_class_rm;
		xflags = (opts & RM_NEW ? MTX_NEW : 0);
		mtx_init(&rm->rm_lock_mtx, name, "rmlock_mtx",
		    xflags | MTX_NOWITNESS);
	}
	lock_init(&rm->lock_object, lc, name, NULL, liflags);
}

void
rm_init(struct rmlock *rm, const char *name)
{

	rm_init_flags(rm, name, 0);
}

void
rm_destroy(struct rmlock *rm)
{

	rm_assert(rm, RA_UNLOCKED);
	LIST_FIRST(&rm->rm_activeReaders) = RM_DESTROYED;
	if (rm->lock_object.lo_flags & LO_SLEEPABLE)
		sx_destroy(&rm->rm_lock_sx);
	else
		mtx_destroy(&rm->rm_lock_mtx);
	lock_destroy(&rm->lock_object);
}

int
rm_wowned(const struct rmlock *rm)
{

	if (rm->lock_object.lo_flags & LO_SLEEPABLE)
		return (sx_xlocked(&rm->rm_lock_sx));
	else
		return (mtx_owned(&rm->rm_lock_mtx));
}

void
rm_sysinit(void *arg)
{
	struct rm_args *args;

	args = arg;
	rm_init_flags(args->ra_rm, args->ra_desc, args->ra_flags);
}

static __noinline int
_rm_rlock_hard(struct rmlock *rm, struct rm_priotracker *tracker, int trylock)
{
	struct pcpu *pc;

	critical_enter();
	pc = get_pcpu();

	/* Check if we just need to do a proper critical_exit. */
	if (!CPU_ISSET(pc->pc_cpuid, &rm->rm_writecpus)) {
		critical_exit();
		return (1);
	}

	/* Remove our tracker from the per-cpu list. */
	rm_tracker_remove(pc, tracker);

	/* Check to see if the IPI granted us the lock after all. */
	if (tracker->rmp_flags) {
		/* Just add back tracker - we hold the lock. */
		rm_tracker_add(pc, tracker);
		critical_exit();
		return (1);
	}

	/*
	 * We allow readers to acquire a lock even if a writer is blocked if
	 * the lock is recursive and the reader already holds the lock.
	 */
	if ((rm->lock_object.lo_flags & LO_RECURSABLE) != 0) {
		/*
		 * Just grant the lock if this thread already has a tracker
		 * for this lock on the per-cpu queue.
		 */
		if (rm_trackers_present(pc, rm, curthread) != 0) {
			mtx_lock_spin(&rm_spinlock);
			LIST_INSERT_HEAD(&rm->rm_activeReaders, tracker,
			    rmp_qentry);
			tracker->rmp_flags = RMPF_ONQUEUE;
			mtx_unlock_spin(&rm_spinlock);
			rm_tracker_add(pc, tracker);
			critical_exit();
			return (1);
		}
	}

	sched_unpin();
	critical_exit();

	if (trylock) {
		if (rm->lock_object.lo_flags & LO_SLEEPABLE) {
			if (!sx_try_xlock(&rm->rm_lock_sx))
				return (0);
		} else {
			if (!mtx_trylock(&rm->rm_lock_mtx))
				return (0);
		}
	} else {
		if (rm->lock_object.lo_flags & LO_SLEEPABLE) {
			THREAD_SLEEPING_OK();
			sx_xlock(&rm->rm_lock_sx);
			THREAD_NO_SLEEPING();
		} else
			mtx_lock(&rm->rm_lock_mtx);
	}

	critical_enter();
	pc = get_pcpu();
	CPU_CLR(pc->pc_cpuid, &rm->rm_writecpus);
	rm_tracker_add(pc, tracker);
	sched_pin();
	critical_exit();

	if (rm->lock_object.lo_flags & LO_SLEEPABLE)
		sx_xunlock(&rm->rm_lock_sx);
	else
		mtx_unlock(&rm->rm_lock_mtx);

	return (1);
}

int
_rm_rlock(struct rmlock *rm, struct rm_priotracker *tracker, int trylock)
{
	struct thread *td = curthread;
	struct pcpu *pc;

	if (SCHEDULER_STOPPED())
		return (1);

	tracker->rmp_flags  = 0;
	tracker->rmp_thread = td;
	tracker->rmp_rmlock = rm;

	if (rm->lock_object.lo_flags & LO_SLEEPABLE)
		THREAD_NO_SLEEPING();

	td->td_critnest++;	/* critical_enter(); */

	__compiler_membar();

	pc = cpuid_to_pcpu[td->td_oncpu]; /* pcpu_find(td->td_oncpu); */

	rm_tracker_add(pc, tracker);

	sched_pin();

	__compiler_membar();

	td->td_critnest--;

	/*
	 * Fast path to combine two common conditions into a single
	 * conditional jump.
	 */
	if (__predict_true(0 == (td->td_owepreempt |
	    CPU_ISSET(pc->pc_cpuid, &rm->rm_writecpus))))
		return (1);

	/* We do not have a read token and need to acquire one. */
	return _rm_rlock_hard(rm, tracker, trylock);
}

static __noinline void
_rm_unlock_hard(struct thread *td,struct rm_priotracker *tracker)
{

	if (td->td_owepreempt) {
		td->td_critnest++;
		critical_exit();
	}

	if (!tracker->rmp_flags)
		return;

	mtx_lock_spin(&rm_spinlock);
	LIST_REMOVE(tracker, rmp_qentry);

	if (tracker->rmp_flags & RMPF_SIGNAL) {
		struct rmlock *rm;
		struct turnstile *ts;

		rm = tracker->rmp_rmlock;

		turnstile_chain_lock(&rm->lock_object);
		mtx_unlock_spin(&rm_spinlock);

		ts = turnstile_lookup(&rm->lock_object);

		turnstile_signal(ts, TS_EXCLUSIVE_QUEUE);
		turnstile_unpend(ts);
		turnstile_chain_unlock(&rm->lock_object);
	} else
		mtx_unlock_spin(&rm_spinlock);
}

void
_rm_runlock(struct rmlock *rm, struct rm_priotracker *tracker)
{
	struct pcpu *pc;
	struct thread *td = tracker->rmp_thread;

	if (SCHEDULER_STOPPED())
		return;

	td->td_critnest++;	/* critical_enter(); */
	pc = cpuid_to_pcpu[td->td_oncpu]; /* pcpu_find(td->td_oncpu); */
	rm_tracker_remove(pc, tracker);
	td->td_critnest--;
	sched_unpin();

	if (rm->lock_object.lo_flags & LO_SLEEPABLE)
		THREAD_SLEEPING_OK();

	if (__predict_true(0 == (td->td_owepreempt | tracker->rmp_flags)))
		return;

	_rm_unlock_hard(td, tracker);
}

void
_rm_wlock(struct rmlock *rm)
{
	struct rm_priotracker *prio;
	struct turnstile *ts;
	cpuset_t readcpus;

	if (SCHEDULER_STOPPED())
		return;

	if (rm->lock_object.lo_flags & LO_SLEEPABLE)
		sx_xlock(&rm->rm_lock_sx);
	else
		mtx_lock(&rm->rm_lock_mtx);

	if (CPU_CMP(&rm->rm_writecpus, &all_cpus)) {
		/* Get all read tokens back */
		readcpus = all_cpus;
		CPU_NAND(&readcpus, &rm->rm_writecpus);
		rm->rm_writecpus = all_cpus;

		/*
		 * Assumes rm->rm_writecpus update is visible on other CPUs
		 * before rm_cleanIPI is called.
		 */
#ifdef SMP
		smp_rendezvous_cpus(readcpus,
		    smp_no_rendezvous_barrier,
		    rm_cleanIPI,
		    smp_no_rendezvous_barrier,
		    rm);

#else
		rm_cleanIPI(rm);
#endif

		mtx_lock_spin(&rm_spinlock);
		while ((prio = LIST_FIRST(&rm->rm_activeReaders)) != NULL) {
			ts = turnstile_trywait(&rm->lock_object);
			prio->rmp_flags = RMPF_ONQUEUE | RMPF_SIGNAL;
			mtx_unlock_spin(&rm_spinlock);
			turnstile_wait(ts, prio->rmp_thread,
			    TS_EXCLUSIVE_QUEUE);
			mtx_lock_spin(&rm_spinlock);
		}
		mtx_unlock_spin(&rm_spinlock);
	}
}

void
_rm_wunlock(struct rmlock *rm)
{

	if (rm->lock_object.lo_flags & LO_SLEEPABLE)
		sx_xunlock(&rm->rm_lock_sx);
	else
		mtx_unlock(&rm->rm_lock_mtx);
}

#if LOCK_DEBUG > 0

void
_rm_wlock_debug(struct rmlock *rm, const char *file, int line)
{

	if (SCHEDULER_STOPPED())
		return;

	KASSERT(kdb_active != 0 || !TD_IS_IDLETHREAD(curthread),
	    ("rm_wlock() by idle thread %p on rmlock %s @ %s:%d",
	    curthread, rm->lock_object.lo_name, file, line));
	KASSERT(!rm_destroyed(rm),
	    ("rm_wlock() of destroyed rmlock @ %s:%d", file, line));
	_rm_assert(rm, RA_UNLOCKED, file, line);

	WITNESS_CHECKORDER(&rm->lock_object, LOP_NEWORDER | LOP_EXCLUSIVE,
	    file, line, NULL);

	_rm_wlock(rm);

	LOCK_LOG_LOCK("RMWLOCK", &rm->lock_object, 0, 0, file, line);
	WITNESS_LOCK(&rm->lock_object, LOP_EXCLUSIVE, file, line);
	TD_LOCKS_INC(curthread);
}

void
_rm_wunlock_debug(struct rmlock *rm, const char *file, int line)
{

	if (SCHEDULER_STOPPED())
		return;

	KASSERT(!rm_destroyed(rm),
	    ("rm_wunlock() of destroyed rmlock @ %s:%d", file, line));
	_rm_assert(rm, RA_WLOCKED, file, line);
	WITNESS_UNLOCK(&rm->lock_object, LOP_EXCLUSIVE, file, line);
	LOCK_LOG_LOCK("RMWUNLOCK", &rm->lock_object, 0, 0, file, line);
	_rm_wunlock(rm);
	TD_LOCKS_DEC(curthread);
}

int
_rm_rlock_debug(struct rmlock *rm, struct rm_priotracker *tracker,
    int trylock, const char *file, int line)
{

	if (SCHEDULER_STOPPED())
		return (1);

#ifdef INVARIANTS
	if (!(rm->lock_object.lo_flags & LO_RECURSABLE) && !trylock) {
		critical_enter();
		KASSERT(rm_trackers_present(get_pcpu(), rm,
		    curthread) == 0,
		    ("rm_rlock: recursed on non-recursive rmlock %s @ %s:%d\n",
		    rm->lock_object.lo_name, file, line));
		critical_exit();
	}
#endif
	KASSERT(kdb_active != 0 || !TD_IS_IDLETHREAD(curthread),
	    ("rm_rlock() by idle thread %p on rmlock %s @ %s:%d",
	    curthread, rm->lock_object.lo_name, file, line));
	KASSERT(!rm_destroyed(rm),
	    ("rm_rlock() of destroyed rmlock @ %s:%d", file, line));
	if (!trylock) {
		KASSERT(!rm_wowned(rm),
		    ("rm_rlock: wlock already held for %s @ %s:%d",
		    rm->lock_object.lo_name, file, line));
		WITNESS_CHECKORDER(&rm->lock_object, LOP_NEWORDER, file, line,
		    NULL);
	}

	if (_rm_rlock(rm, tracker, trylock)) {
		if (trylock)
			LOCK_LOG_TRY("RMRLOCK", &rm->lock_object, 0, 1, file,
			    line);
		else
			LOCK_LOG_LOCK("RMRLOCK", &rm->lock_object, 0, 0, file,
			    line);
		WITNESS_LOCK(&rm->lock_object, 0, file, line);
		TD_LOCKS_INC(curthread);
		return (1);
	} else if (trylock)
		LOCK_LOG_TRY("RMRLOCK", &rm->lock_object, 0, 0, file, line);

	return (0);
}

void
_rm_runlock_debug(struct rmlock *rm, struct rm_priotracker *tracker,
    const char *file, int line)
{

	if (SCHEDULER_STOPPED())
		return;

	KASSERT(!rm_destroyed(rm),
	    ("rm_runlock() of destroyed rmlock @ %s:%d", file, line));
	_rm_assert(rm, RA_RLOCKED, file, line);
	WITNESS_UNLOCK(&rm->lock_object, 0, file, line);
	LOCK_LOG_LOCK("RMRUNLOCK", &rm->lock_object, 0, 0, file, line);
	_rm_runlock(rm, tracker);
	TD_LOCKS_DEC(curthread);
}

#else

/*
 * Just strip out file and line arguments if no lock debugging is enabled in
 * the kernel - we are called from a kernel module.
 */
void
_rm_wlock_debug(struct rmlock *rm, const char *file, int line)
{

	_rm_wlock(rm);
}

void
_rm_wunlock_debug(struct rmlock *rm, const char *file, int line)
{

	_rm_wunlock(rm);
}

int
_rm_rlock_debug(struct rmlock *rm, struct rm_priotracker *tracker,
    int trylock, const char *file, int line)
{

	return _rm_rlock(rm, tracker, trylock);
}

void
_rm_runlock_debug(struct rmlock *rm, struct rm_priotracker *tracker,
    const char *file, int line)
{

	_rm_runlock(rm, tracker);
}

#endif

#ifdef INVARIANT_SUPPORT
#ifndef INVARIANTS
#undef _rm_assert
#endif

/*
 * Note that this does not need to use witness_assert() for read lock
 * assertions since an exact count of read locks held by this thread
 * is computable.
 */
void
_rm_assert(const struct rmlock *rm, int what, const char *file, int line)
{
	int count;

	if (SCHEDULER_STOPPED())
		return;
	switch (what) {
	case RA_LOCKED:
	case RA_LOCKED | RA_RECURSED:
	case RA_LOCKED | RA_NOTRECURSED:
	case RA_RLOCKED:
	case RA_RLOCKED | RA_RECURSED:
	case RA_RLOCKED | RA_NOTRECURSED:
		/*
		 * Handle the write-locked case.  Unlike other
		 * primitives, writers can never recurse.
		 */
		if (rm_wowned(rm)) {
			if (what & RA_RLOCKED)
				panic("Lock %s exclusively locked @ %s:%d\n",
				    rm->lock_object.lo_name, file, line);
			if (what & RA_RECURSED)
				panic("Lock %s not recursed @ %s:%d\n",
				    rm->lock_object.lo_name, file, line);
			break;
		}

		critical_enter();
		count = rm_trackers_present(get_pcpu(), rm, curthread);
		critical_exit();

		if (count == 0)
			panic("Lock %s not %slocked @ %s:%d\n",
			    rm->lock_object.lo_name, (what & RA_RLOCKED) ?
			    "read " : "", file, line);
		if (count > 1) {
			if (what & RA_NOTRECURSED)
				panic("Lock %s recursed @ %s:%d\n",
				    rm->lock_object.lo_name, file, line);
		} else if (what & RA_RECURSED)
			panic("Lock %s not recursed @ %s:%d\n",
			    rm->lock_object.lo_name, file, line);
		break;
	case RA_WLOCKED:
		if (!rm_wowned(rm))
			panic("Lock %s not exclusively locked @ %s:%d\n",
			    rm->lock_object.lo_name, file, line);
		break;
	case RA_UNLOCKED:
		if (rm_wowned(rm))
			panic("Lock %s exclusively locked @ %s:%d\n",
			    rm->lock_object.lo_name, file, line);

		critical_enter();
		count = rm_trackers_present(get_pcpu(), rm, curthread);
		critical_exit();

		if (count != 0)
			panic("Lock %s read locked @ %s:%d\n",
			    rm->lock_object.lo_name, file, line);
		break;
	default:
		panic("Unknown rm lock assertion: %d @ %s:%d", what, file,
		    line);
	}
}
#endif /* INVARIANT_SUPPORT */

#ifdef DDB
static void
print_tracker(struct rm_priotracker *tr)
{
	struct thread *td;

	td = tr->rmp_thread;
	db_printf("   thread %p (tid %d, pid %d, \"%s\") {", td, td->td_tid,
	    td->td_proc->p_pid, td->td_name);
	if (tr->rmp_flags & RMPF_ONQUEUE) {
		db_printf("ONQUEUE");
		if (tr->rmp_flags & RMPF_SIGNAL)
			db_printf(",SIGNAL");
	} else
		db_printf("0");
	db_printf("}\n");
}

static void
db_show_rm(const struct lock_object *lock)
{
	struct rm_priotracker *tr;
	struct rm_queue *queue;
	const struct rmlock *rm;
	struct lock_class *lc;
	struct pcpu *pc;

	rm = (const struct rmlock *)lock;
	db_printf(" writecpus: ");
	ddb_display_cpuset(__DEQUALIFY(const cpuset_t *, &rm->rm_writecpus));
	db_printf("\n");
	db_printf(" per-CPU readers:\n");
	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu)
		for (queue = pc->pc_rm_queue.rmq_next;
		    queue != &pc->pc_rm_queue; queue = queue->rmq_next) {
			tr = (struct rm_priotracker *)queue;
			if (tr->rmp_rmlock == rm)
				print_tracker(tr);
		}
	db_printf(" active readers:\n");
	LIST_FOREACH(tr, &rm->rm_activeReaders, rmp_qentry)
		print_tracker(tr);
	lc = LOCK_CLASS(&rm->rm_wlock_object);
	db_printf("Backing write-lock (%s):\n", lc->lc_name);
	lc->lc_ddb_show(&rm->rm_wlock_object);
}
#endif
