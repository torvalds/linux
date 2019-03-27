/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from BSDI $Id: mutex_witness.c,v 1.1.2.20 2000/04/27 03:10:27 cp Exp $
 *	and BSDI $Id: synch_machdep.c,v 2.3.2.39 2000/04/27 03:10:25 cp Exp $
 */

/*
 * Implementation of turnstiles used to hold queue of threads blocked on
 * non-sleepable locks.  Sleepable locks use condition variables to
 * implement their queues.  Turnstiles differ from a sleep queue in that
 * turnstile queue's are assigned to a lock held by an owning thread.  Thus,
 * when one thread is enqueued onto a turnstile, it can lend its priority
 * to the owning thread.
 *
 * We wish to avoid bloating locks with an embedded turnstile and we do not
 * want to use back-pointers in the locks for the same reason.  Thus, we
 * use a similar approach to that of Solaris 7 as described in Solaris
 * Internals by Jim Mauro and Richard McDougall.  Turnstiles are looked up
 * in a hash table based on the address of the lock.  Each entry in the
 * hash table is a linked-lists of turnstiles and is called a turnstile
 * chain.  Each chain contains a spin mutex that protects all of the
 * turnstiles in the chain.
 *
 * Each time a thread is created, a turnstile is allocated from a UMA zone
 * and attached to that thread.  When a thread blocks on a lock, if it is the
 * first thread to block, it lends its turnstile to the lock.  If the lock
 * already has a turnstile, then it gives its turnstile to the lock's
 * turnstile's free list.  When a thread is woken up, it takes a turnstile from
 * the free list if there are any other waiters.  If it is the only thread
 * blocked on the lock, then it reclaims the turnstile associated with the lock
 * and removes it from the hash table.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_turnstile_profiling.h"
#include "opt_sched.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sched.h>
#include <sys/sdt.h>
#include <sys/sysctl.h>
#include <sys/turnstile.h>

#include <vm/uma.h>

#ifdef DDB
#include <ddb/ddb.h>
#include <sys/lockmgr.h>
#include <sys/sx.h>
#endif

/*
 * Constants for the hash table of turnstile chains.  TC_SHIFT is a magic
 * number chosen because the sleep queue's use the same value for the
 * shift.  Basically, we ignore the lower 8 bits of the address.
 * TC_TABLESIZE must be a power of two for TC_MASK to work properly.
 */
#define	TC_TABLESIZE	128			/* Must be power of 2. */
#define	TC_MASK		(TC_TABLESIZE - 1)
#define	TC_SHIFT	8
#define	TC_HASH(lock)	(((uintptr_t)(lock) >> TC_SHIFT) & TC_MASK)
#define	TC_LOOKUP(lock)	&turnstile_chains[TC_HASH(lock)]

/*
 * There are three different lists of turnstiles as follows.  The list
 * connected by ts_link entries is a per-thread list of all the turnstiles
 * attached to locks that we own.  This is used to fixup our priority when
 * a lock is released.  The other two lists use the ts_hash entries.  The
 * first of these two is the turnstile chain list that a turnstile is on
 * when it is attached to a lock.  The second list to use ts_hash is the
 * free list hung off of a turnstile that is attached to a lock.
 *
 * Each turnstile contains three lists of threads.  The two ts_blocked lists
 * are linked list of threads blocked on the turnstile's lock.  One list is
 * for exclusive waiters, and the other is for shared waiters.  The
 * ts_pending list is a linked list of threads previously awakened by
 * turnstile_signal() or turnstile_wait() that are waiting to be put on
 * the run queue.
 *
 * Locking key:
 *  c - turnstile chain lock
 *  q - td_contested lock
 */
struct turnstile {
	struct mtx ts_lock;			/* Spin lock for self. */
	struct threadqueue ts_blocked[2];	/* (c + q) Blocked threads. */
	struct threadqueue ts_pending;		/* (c) Pending threads. */
	LIST_ENTRY(turnstile) ts_hash;		/* (c) Chain and free list. */
	LIST_ENTRY(turnstile) ts_link;		/* (q) Contested locks. */
	LIST_HEAD(, turnstile) ts_free;		/* (c) Free turnstiles. */
	struct lock_object *ts_lockobj;		/* (c) Lock we reference. */
	struct thread *ts_owner;		/* (c + q) Who owns the lock. */
};

struct turnstile_chain {
	LIST_HEAD(, turnstile) tc_turnstiles;	/* List of turnstiles. */
	struct mtx tc_lock;			/* Spin lock for this chain. */
#ifdef TURNSTILE_PROFILING
	u_int	tc_depth;			/* Length of tc_queues. */
	u_int	tc_max_depth;			/* Max length of tc_queues. */
#endif
};

#ifdef TURNSTILE_PROFILING
u_int turnstile_max_depth;
static SYSCTL_NODE(_debug, OID_AUTO, turnstile, CTLFLAG_RD, 0,
    "turnstile profiling");
static SYSCTL_NODE(_debug_turnstile, OID_AUTO, chains, CTLFLAG_RD, 0,
    "turnstile chain stats");
SYSCTL_UINT(_debug_turnstile, OID_AUTO, max_depth, CTLFLAG_RD,
    &turnstile_max_depth, 0, "maximum depth achieved of a single chain");
#endif
static struct mtx td_contested_lock;
static struct turnstile_chain turnstile_chains[TC_TABLESIZE];
static uma_zone_t turnstile_zone;

/*
 * Prototypes for non-exported routines.
 */
static void	init_turnstile0(void *dummy);
#ifdef TURNSTILE_PROFILING
static void	init_turnstile_profiling(void *arg);
#endif
static void	propagate_priority(struct thread *td);
static int	turnstile_adjust_thread(struct turnstile *ts,
		    struct thread *td);
