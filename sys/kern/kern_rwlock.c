/*	$OpenBSD: kern_rwlock.c,v 1.58 2025/07/21 20:36:41 bluhm Exp $	*/

/*
 * Copyright (c) 2002, 2003 Artur Grabowski <art@openbsd.org>
 * Copyright (c) 2011 Thordur Bjornsson <thib@secnorth.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/limits.h>
#include <sys/atomic.h>
#include <sys/tracepoint.h>
#include <sys/witness.h>

#ifdef RWDIAG
#define RW_SLEEP_TMO	10000000000ULL /* 10 seconds */
#else
#define RW_SLEEP_TMO	INFSLP
#endif

/*
 * Other OSes implement more sophisticated mechanism to determine how long the
 * process attempting to acquire the lock should be spinning. We start with
 * the most simple approach: we do RW_SPINS attempts at most before eventually
 * giving up and putting the process to sleep queue.
 */
#define RW_SPINS	1000

#ifdef MULTIPROCESSOR
#define rw_cas(p, e, n)	atomic_cas_ulong(p, e, n)
#define rw_inc(p)	atomic_inc_int(p)
#define rw_dec(p)	atomic_dec_int(p)
#else
static inline unsigned long
rw_cas(volatile unsigned long *p, unsigned long e, unsigned long n)
{
	unsigned long o = *p;

	if (o == e)
		*p = n;

	return (o);
}

static inline void
rw_inc(volatile unsigned int *p)
{
	++(*p);
}

static inline void
rw_dec(volatile unsigned int *p)
{
	(*p)--;
}
#endif

static int	rw_do_enter_read(struct rwlock *, int);
static void	rw_do_exit_read(struct rwlock *, unsigned long);
static int	rw_do_enter_write(struct rwlock *, int);
static int	rw_downgrade(struct rwlock *, int);
static int	rw_upgrade(struct rwlock *, int);

static void	rw_exited(struct rwlock *);

static unsigned long
rw_self(void)
{
	unsigned long self = (unsigned long)curproc;

	CLR(self, RWLOCK_MASK);
	SET(self, RWLOCK_WRLOCK);

	return (self);
}

void
rw_enter_read(struct rwlock *rwl)
{
	rw_do_enter_read(rwl, 0);
}

void
rw_enter_write(struct rwlock *rwl)
{
	rw_do_enter_write(rwl, 0);
}

void
rw_exit_read(struct rwlock *rwl)
{
	/* maybe we're the last one? */
	rw_do_exit_read(rwl, RWLOCK_READ_INCR);
}

static void
rw_do_exit_read(struct rwlock *rwl, unsigned long owner)
{
	unsigned long decr;
	unsigned long nowner;

	WITNESS_UNLOCK(&rwl->rwl_lock_obj, 0);

	for (;;) {
		decr = owner - RWLOCK_READ_INCR;
		nowner = rw_cas(&rwl->rwl_owner, owner, decr);
		if (owner == nowner)
			break;

		if (__predict_false(ISSET(nowner, RWLOCK_WRLOCK))) {
			panic("%s rwlock %p: exit read on write locked lock"
			    " (owner 0x%lx)", rwl->rwl_name, rwl, nowner);
		}
		if (__predict_false(nowner == 0)) {
			panic("%s rwlock %p: exit read on unlocked lock",
			    rwl->rwl_name, rwl);
		}

		owner = nowner;
	}

	/* read lock didn't change anything, so no barrier needed? */

	if (decr == 0) {
		/* last one out */
		rw_exited(rwl);
	}
}

void
rw_exit_write(struct rwlock *rwl)
{
	unsigned long self = rw_self();
	unsigned long owner;

	WITNESS_UNLOCK(&rwl->rwl_lock_obj, LOP_EXCLUSIVE);

	membar_exit_before_atomic();
	owner = rw_cas(&rwl->rwl_owner, self, 0);
	if (__predict_false(owner != self)) {
		panic("%s rwlock %p: exit write when lock not held "
		    "(owner 0x%lx, self 0x%lx)", rwl->rwl_name, rwl,
		    owner, self);
	}

	rw_exited(rwl);
}