static struct thread *turnstile_first_waiter(struct turnstile *ts);
static void	turnstile_setowner(struct turnstile *ts, struct thread *owner);
#ifdef INVARIANTS
static void	turnstile_dtor(void *mem, int size, void *arg);
#endif
static int	turnstile_init(void *mem, int size, int flags);
static void	turnstile_fini(void *mem, int size);

SDT_PROVIDER_DECLARE(sched);
SDT_PROBE_DEFINE(sched, , , sleep);
SDT_PROBE_DEFINE2(sched, , , wakeup, "struct thread *", 
    "struct proc *");

/*
 * Walks the chain of turnstiles and their owners to propagate the priority
 * of the thread being blocked to all the threads holding locks that have to
 * release their locks before this thread can run again.
 */
static void
propagate_priority(struct thread *td)
{
	struct turnstile *ts;
	int pri;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	pri = td->td_priority;
	ts = td->td_blocked;
	THREAD_LOCKPTR_ASSERT(td, &ts->ts_lock);
	/*
	 * Grab a recursive lock on this turnstile chain so it stays locked
	 * for the whole operation.  The caller expects us to return with
	 * the original lock held.  We only ever lock down the chain so
	 * the lock order is constant.
	 */
	mtx_lock_spin(&ts->ts_lock);
	for (;;) {
		td = ts->ts_owner;

		if (td == NULL) {
			/*
			 * This might be a read lock with no owner.  There's
			 * not much we can do, so just bail.
			 */
			mtx_unlock_spin(&ts->ts_lock);
			return;
		}

		thread_lock_flags(td, MTX_DUPOK);
		mtx_unlock_spin(&ts->ts_lock);
		MPASS(td->td_proc != NULL);
		MPASS(td->td_proc->p_magic == P_MAGIC);

		/*
		 * If the thread is asleep, then we are probably about
		 * to deadlock.  To make debugging this easier, show
		 * backtrace of misbehaving thread and panic to not
		 * leave the kernel deadlocked.
		 */
		if (TD_IS_SLEEPING(td)) {
			printf(
		"Sleeping thread (tid %d, pid %d) owns a non-sleepable lock\n",
			    td->td_tid, td->td_proc->p_pid);
			kdb_backtrace_thread(td);
			panic("sleeping thread");
		}

		/*
		 * If this thread already has higher priority than the
		 * thread that is being blocked, we are finished.
		 */
		if (td->td_priority <= pri) {
			thread_unlock(td);
			return;
		}

		/*
		 * Bump this thread's priority.
		 */
		sched_lend_prio(td, pri);

		/*
		 * If lock holder is actually running or on the run queue
		 * then we are done.
		 */
		if (TD_IS_RUNNING(td) || TD_ON_RUNQ(td)) {
			MPASS(td->td_blocked == NULL);
			thread_unlock(td);
			return;
		}

#ifndef SMP
		/*
		 * For UP, we check to see if td is curthread (this shouldn't
		 * ever happen however as it would mean we are in a deadlock.)
		 */
		KASSERT(td != curthread, ("Deadlock detected"));
#endif

		/*
		 * If we aren't blocked on a lock, we should be.
		 */
		KASSERT(TD_ON_LOCK(td), (
		    "thread %d(%s):%d holds %s but isn't blocked on a lock\n",
		    td->td_tid, td->td_name, td->td_state,
		    ts->ts_lockobj->lo_name));

		/*
		 * Pick up the lock that td is blocked on.
		 */
		ts = td->td_blocked;
		MPASS(ts != NULL);
		THREAD_LOCKPTR_ASSERT(td, &ts->ts_lock);
		/* Resort td on the list if needed. */
		if (!turnstile_adjust_thread(ts, td)) {
			mtx_unlock_spin(&ts->ts_lock);
			return;
		}
		/* The thread lock is released as ts lock above. */
	}
}

/*
 * Adjust the thread's position on a turnstile after its priority has been
 * changed.
 */
static int
turnstile_adjust_thread(struct turnstile *ts, struct thread *td)
{
	struct thread *td1, *td2;
	int queue;

	THREAD_LOCK_ASSERT(td, MA_OWNED);
	MPASS(TD_ON_LOCK(td));

	/*
	 * This thread may not be blocked on this turnstile anymore
	 * but instead might already be woken up on another CPU
	 * that is waiting on the thread lock in turnstile_unpend() to
	 * finish waking this thread up.  We can detect this case
	 * by checking to see if this thread has been given a
	 * turnstile by either turnstile_signal() or
	 * turnstile_broadcast().  In this case, treat the thread as
	 * if it was already running.
	 */
	if (td->td_turnstile != NULL)
		return (0);

	/*
	 * Check if the thread needs to be moved on the blocked chain.
	 * It needs to be moved if either its priority is lower than
	 * the previous thread or higher than the next thread.
	 */
	THREAD_LOCKPTR_ASSERT(td, &ts->ts_lock);
	td1 = TAILQ_PREV(td, threadqueue, td_lockq);
	td2 = TAILQ_NEXT(td, td_lockq);
	if ((td1 != NULL && td->td_priority < td1->td_priority) ||
	    (td2 != NULL && td->td_priority > td2->td_priority)) {

		/*
		 * Remove thread from blocked chain and determine where
		 * it should be moved to.
		 */
		queue = td->td_tsqueue;
		MPASS(queue == TS_EXCLUSIVE_QUEUE || queue == TS_SHARED_QUEUE);
		mtx_lock_spin(&td_contested_lock);
		TAILQ_REMOVE(&ts->ts_blocked[queue], td, td_lockq);
		TAILQ_FOREACH(td1, &ts->ts_blocked[queue], td_lockq) {
			MPASS(td1->td_proc->p_magic == P_MAGIC);
			if (td1->td_priority > td->td_priority)
				break;
		}

		if (td1 == NULL)
			TAILQ_INSERT_TAIL(&ts->ts_blocked[queue], td, td_lockq);
		else
			TAILQ_INSERT_BEFORE(td1, td, td_lockq);
		mtx_unlock_spin(&td_contested_lock);
		if (td1 == NULL)
			CTR3(KTR_LOCK,
		    "turnstile_adjust_thread: td %d put at tail on [%p] %s",
			    td->td_tid, ts->ts_lockobj, ts->ts_lockobj->lo_name);
		else
			CTR4(KTR_LOCK,
		    "turnstile_adjust_thread: td %d moved before %d on [%p] %s",
			    td->td_tid, td1->td_tid, ts->ts_lockobj,
			    ts->ts_lockobj->lo_name);
	}
	return (1);
}