static void
_rw_init_flags_witness(struct rwlock *rwl, const char *name, int lo_flags,
    const struct lock_type *type, int trace)
{
	rwl->rwl_owner = 0;
	rwl->rwl_waiters = 0;
	rwl->rwl_readers = 0;
	rwl->rwl_name = name;
	rwl->rwl_traceidx = trace;

#ifdef WITNESS
	rwl->rwl_lock_obj.lo_flags = lo_flags;
	rwl->rwl_lock_obj.lo_name = name;
	rwl->rwl_lock_obj.lo_type = type;
	WITNESS_INIT(&rwl->rwl_lock_obj, type);
#else
	(void)type;
	(void)lo_flags;
#endif
}

void
_rw_init_flags(struct rwlock *rwl, const char *name, int flags,
    const struct lock_type *type, int trace)
{
	_rw_init_flags_witness(rwl, name, RWLOCK_LO_FLAGS(flags), type, trace);
}

int
rw_enter(struct rwlock *rwl, int flags)
{
	int op = flags & RW_OPMASK;
	int error;

	switch (op) {
	case RW_WRITE:
		error = rw_do_enter_write(rwl, flags);
		break;
	case RW_READ:
		error = rw_do_enter_read(rwl, flags);
		break;
	case RW_DOWNGRADE:
		error = rw_downgrade(rwl, flags);
		break;
	case RW_UPGRADE:
		error = rw_upgrade(rwl, flags);
		break;
	default:
		panic("%s rwlock %p: %s unexpected op 0x%x",
		    rwl->rwl_name, rwl, __func__, op);
		/* NOTREACHED */
	}

	return (error);
}

static int
rw_do_enter_write(struct rwlock *rwl, int flags)
{
	unsigned long self = rw_self();
	unsigned long owner;
	int prio;
	int error;

#ifdef WITNESS
	int lop_flags = LOP_NEWORDER | LOP_EXCLUSIVE;
	if (ISSET(flags, RW_DUPOK))
		lop_flags |= LOP_DUPOK;

	if (!ISSET(flags, RW_NOSLEEP))
		WITNESS_CHECKORDER(&rwl->rwl_lock_obj, lop_flags, NULL);
#endif

	owner = rw_cas(&rwl->rwl_owner, 0, self);
	if (owner == 0) {
		/* wow, we won. so easy */
		TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 2, 1);
		goto locked;
	}
	if (__predict_false(owner == self)) {
		panic("%s rwlock %p: enter write deadlock",
		    rwl->rwl_name, rwl);
	}

#ifdef MULTIPROCESSOR
	/*
	 * If process holds the kernel lock, then we want to give up on CPU
	 * as soon as possible so other processes waiting for the kernel lock
	 * can progress. Hence no spinning if we hold the kernel lock.
	 */
	if (!_kernel_lock_held()) {
		struct schedstate_percpu *spc = &curcpu()->ci_schedstate;
		int spins;

		/*
		 * It makes sense to try to spin just in case the lock
		 * is acquired by writer.
		 */

		spc->spc_spinning++;
		for (spins = 0; spins < RW_SPINS; spins++) {
			CPU_BUSY_CYCLE();
			owner = atomic_load_long(&rwl->rwl_owner);
			if (owner != 0)
				continue;

			owner = rw_cas(&rwl->rwl_owner, 0, self);
			if (owner == 0) {
				spc->spc_spinning--;
				/* ok, we won now. */
				TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 2,
				    2);
				goto locked;
			}
		}
		spc->spc_spinning--;
	}
#endif

	if (ISSET(flags, RW_NOSLEEP)) {
		TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 2, 4);
		return (EBUSY);
	}

	prio = PLOCK - 4;
	if (ISSET(flags, RW_INTR))
		prio |= PCATCH;

	rw_inc(&rwl->rwl_waiters);
	membar_producer();
	do {
		sleep_setup(&rwl->rwl_waiters, prio, rwl->rwl_name);
		membar_consumer();
		owner = atomic_load_long(&rwl->rwl_owner);
		error = sleep_finish(RW_SLEEP_TMO, owner != 0);
#ifdef RWDIAG
		if (error == EWOULDBLOCK) {
			printf("%s rwlock %p: %s timeout owner 0x%lx "
			    "(self 0x%lx)", rwl->rwl_name, rwl, __func__,
			    owner, self);
			db_enter();
		}
#endif
		if (ISSET(flags, RW_INTR) && (error != 0)) {
			rw_dec(&rwl->rwl_waiters);
			TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 2, 4);
			return (error);
		}

		owner = rw_cas(&rwl->rwl_owner, 0, self);
	} while (owner != 0);
	rw_dec(&rwl->rwl_waiters);
	TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 2, 3);