/*
 * Early initialization of turnstiles.  This is not done via a SYSINIT()
 * since this needs to be initialized very early when mutexes are first
 * initialized.
 */
void
init_turnstiles(void)
{
	int i;

	for (i = 0; i < TC_TABLESIZE; i++) {
		LIST_INIT(&turnstile_chains[i].tc_turnstiles);
		mtx_init(&turnstile_chains[i].tc_lock, "turnstile chain",
		    NULL, MTX_SPIN);
	}
	mtx_init(&td_contested_lock, "td_contested", NULL, MTX_SPIN);
	LIST_INIT(&thread0.td_contested);
	thread0.td_turnstile = NULL;
}

#ifdef TURNSTILE_PROFILING
static void
init_turnstile_profiling(void *arg)
{
	struct sysctl_oid *chain_oid;
	char chain_name[10];
	int i;

	for (i = 0; i < TC_TABLESIZE; i++) {
		snprintf(chain_name, sizeof(chain_name), "%d", i);
		chain_oid = SYSCTL_ADD_NODE(NULL, 
		    SYSCTL_STATIC_CHILDREN(_debug_turnstile_chains), OID_AUTO,
		    chain_name, CTLFLAG_RD, NULL, "turnstile chain stats");
		SYSCTL_ADD_UINT(NULL, SYSCTL_CHILDREN(chain_oid), OID_AUTO,
		    "depth", CTLFLAG_RD, &turnstile_chains[i].tc_depth, 0,
		    NULL);
		SYSCTL_ADD_UINT(NULL, SYSCTL_CHILDREN(chain_oid), OID_AUTO,
		    "max_depth", CTLFLAG_RD, &turnstile_chains[i].tc_max_depth,
		    0, NULL);
	}
}
SYSINIT(turnstile_profiling, SI_SUB_LOCK, SI_ORDER_ANY,
    init_turnstile_profiling, NULL);
#endif

static void
init_turnstile0(void *dummy)
{

	turnstile_zone = uma_zcreate("TURNSTILE", sizeof(struct turnstile),
	    NULL,
#ifdef INVARIANTS
	    turnstile_dtor,
#else
	    NULL,
#endif
	    turnstile_init, turnstile_fini, UMA_ALIGN_CACHE, UMA_ZONE_NOFREE);
	thread0.td_turnstile = turnstile_alloc();
}
SYSINIT(turnstile0, SI_SUB_LOCK, SI_ORDER_ANY, init_turnstile0, NULL);

/*
 * Update a thread on the turnstile list after it's priority has been changed.
 * The old priority is passed in as an argument.
 */
void
turnstile_adjust(struct thread *td, u_char oldpri)
{
	struct turnstile *ts;

	MPASS(TD_ON_LOCK(td));

	/*
	 * Pick up the lock that td is blocked on.
	 */
	ts = td->td_blocked;
	MPASS(ts != NULL);
	THREAD_LOCKPTR_ASSERT(td, &ts->ts_lock);
	mtx_assert(&ts->ts_lock, MA_OWNED);

	/* Resort the turnstile on the list. */
	if (!turnstile_adjust_thread(ts, td))
		return;
	/*
	 * If our priority was lowered and we are at the head of the
	 * turnstile, then propagate our new priority up the chain.
	 * Note that we currently don't try to revoke lent priorities
	 * when our priority goes up.
	 */
	MPASS(td->td_tsqueue == TS_EXCLUSIVE_QUEUE ||
	    td->td_tsqueue == TS_SHARED_QUEUE);
	if (td == TAILQ_FIRST(&ts->ts_blocked[td->td_tsqueue]) &&
	    td->td_priority < oldpri) {
		propagate_priority(td);
	}
}

/*
 * Set the owner of the lock this turnstile is attached to.
 */
static void
turnstile_setowner(struct turnstile *ts, struct thread *owner)
{

	mtx_assert(&td_contested_lock, MA_OWNED);
	MPASS(ts->ts_owner == NULL);

	/* A shared lock might not have an owner. */
	if (owner == NULL)
		return;

	MPASS(owner->td_proc->p_magic == P_MAGIC);
	ts->ts_owner = owner;
	LIST_INSERT_HEAD(&owner->td_contested, ts, ts_link);
}

#ifdef INVARIANTS
/*
 * UMA zone item deallocator.
 */
static void
turnstile_dtor(void *mem, int size, void *arg)
{
	struct turnstile *ts;

	ts = mem;
	MPASS(TAILQ_EMPTY(&ts->ts_blocked[TS_EXCLUSIVE_QUEUE]));
	MPASS(TAILQ_EMPTY(&ts->ts_blocked[TS_SHARED_QUEUE]));
	MPASS(TAILQ_EMPTY(&ts->ts_pending));
}
#endif

/*
 * UMA zone item initializer.
 */
static int
turnstile_init(void *mem, int size, int flags)
{
	struct turnstile *ts;

	bzero(mem, size);
	ts = mem;
	TAILQ_INIT(&ts->ts_blocked[TS_EXCLUSIVE_QUEUE]);
	TAILQ_INIT(&ts->ts_blocked[TS_SHARED_QUEUE]);
	TAILQ_INIT(&ts->ts_pending);
	LIST_INIT(&ts->ts_free);
	mtx_init(&ts->ts_lock, "turnstile lock", NULL, MTX_SPIN | MTX_RECURSE);
	return (0);
}

static void
turnstile_fini(void *mem, int size)
{
	struct turnstile *ts;

	ts = mem;
	mtx_destroy(&ts->ts_lock);
}

/*
 * Get a turnstile for a new thread.
 */
struct turnstile *
turnstile_alloc(void)
{

	return (uma_zalloc(turnstile_zone, M_WAITOK));
}

/*
 * Free a turnstile when a thread is destroyed.
 */
void
turnstile_free(struct turnstile *ts)
{

	uma_zfree(turnstile_zone, ts);
}

/*
 * Lock the turnstile chain associated with the specified lock.
 */
void
turnstile_chain_lock(struct lock_object *lock)
{
	struct turnstile_chain *tc;

	tc = TC_LOOKUP(lock);
	mtx_lock_spin(&tc->tc_lock);
}

struct turnstile *
turnstile_trywait(struct lock_object *lock)
{
	struct turnstile_chain *tc;
	struct turnstile *ts;

	tc = TC_LOOKUP(lock);
	mtx_lock_spin(&tc->tc_lock);
	LIST_FOREACH(ts, &tc->tc_turnstiles, ts_hash)
		if (ts->ts_lockobj == lock) {
			mtx_lock_spin(&ts->ts_lock);
			return (ts);
		}

	ts = curthread->td_turnstile;
	MPASS(ts != NULL);
	mtx_lock_spin(&ts->ts_lock);
	KASSERT(ts->ts_lockobj == NULL, ("stale ts_lockobj pointer"));
	ts->ts_lockobj = lock;

	return (ts);
}

struct thread *
turnstile_lock(struct turnstile *ts, struct lock_object **lockp)
{
	struct turnstile_chain *tc;
	struct lock_object *lock;

	if ((lock = ts->ts_lockobj) == NULL)
		return (NULL);
	tc = TC_LOOKUP(lock);
	mtx_lock_spin(&tc->tc_lock);
	mtx_lock_spin(&ts->ts_lock);
	if (__predict_false(lock != ts->ts_lockobj)) {
		mtx_unlock_spin(&tc->tc_lock);
		mtx_unlock_spin(&ts->ts_lock);
		return (NULL);
	}
	*lockp = lock;
	return (ts->ts_owner);
}

void
turnstile_unlock(struct turnstile *ts, struct lock_object *lock)
{
	struct turnstile_chain *tc;

	mtx_assert(&ts->ts_lock, MA_OWNED);
	mtx_unlock_spin(&ts->ts_lock);
	if (ts == curthread->td_turnstile)
		ts->ts_lockobj = NULL;
	tc = TC_LOOKUP(lock);
	mtx_unlock_spin(&tc->tc_lock);
}

void
turnstile_assert(struct turnstile *ts)
{
	MPASS(ts->ts_lockobj == NULL);
}

void
turnstile_cancel(struct turnstile *ts)
{
	struct turnstile_chain *tc;
	struct lock_object *lock;

	mtx_assert(&ts->ts_lock, MA_OWNED);

	mtx_unlock_spin(&ts->ts_lock);
	lock = ts->ts_lockobj;
	if (ts == curthread->td_turnstile)
		ts->ts_lockobj = NULL;
	tc = TC_LOOKUP(lock);
	mtx_unlock_spin(&tc->tc_lock);
}

/*
 * Look up the turnstile for a lock in the hash table locking the associated
 * turnstile chain along the way.  If no turnstile is found in the hash
 * table, NULL is returned.
 */
struct turnstile *
turnstile_lookup(struct lock_object *lock)
{
	struct turnstile_chain *tc;
	struct turnstile *ts;

	tc = TC_LOOKUP(lock);
	mtx_assert(&tc->tc_lock, MA_OWNED);
	LIST_FOREACH(ts, &tc->tc_turnstiles, ts_hash)
		if (ts->ts_lockobj == lock) {
			mtx_lock_spin(&ts->ts_lock);
			return (ts);
		}
	return (NULL);
}

/*
 * Unlock the turnstile chain associated with a given lock.
 */
void
turnstile_chain_unlock(struct lock_object *lock)
{
	struct turnstile_chain *tc;

	tc = TC_LOOKUP(lock);
	mtx_unlock_spin(&tc->tc_lock);
}

/*
 * Return a pointer to the thread waiting on this turnstile with the
 * most important priority or NULL if the turnstile has no waiters.
 */
static struct thread *
turnstile_first_waiter(struct turnstile *ts)
{
	struct thread *std, *xtd;

	std = TAILQ_FIRST(&ts->ts_blocked[TS_SHARED_QUEUE]);
	xtd = TAILQ_FIRST(&ts->ts_blocked[TS_EXCLUSIVE_QUEUE]);
	if (xtd == NULL || (std != NULL && std->td_priority < xtd->td_priority))
		return (std);
	return (xtd);
}

/*
 * Take ownership of a turnstile and adjust the priority of the new
 * owner appropriately.
 */