locked:
	membar_enter_after_atomic();
	WITNESS_LOCK(&rwl->rwl_lock_obj, lop_flags);

	return (0);
}

static int
rw_read_incr(struct rwlock *rwl, unsigned long owner)
{
	unsigned long incr;
	unsigned long nowner;

	do {
		incr = owner + RWLOCK_READ_INCR;
		nowner = rw_cas(&rwl->rwl_owner, owner, incr);
		if (nowner == owner)
			return (1);

		owner = nowner;
	} while (!ISSET(owner, RWLOCK_WRLOCK));

	return (0);
}

static int
rw_do_enter_read(struct rwlock *rwl, int flags)
{
	unsigned long owner;
	int error;
	int prio;

#ifdef WITNESS
	int lop_flags = LOP_NEWORDER;
	if (ISSET(flags, RW_DUPOK))
		lop_flags |= LOP_DUPOK;
	if (!ISSET(flags, RW_NOSLEEP))
		WITNESS_CHECKORDER(&rwl->rwl_lock_obj, lop_flags, NULL);
#endif

	owner = rw_cas(&rwl->rwl_owner, 0, RWLOCK_READ_INCR);
	if (owner == 0) {
		/* ermagerd, we won! */
		TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 1, 1);
		goto locked;
	}

	if (ISSET(owner, RWLOCK_WRLOCK)) {
		if (__predict_false(owner == rw_self())) {
			panic("%s rwlock %p: enter read deadlock",
			    rwl->rwl_name, rwl);
		}
	} else if (atomic_load_int(&rwl->rwl_waiters) == 0) {
		if (rw_read_incr(rwl, owner)) {
			/* nailed it */
			TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 1, 2);
			goto locked;
		}
	}

	if (ISSET(flags, RW_NOSLEEP)) {
		TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 1, 4);
		return (EBUSY);
	}

	prio = PLOCK;
	if (ISSET(flags, RW_INTR))
		prio |= PCATCH;

	rw_inc(&rwl->rwl_readers);
	membar_producer();
	do {
		sleep_setup(&rwl->rwl_readers, prio, rwl->rwl_name);
		membar_consumer();
		error = sleep_finish(RW_SLEEP_TMO,
		    atomic_load_int(&rwl->rwl_waiters) > 0 ||
		    ISSET(atomic_load_long(&rwl->rwl_owner), RWLOCK_WRLOCK));
#ifdef RWDIAG
		if (error == EWOULDBLOCK) {
			printf("%s rwlock %p: %s timeout owner 0x%lx\n",
			    rwl->rwl_name, rwl, __func__, owner);
			db_enter();
		}
#endif
		if (ISSET(flags, RW_INTR) && (error != 0)) {
			rw_dec(&rwl->rwl_readers);
			TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 1, 4);
			return (error);
		}
	} while (!rw_read_incr(rwl, 0));
	rw_dec(&rwl->rwl_readers);
	TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 1, 3);

locked:
	membar_enter_after_atomic();
	WITNESS_LOCK(&rwl->rwl_lock_obj, lop_flags);

	return (0);
}

static int
rw_downgrade(struct rwlock *rwl, int flags)
{
	unsigned long self = rw_self();
	unsigned long owner;

	membar_exit_before_atomic();
	owner = atomic_cas_ulong(&rwl->rwl_owner, self, RWLOCK_READ_INCR);
	if (__predict_false(owner != self)) {
		panic("%s rwlock %p: downgrade when lock not held "
		    "(owner 0x%lx, self 0x%lx)", rwl->rwl_name, rwl,
		    owner, self);
	}

#ifdef WITNESS
	{
		int lop_flags = LOP_NEWORDER;
		if (ISSET(flags, RW_DUPOK))
			lop_flags |= LOP_DUPOK;
		WITNESS_DOWNGRADE(&rwl->rwl_lock_obj, lop_flags);
	}
#endif

	membar_consumer();
	if (atomic_load_int(&rwl->rwl_waiters) == 0 &&
	    atomic_load_int(&rwl->rwl_readers) > 0)
		wakeup(&rwl->rwl_readers);

	return (0);
}