void
turnstile_claim(struct turnstile *ts)
{
	struct thread *td, *owner;
	struct turnstile_chain *tc;

	mtx_assert(&ts->ts_lock, MA_OWNED);
	MPASS(ts != curthread->td_turnstile);

	owner = curthread;
	mtx_lock_spin(&td_contested_lock);
	turnstile_setowner(ts, owner);
	mtx_unlock_spin(&td_contested_lock);

	td = turnstile_first_waiter(ts);
	MPASS(td != NULL);
	MPASS(td->td_proc->p_magic == P_MAGIC);
	THREAD_LOCKPTR_ASSERT(td, &ts->ts_lock);

	/*
	 * Update the priority of the new owner if needed.
	 */
	thread_lock(owner);
	if (td->td_priority < owner->td_priority)
		sched_lend_prio(owner, td->td_priority);
	thread_unlock(owner);
	tc = TC_LOOKUP(ts->ts_lockobj);
	mtx_unlock_spin(&ts->ts_lock);
	mtx_unlock_spin(&tc->tc_lock);
}

/*
 * Block the current thread on the turnstile assicated with 'lock'.  This
 * function will context switch and not return until this thread has been
 * woken back up.  This function must be called with the appropriate
 * turnstile chain locked and will return with it unlocked.
 */
void
turnstile_wait(struct turnstile *ts, struct thread *owner, int queue)
{
	struct turnstile_chain *tc;
	struct thread *td, *td1;
	struct lock_object *lock;

	td = curthread;
	mtx_assert(&ts->ts_lock, MA_OWNED);
	if (owner)
		MPASS(owner->td_proc->p_magic == P_MAGIC);
	MPASS(queue == TS_SHARED_QUEUE || queue == TS_EXCLUSIVE_QUEUE);

	/*
	 * If the lock does not already have a turnstile, use this thread's
	 * turnstile.  Otherwise insert the current thread into the
	 * turnstile already in use by this lock.
	 */
	tc = TC_LOOKUP(ts->ts_lockobj);
	mtx_assert(&tc->tc_lock, MA_OWNED);
	if (ts == td->td_turnstile) {
#ifdef TURNSTILE_PROFILING
		tc->tc_depth++;
		if (tc->tc_depth > tc->tc_max_depth) {
			tc->tc_max_depth = tc->tc_depth;
			if (tc->tc_max_depth > turnstile_max_depth)
				turnstile_max_depth = tc->tc_max_depth;
		}
#endif
		LIST_INSERT_HEAD(&tc->tc_turnstiles, ts, ts_hash);
		KASSERT(TAILQ_EMPTY(&ts->ts_pending),
		    ("thread's turnstile has pending threads"));
		KASSERT(TAILQ_EMPTY(&ts->ts_blocked[TS_EXCLUSIVE_QUEUE]),
		    ("thread's turnstile has exclusive waiters"));
		KASSERT(TAILQ_EMPTY(&ts->ts_blocked[TS_SHARED_QUEUE]),
		    ("thread's turnstile has shared waiters"));
		KASSERT(LIST_EMPTY(&ts->ts_free),
		    ("thread's turnstile has a non-empty free list"));
		MPASS(ts->ts_lockobj != NULL);
		mtx_lock_spin(&td_contested_lock);
		TAILQ_INSERT_TAIL(&ts->ts_blocked[queue], td, td_lockq);
		turnstile_setowner(ts, owner);
		mtx_unlock_spin(&td_contested_lock);
	} else {
		TAILQ_FOREACH(td1, &ts->ts_blocked[queue], td_lockq)
			if (td1->td_priority > td->td_priority)
				break;
		mtx_lock_spin(&td_contested_lock);
		if (td1 != NULL)
			TAILQ_INSERT_BEFORE(td1, td, td_lockq);
		else
			TAILQ_INSERT_TAIL(&ts->ts_blocked[queue], td, td_lockq);
		MPASS(owner == ts->ts_owner);
		mtx_unlock_spin(&td_contested_lock);
		MPASS(td->td_turnstile != NULL);
		LIST_INSERT_HEAD(&ts->ts_free, td->td_turnstile, ts_hash);
	}
	thread_lock(td);
	thread_lock_set(td, &ts->ts_lock);
	td->td_turnstile = NULL;

	/* Save who we are blocked on and switch. */
	lock = ts->ts_lockobj;
	td->td_tsqueue = queue;
	td->td_blocked = ts;
	td->td_lockname = lock->lo_name;
	td->td_blktick = ticks;
	TD_SET_LOCK(td);
	mtx_unlock_spin(&tc->tc_lock);
	propagate_priority(td);

	if (LOCK_LOG_TEST(lock, 0))
		CTR4(KTR_LOCK, "%s: td %d blocked on [%p] %s", __func__,
		    td->td_tid, lock, lock->lo_name);

	SDT_PROBE0(sched, , , sleep);

	THREAD_LOCKPTR_ASSERT(td, &ts->ts_lock);
	mi_switch(SW_VOL | SWT_TURNSTILE, NULL);

	if (LOCK_LOG_TEST(lock, 0))
		CTR4(KTR_LOCK, "%s: td %d free from blocked on [%p] %s",
		    __func__, td->td_tid, lock, lock->lo_name);
	thread_unlock(td);
}

/*
 * Pick the highest priority thread on this turnstile and put it on the
 * pending list.  This must be called with the turnstile chain locked.
 */