static int
rw_upgrade(struct rwlock *rwl, int flags)
{
	unsigned long self = rw_self();
	unsigned long owner;

	KASSERTMSG(ISSET(flags, RW_NOSLEEP), "RW_UPGRADE without RW_NOSLEEP");

	owner = atomic_cas_ulong(&rwl->rwl_owner, RWLOCK_READ_INCR, self);
	if (owner != RWLOCK_READ_INCR) {
		if (__predict_false(owner == 0)) {
			panic("%s rwlock %p: upgrade on unowned lock",
			    rwl->rwl_name, rwl);
		}
		if (__predict_false(ISSET(owner, RWLOCK_WRLOCK))) {
			panic("%s rwlock %p: upgrade on write locked lock"
			    "(owner 0x%lx, self 0x%lx)", rwl->rwl_name, rwl,
			    owner, self);
		}
		TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 3, 4);
		return (EBUSY);
	}
	TRACEINDEX(rwlock, rwl->rwl_traceidx, rwl, 3, 1);

#ifdef WITNESS
	{
		int lop_flags = LOP_NEWORDER;
		if (ISSET(flags, RW_DUPOK))
			lop_flags |= LOP_DUPOK;
		WITNESS_UPGRADE(&rwl->rwl_lock_obj, lop_flags);
	}
#endif

	return (0);
}

void
rw_exit(struct rwlock *rwl)
{
	unsigned long owner;

	owner = atomic_load_long(&rwl->rwl_owner);
	if (__predict_false(owner == 0)) {
		panic("%s rwlock %p: exit on unlocked lock",
		    rwl->rwl_name, rwl);
	}

	if (ISSET(owner, RWLOCK_WRLOCK))
		rw_exit_write(rwl);
	else
		rw_do_exit_read(rwl, owner);
}

static void
rw_exited(struct rwlock *rwl)
{
	membar_consumer();
	if (atomic_load_int(&rwl->rwl_waiters) > 0)
		wakeup_one(&rwl->rwl_waiters);
	else if (atomic_load_int(&rwl->rwl_readers) > 0)
		wakeup(&rwl->rwl_readers);
}

int
rw_status(struct rwlock *rwl)
{
	unsigned long owner;

	owner = atomic_load_long(&rwl->rwl_owner);
	if (ISSET(owner, RWLOCK_WRLOCK)) {
		if (rw_self() == owner)
			return RW_WRITE;
		else
			return RW_WRITE_OTHER;
	}
	if (owner)
		return RW_READ;
	return (0);
}

#ifdef DIAGNOSTIC
void
rw_assert_wrlock(struct rwlock *rwl)
{
	if (panicstr || db_active)
		return;

#ifdef WITNESS
	witness_assert(&rwl->rwl_lock_obj, LA_XLOCKED);
#else
	if (atomic_load_long(&rwl->rwl_owner) != rw_self()) {
		panic("%s rwlock %p: lock not held by this process",
		    rwl->rwl_name, rwl);
	}
#endif
}

void
rw_assert_rdlock(struct rwlock *rwl)
{
	if (panicstr || db_active)
		return;

#ifdef WITNESS
	witness_assert(&rwl->rwl_lock_obj, LA_SLOCKED);
#else
	if (rw_status(rwl) != RW_READ)
		panic("%s rwlock %p: lock not shared", rwl->rwl_name, rwl);
#endif
}

void
rw_assert_anylock(struct rwlock *rwl)
{
	if (panicstr || db_active)
		return;

#ifdef WITNESS
	witness_assert(&rwl->rwl_lock_obj, LA_LOCKED);
#else
	switch (rw_status(rwl)) {
	case RW_WRITE_OTHER:
		panic("%s rwlock %p: lock held by different process "
		    "(self %lx, owner %lx)", rwl->rwl_name, rwl,
		    rw_self(), rwl->rwl_owner);
	case 0:
		panic("%s rwlock %p: lock not held", rwl->rwl_name, rwl);
	}
#endif
}

void
rw_assert_unlocked(struct rwlock *rwl)
{
	if (panicstr || db_active)
		return;

#ifdef WITNESS
	witness_assert(&rwl->rwl_lock_obj, LA_UNLOCKED);
#else
	if (atomic_load_long(&rwl->rwl_owner) == rw_self())
		panic("%s rwlock %p: lock held", rwl->rwl_name, rwl);
#endif
}
#endif

/* recursive rwlocks; */
void
_rrw_init_flags(struct rrwlock *rrwl, const char *name, int flags,
    const struct lock_type *type)
{
	memset(rrwl, 0, sizeof(struct rrwlock));
	_rw_init_flags_witness(&rrwl->rrwl_lock, name, RRWLOCK_LO_FLAGS(flags),
	    type, 0);
}