int
turnstile_signal(struct turnstile *ts, int queue)
{
	struct turnstile_chain *tc __unused;
	struct thread *td;
	int empty;

	MPASS(ts != NULL);
	mtx_assert(&ts->ts_lock, MA_OWNED);
	MPASS(curthread->td_proc->p_magic == P_MAGIC);
	MPASS(ts->ts_owner == curthread || ts->ts_owner == NULL);
	MPASS(queue == TS_SHARED_QUEUE || queue == TS_EXCLUSIVE_QUEUE);

	/*
	 * Pick the highest priority thread blocked on this lock and
	 * move it to the pending list.
	 */
	td = TAILQ_FIRST(&ts->ts_blocked[queue]);
	MPASS(td->td_proc->p_magic == P_MAGIC);
	mtx_lock_spin(&td_contested_lock);
	TAILQ_REMOVE(&ts->ts_blocked[queue], td, td_lockq);
	mtx_unlock_spin(&td_contested_lock);
	TAILQ_INSERT_TAIL(&ts->ts_pending, td, td_lockq);

	/*
	 * If the turnstile is now empty, remove it from its chain and
	 * give it to the about-to-be-woken thread.  Otherwise take a
	 * turnstile from the free list and give it to the thread.
	 */
	empty = TAILQ_EMPTY(&ts->ts_blocked[TS_EXCLUSIVE_QUEUE]) &&
	    TAILQ_EMPTY(&ts->ts_blocked[TS_SHARED_QUEUE]);
	if (empty) {
		tc = TC_LOOKUP(ts->ts_lockobj);
		mtx_assert(&tc->tc_lock, MA_OWNED);
		MPASS(LIST_EMPTY(&ts->ts_free));
#ifdef TURNSTILE_PROFILING
		tc->tc_depth--;
#endif
	} else
		ts = LIST_FIRST(&ts->ts_free);
	MPASS(ts != NULL);
	LIST_REMOVE(ts, ts_hash);
	td->td_turnstile = ts;

	return (empty);
}
	
/*
 * Put all blocked threads on the pending list.  This must be called with
 * the turnstile chain locked.
 */
void
turnstile_broadcast(struct turnstile *ts, int queue)
{
	struct turnstile_chain *tc __unused;
	struct turnstile *ts1;
	struct thread *td;

	MPASS(ts != NULL);
	mtx_assert(&ts->ts_lock, MA_OWNED);
	MPASS(curthread->td_proc->p_magic == P_MAGIC);
	MPASS(ts->ts_owner == curthread || ts->ts_owner == NULL);
	/*
	 * We must have the chain locked so that we can remove the empty
	 * turnstile from the hash queue.
	 */
	tc = TC_LOOKUP(ts->ts_lockobj);
	mtx_assert(&tc->tc_lock, MA_OWNED);
	MPASS(queue == TS_SHARED_QUEUE || queue == TS_EXCLUSIVE_QUEUE);

	/*
	 * Transfer the blocked list to the pending list.
	 */
	mtx_lock_spin(&td_contested_lock);
	TAILQ_CONCAT(&ts->ts_pending, &ts->ts_blocked[queue], td_lockq);
	mtx_unlock_spin(&td_contested_lock);

	/*
	 * Give a turnstile to each thread.  The last thread gets
	 * this turnstile if the turnstile is empty.
	 */
	TAILQ_FOREACH(td, &ts->ts_pending, td_lockq) {
		if (LIST_EMPTY(&ts->ts_free)) {
			MPASS(TAILQ_NEXT(td, td_lockq) == NULL);
			ts1 = ts;
#ifdef TURNSTILE_PROFILING
			tc->tc_depth--;
#endif
		} else
			ts1 = LIST_FIRST(&ts->ts_free);
		MPASS(ts1 != NULL);
		LIST_REMOVE(ts1, ts_hash);
		td->td_turnstile = ts1;
	}
}

/*
 * Wakeup all threads on the pending list and adjust the priority of the
 * current thread appropriately.  This must be called with the turnstile
 * chain locked.
 */
void
turnstile_unpend(struct turnstile *ts)
{
	TAILQ_HEAD( ,thread) pending_threads;
	struct turnstile *nts;
	struct thread *td;
	u_char cp, pri;

	MPASS(ts != NULL);
	mtx_assert(&ts->ts_lock, MA_OWNED);
	MPASS(ts->ts_owner == curthread || ts->ts_owner == NULL);
	MPASS(!TAILQ_EMPTY(&ts->ts_pending));

	/*
	 * Move the list of pending threads out of the turnstile and
	 * into a local variable.
	 */
	TAILQ_INIT(&pending_threads);
	TAILQ_CONCAT(&pending_threads, &ts->ts_pending, td_lockq);
#ifdef INVARIANTS
	if (TAILQ_EMPTY(&ts->ts_blocked[TS_EXCLUSIVE_QUEUE]) &&
	    TAILQ_EMPTY(&ts->ts_blocked[TS_SHARED_QUEUE]))
		ts->ts_lockobj = NULL;
#endif
	/*
	 * Adjust the priority of curthread based on other contested
	 * locks it owns.  Don't lower the priority below the base
	 * priority however.
	 */
	td = curthread;
	pri = PRI_MAX;
	thread_lock(td);
	mtx_lock_spin(&td_contested_lock);
	/*
	 * Remove the turnstile from this thread's list of contested locks
	 * since this thread doesn't own it anymore.  New threads will
	 * not be blocking on the turnstile until it is claimed by a new
	 * owner.  There might not be a current owner if this is a shared
	 * lock.
	 */
	if (ts->ts_owner != NULL) {
		ts->ts_owner = NULL;
		LIST_REMOVE(ts, ts_link);
	}
	LIST_FOREACH(nts, &td->td_contested, ts_link) {
		cp = turnstile_first_waiter(nts)->td_priority;
		if (cp < pri)
			pri = cp;
	}
	mtx_unlock_spin(&td_contested_lock);
	sched_unlend_prio(td, pri);
	thread_unlock(td);
	/*
	 * Wake up all the pending threads.  If a thread is not blocked
	 * on a lock, then it is currently executing on another CPU in
	 * turnstile_wait() or sitting on a run queue waiting to resume
	 * in turnstile_wait().  Set a flag to force it to try to acquire
	 * the lock again instead of blocking.
	 */
	while (!TAILQ_EMPTY(&pending_threads)) {
		td = TAILQ_FIRST(&pending_threads);
		TAILQ_REMOVE(&pending_threads, td, td_lockq);
		SDT_PROBE2(sched, , , wakeup, td, td->td_proc);
		thread_lock(td);
		THREAD_LOCKPTR_ASSERT(td, &ts->ts_lock);
		MPASS(td->td_proc->p_magic == P_MAGIC);
		MPASS(TD_ON_LOCK(td));
		TD_CLR_LOCK(td);
		MPASS(TD_CAN_RUN(td));
		td->td_blocked = NULL;
		td->td_lockname = NULL;
		td->td_blktick = 0;
#ifdef INVARIANTS
		td->td_tsqueue = 0xff;
#endif
		sched_add(td, SRQ_BORING);
		thread_unlock(td);
	}
	mtx_unlock_spin(&ts->ts_lock);
}