int
rrw_enter(struct rrwlock *rrwl, int flags)
{
	int	rv;

	if (atomic_load_long(&rrwl->rrwl_lock.rwl_owner) == rw_self()) {
		if (flags & RW_RECURSEFAIL)
			return (EDEADLK);
		else {
			rrwl->rrwl_wcnt++;
			WITNESS_LOCK(&rrwl->rrwl_lock.rwl_lock_obj,
			    LOP_EXCLUSIVE);
			return (0);
		}
	}

	rv = rw_enter(&rrwl->rrwl_lock, flags);
	if (rv == 0)
		rrwl->rrwl_wcnt = 1;

	return (rv);
}

void
rrw_exit(struct rrwlock *rrwl)
{

	if (atomic_load_long(&rrwl->rrwl_lock.rwl_owner) == rw_self()) {
		KASSERT(rrwl->rrwl_wcnt > 0);
		rrwl->rrwl_wcnt--;
		if (rrwl->rrwl_wcnt != 0) {
			WITNESS_UNLOCK(&rrwl->rrwl_lock.rwl_lock_obj,
			    LOP_EXCLUSIVE);
			return;
		}
	}

	rw_exit(&rrwl->rrwl_lock);
}

int
rrw_status(struct rrwlock *rrwl)
{
	return (rw_status(&rrwl->rrwl_lock));
}

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define	RWLOCK_OBJ_MAGIC	0x5aa3c85d
struct rwlock_obj {
	struct rwlock	ro_lock;
	u_int		ro_magic;
	u_int		ro_refcnt;
};


struct pool rwlock_obj_pool;

/*
 * rw_obj_init:
 *
 *	Initialize the mutex object store.
 */
void
rw_obj_init(void)
{
	pool_init(&rwlock_obj_pool, sizeof(struct rwlock_obj), 0, IPL_MPFLOOR,
	    PR_WAITOK, "rwobjpl", NULL);
}

/*
 * rw_obj_alloc:
 *
 *	Allocate a single lock object.
 */
void
_rw_obj_alloc_flags(struct rwlock **lock, const char *name, int flags,
    struct lock_type *type)
{
	struct rwlock_obj *mo;

	mo = pool_get(&rwlock_obj_pool, PR_WAITOK);
	mo->ro_magic = RWLOCK_OBJ_MAGIC;
	_rw_init_flags(&mo->ro_lock, name, flags, type, 0);
	mo->ro_refcnt = 1;

	*lock = &mo->ro_lock;
}

/*
 * rw_obj_hold:
 *
 *	Add a single reference to a lock object.  A reference to the object
 *	must already be held, and must be held across this call.
 */

void
rw_obj_hold(struct rwlock *lock)
{
	struct rwlock_obj *mo = (struct rwlock_obj *)lock;

	KASSERTMSG(mo->ro_magic == RWLOCK_OBJ_MAGIC,
	    "%s: lock %p: mo->ro_magic (%#x) != RWLOCK_OBJ_MAGIC (%#x)",
	     __func__, mo, mo->ro_magic, RWLOCK_OBJ_MAGIC);
	KASSERTMSG(mo->ro_refcnt > 0,
	    "%s: lock %p: mo->ro_refcnt (%#x) == 0",
	     __func__, mo, mo->ro_refcnt);

	atomic_inc_int(&mo->ro_refcnt);
}

/*
 * rw_obj_free:
 *
 *	Drop a reference from a lock object.  If the last reference is being
 *	dropped, free the object and return true.  Otherwise, return false.
 */
int
rw_obj_free(struct rwlock *lock)
{
	struct rwlock_obj *mo = (struct rwlock_obj *)lock;

	KASSERTMSG(mo->ro_magic == RWLOCK_OBJ_MAGIC,
	    "%s: lock %p: mo->ro_magic (%#x) != RWLOCK_OBJ_MAGIC (%#x)",
	     __func__, mo, mo->ro_magic, RWLOCK_OBJ_MAGIC);
	KASSERTMSG(mo->ro_refcnt > 0,
	    "%s: lock %p: mo->ro_refcnt (%#x) == 0",
	     __func__, mo, mo->ro_refcnt);

	if (atomic_dec_int_nv(&mo->ro_refcnt) > 0) {
		return false;
	}
#if notyet
	WITNESS_DESTROY(&mo->ro_lock);
#endif
	pool_put(&rwlock_obj_pool, mo);
	return true;
}