/*
 * Give up ownership of a turnstile.  This must be called with the
 * turnstile chain locked.
 */
void
turnstile_disown(struct turnstile *ts)
{
	struct thread *td;
	u_char cp, pri;

	MPASS(ts != NULL);
	mtx_assert(&ts->ts_lock, MA_OWNED);
	MPASS(ts->ts_owner == curthread);
	MPASS(TAILQ_EMPTY(&ts->ts_pending));
	MPASS(!TAILQ_EMPTY(&ts->ts_blocked[TS_EXCLUSIVE_QUEUE]) ||
	    !TAILQ_EMPTY(&ts->ts_blocked[TS_SHARED_QUEUE]));

	/*
	 * Remove the turnstile from this thread's list of contested locks
	 * since this thread doesn't own it anymore.  New threads will
	 * not be blocking on the turnstile until it is claimed by a new
	 * owner.
	 */
	mtx_lock_spin(&td_contested_lock);
	ts->ts_owner = NULL;
	LIST_REMOVE(ts, ts_link);
	mtx_unlock_spin(&td_contested_lock);

	/*
	 * Adjust the priority of curthread based on other contested
	 * locks it owns.  Don't lower the priority below the base
	 * priority however.
	 */
	td = curthread;
	pri = PRI_MAX;
	thread_lock(td);
	mtx_unlock_spin(&ts->ts_lock);
	mtx_lock_spin(&td_contested_lock);
	LIST_FOREACH(ts, &td->td_contested, ts_link) {
		cp = turnstile_first_waiter(ts)->td_priority;
		if (cp < pri)
			pri = cp;
	}
	mtx_unlock_spin(&td_contested_lock);
	sched_unlend_prio(td, pri);
	thread_unlock(td);
}

/*
 * Return the first thread in a turnstile.
 */
struct thread *
turnstile_head(struct turnstile *ts, int queue)
{
#ifdef INVARIANTS

	MPASS(ts != NULL);
	MPASS(queue == TS_SHARED_QUEUE || queue == TS_EXCLUSIVE_QUEUE);
	mtx_assert(&ts->ts_lock, MA_OWNED);
#endif
	return (TAILQ_FIRST(&ts->ts_blocked[queue]));
}

/*
 * Returns true if a sub-queue of a turnstile is empty.
 */
int
turnstile_empty(struct turnstile *ts, int queue)
{
#ifdef INVARIANTS

	MPASS(ts != NULL);
	MPASS(queue == TS_SHARED_QUEUE || queue == TS_EXCLUSIVE_QUEUE);
	mtx_assert(&ts->ts_lock, MA_OWNED);
#endif
	return (TAILQ_EMPTY(&ts->ts_blocked[queue]));
}

#ifdef DDB
static void
print_thread(struct thread *td, const char *prefix)
{

	db_printf("%s%p (tid %d, pid %d, \"%s\")\n", prefix, td, td->td_tid,
	    td->td_proc->p_pid, td->td_name);
}

static void
print_queue(struct threadqueue *queue, const char *header, const char *prefix)
{
	struct thread *td;

	db_printf("%s:\n", header);
	if (TAILQ_EMPTY(queue)) {
		db_printf("%sempty\n", prefix);
		return;
	}
	TAILQ_FOREACH(td, queue, td_lockq) {
		print_thread(td, prefix);
	}
}

DB_SHOW_COMMAND(turnstile, db_show_turnstile)
{
	struct turnstile_chain *tc;
	struct turnstile *ts;
	struct lock_object *lock;
	int i;

	if (!have_addr)
		return;

	/*
	 * First, see if there is an active turnstile for the lock indicated
	 * by the address.
	 */
	lock = (struct lock_object *)addr;
	tc = TC_LOOKUP(lock);
	LIST_FOREACH(ts, &tc->tc_turnstiles, ts_hash)
		if (ts->ts_lockobj == lock)
			goto found;

	/*
	 * Second, see if there is an active turnstile at the address
	 * indicated.
	 */
	for (i = 0; i < TC_TABLESIZE; i++)
		LIST_FOREACH(ts, &turnstile_chains[i].tc_turnstiles, ts_hash) {
			if (ts == (struct turnstile *)addr)
				goto found;
		}

	db_printf("Unable to locate a turnstile via %p\n", (void *)addr);
	return;
found:
	lock = ts->ts_lockobj;
	db_printf("Lock: %p - (%s) %s\n", lock, LOCK_CLASS(lock)->lc_name,
	    lock->lo_name);
	if (ts->ts_owner)
		print_thread(ts->ts_owner, "Lock Owner: ");
	else
		db_printf("Lock Owner: none\n");
	print_queue(&ts->ts_blocked[TS_SHARED_QUEUE], "Shared Waiters", "\t");
	print_queue(&ts->ts_blocked[TS_EXCLUSIVE_QUEUE], "Exclusive Waiters",
	    "\t");
	print_queue(&ts->ts_pending, "Pending Threads", "\t");
	
}

/*
 * Show all the threads a particular thread is waiting on based on
 * non-spin locks.
 */
static void
print_lockchain(struct thread *td, const char *prefix)
{
	struct lock_object *lock;
	struct lock_class *class;
	struct turnstile *ts;
	struct thread *owner;

	/*
	 * Follow the chain.  We keep walking as long as the thread is
	 * blocked on a lock that has an owner.
	 */
	while (!db_pager_quit) {
		db_printf("%sthread %d (pid %d, %s) ", prefix, td->td_tid,
		    td->td_proc->p_pid, td->td_name);
		switch (td->td_state) {
		case TDS_INACTIVE:
			db_printf("is inactive\n");
			return;
		case TDS_CAN_RUN:
			db_printf("can run\n");
			return;
		case TDS_RUNQ:
			db_printf("is on a run queue\n");
			return;
		case TDS_RUNNING:
			db_printf("running on CPU %d\n", td->td_oncpu);
			return;
		case TDS_INHIBITED:
			if (TD_ON_LOCK(td)) {
				ts = td->td_blocked;
				lock = ts->ts_lockobj;
				class = LOCK_CLASS(lock);
				db_printf("blocked on lock %p (%s) \"%s\"\n",
				    lock, class->lc_name, lock->lo_name);
				if (ts->ts_owner == NULL)
					return;
				td = ts->ts_owner;
				break;
			} else if (TD_ON_SLEEPQ(td)) {
				if (!lockmgr_chain(td, &owner) &&
				    !sx_chain(td, &owner)) {
					db_printf("sleeping on %p \"%s\"\n",
					    td->td_wchan, td->td_wmesg);
					return;
				}
				if (owner == NULL)
					return;
				td = owner;
				break;
			}
			db_printf("inhibited\n");
			return;
		default:
			db_printf("??? (%#x)\n", td->td_state);
			return;
		}
	}
}

DB_SHOW_COMMAND(lockchain, db_show_lockchain)
{
	struct thread *td;

	/* Figure out which thread to start with. */
	if (have_addr)
		td = db_lookup_thread(addr, true);
	else
		td = kdb_thread;

	print_lockchain(td, "");
}
DB_SHOW_ALIAS(sleepchain, db_show_lockchain);

DB_SHOW_ALL_COMMAND(chains, db_show_allchains)
{
	struct thread *td;
	struct proc *p;
	int i;

	i = 1;
	FOREACH_PROC_IN_SYSTEM(p) {
		FOREACH_THREAD_IN_PROC(p, td) {
			if ((TD_ON_LOCK(td) && LIST_EMPTY(&td->td_contested))
			    || (TD_IS_INHIBITED(td) && TD_ON_SLEEPQ(td))) {
				db_printf("chain %d:\n", i++);
				print_lockchain(td, " ");
			}
			if (db_pager_quit)
				return;
		}
	}
}
DB_SHOW_ALIAS(allchains, db_show_allchains)

static void	print_waiters(struct turnstile *ts, int indent);
	
static void
print_waiter(struct thread *td, int indent)
{
	struct turnstile *ts;
	int i;

	if (db_pager_quit)
		return;
	for (i = 0; i < indent; i++)
		db_printf(" ");
	print_thread(td, "thread ");
	LIST_FOREACH(ts, &td->td_contested, ts_link)
		print_waiters(ts, indent + 1);
}

static void
print_waiters(struct turnstile *ts, int indent)
{
	struct lock_object *lock;
	struct lock_class *class;
	struct thread *td;
	int i;

	if (db_pager_quit)
		return;
	lock = ts->ts_lockobj;
	class = LOCK_CLASS(lock);
	for (i = 0; i < indent; i++)
		db_printf(" ");
	db_printf("lock %p (%s) \"%s\"\n", lock, class->lc_name, lock->lo_name);
	TAILQ_FOREACH(td, &ts->ts_blocked[TS_EXCLUSIVE_QUEUE], td_lockq)
		print_waiter(td, indent + 1);
	TAILQ_FOREACH(td, &ts->ts_blocked[TS_SHARED_QUEUE], td_lockq)
		print_waiter(td, indent + 1);
	TAILQ_FOREACH(td, &ts->ts_pending, td_lockq)
		print_waiter(td, indent + 1);
}

DB_SHOW_COMMAND(locktree, db_show_locktree)
{
	struct lock_object *lock;
	struct lock_class *class;
	struct turnstile_chain *tc;
	struct turnstile *ts;

	if (!have_addr)
		return;
	lock = (struct lock_object *)addr;
	tc = TC_LOOKUP(lock);
	LIST_FOREACH(ts, &tc->tc_turnstiles, ts_hash)
		if (ts->ts_lockobj == lock)
			break;
	if (ts == NULL) {
		class = LOCK_CLASS(lock);
		db_printf("lock %p (%s) \"%s\"\n", lock, class->lc_name,
		    lock->lo_name);
	} else
		print_waiters(ts, 0);
}
#endif
