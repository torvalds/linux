/*	$OpenBSD: vfs_lockf.c,v 1.50 2022/08/14 01:58:28 jsg Exp $	*/
/*	$NetBSD: vfs_lockf.c,v 1.7 1996/02/04 02:18:21 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Scooter Morris at Genentech Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufs_lockf.c	8.3 (Berkeley) 1/6/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/pool.h>
#include <sys/fcntl.h>
#include <sys/lockf.h>
#include <sys/rwlock.h>
#include <sys/unistd.h>

/*
 * The lockf structure is a kernel structure which contains the information
 * associated with a byte range lock.  The lockf structures are linked into
 * the inode structure. Locks are sorted by the starting byte of the lock for
 * efficiency.
 */
TAILQ_HEAD(locklist, lockf);

struct lockf {
	short	lf_flags;	 /* Lock semantics: F_POSIX, F_FLOCK, F_WAIT */
	short	lf_type;	 /* Lock type: F_RDLCK, F_WRLCK */
	off_t	lf_start;	 /* The byte # of the start of the lock */
	off_t	lf_end;		 /* The byte # of the end of the lock (-1=EOF)*/
	caddr_t	lf_id;		 /* The id of the resource holding the lock */
	struct	lockf_state *lf_state;	/* State associated with the lock */
	TAILQ_ENTRY(lockf) lf_entry;
	struct	lockf *lf_blk;	 /* The lock that blocks us */
	struct	locklist lf_blkhd;	/* The list of blocked locks */
	TAILQ_ENTRY(lockf) lf_block; /* A request waiting for a lock */
	uid_t	lf_uid;		/* User ID responsible */
	pid_t	lf_pid;		/* POSIX - owner pid */
};

struct lockf_state {
	TAILQ_HEAD(, lockf)	  ls_locks;	/* list of active locks */
	TAILQ_HEAD(, lockf)	  ls_pending;	/* list of pending locks */
	struct lockf_state	**ls_owner;	/* owner */
	int		 	  ls_refs;	/* reference counter */
};

struct pool lockf_state_pool;
struct pool lockf_pool;

#define SELF	0x1
#define OTHERS	0x2

#ifdef LOCKF_DEBUG

#define	DEBUG_SETLOCK		0x01
#define	DEBUG_CLEARLOCK		0x02
#define	DEBUG_GETLOCK		0x04
#define	DEBUG_FINDOVR		0x08
#define	DEBUG_SPLIT		0x10
#define	DEBUG_WAKELOCK		0x20
#define	DEBUG_LINK		0x40

int	lockf_debug = DEBUG_SETLOCK|DEBUG_CLEARLOCK|DEBUG_WAKELOCK;

void	lf_print(const char *, struct lockf *);
void	lf_printlist(const char *, struct lockf *);

#define	DPRINTF(args, level)	if (lockf_debug & (level)) printf args
#define	LFPRINT(args, level)	if (lockf_debug & (level)) lf_print args
#else
#define	DPRINTF(args, level)
#define	LFPRINT(args, level)
#endif

struct lockf *lf_alloc(uid_t, int);
void lf_free(struct lockf *);
int lf_clearlock(struct lockf *);
int lf_findoverlap(struct lockf *, struct lockf *, int, struct lockf **);
struct lockf *lf_getblock(struct lockf *, struct lockf *);
int lf_getlock(struct lockf *, struct flock *);
int lf_setlock(struct lockf *);
void lf_split(struct lockf *, struct lockf *);
void lf_wakelock(struct lockf *, int);
int lf_deadlock(struct lockf *);
void ls_ref(struct lockf_state *);
void ls_rele(struct lockf_state *);

/*
 * Serializes access to each instance of struct lockf and struct lockf_state
 * and each pointer from a vnode to struct lockf_state.
 */
struct rwlock lockf_lock = RWLOCK_INITIALIZER("lockflk");

void
lf_init(void)
{
	pool_init(&lockf_state_pool, sizeof(struct lockf_state), 0, IPL_NONE,
	    PR_WAITOK | PR_RWLOCK, "lockfspl", NULL);
	pool_init(&lockf_pool, sizeof(struct lockf), 0, IPL_NONE,
	    PR_WAITOK | PR_RWLOCK, "lockfpl", NULL);
}

void
ls_ref(struct lockf_state *ls)
{
	rw_assert_wrlock(&lockf_lock);

	ls->ls_refs++;
}

void
ls_rele(struct lockf_state *ls)
{
	rw_assert_wrlock(&lockf_lock);

	if (--ls->ls_refs > 0)
		return;

	KASSERT(TAILQ_EMPTY(&ls->ls_locks));
	KASSERT(TAILQ_EMPTY(&ls->ls_pending));

	*ls->ls_owner = NULL;
	pool_put(&lockf_state_pool, ls);
}

/*
 * We enforce a limit on locks by uid, so that a single user cannot
 * run the kernel out of memory.  For now, the limit is pretty coarse.
 * There is no limit on root.
 *
 * Splitting a lock will always succeed, regardless of current allocations.
 * If you're slightly above the limit, we still have to permit an allocation
 * so that the unlock can succeed.  If the unlocking causes too many splits,
 * however, you're totally cutoff.
 */
int maxlocksperuid = 1024;

/*
 * 3 options for allowfail.
 * 0 - always allocate.  1 - cutoff at limit.  2 - cutoff at double limit.
 */
struct lockf *
lf_alloc(uid_t uid, int allowfail)
{
	struct uidinfo *uip;
	struct lockf *lock;

	uip = uid_find(uid);
	if (uid && allowfail && uip->ui_lockcnt >
	    (allowfail == 1 ? maxlocksperuid : (maxlocksperuid * 2))) {
		uid_release(uip);
		return (NULL);
	}
	uip->ui_lockcnt++;
	uid_release(uip);
	lock = pool_get(&lockf_pool, PR_WAITOK);
	lock->lf_uid = uid;
	return (lock);
}

void
lf_free(struct lockf *lock)
{
	struct uidinfo *uip;

	rw_assert_wrlock(&lockf_lock);

	LFPRINT(("lf_free", lock), DEBUG_LINK);

	KASSERT(TAILQ_EMPTY(&lock->lf_blkhd));

	ls_rele(lock->lf_state);

	uip = uid_find(lock->lf_uid);
	uip->ui_lockcnt--;
	uid_release(uip);
	pool_put(&lockf_pool, lock);
}


/*
 * Do an advisory lock operation.
 */
int
lf_advlock(struct lockf_state **state, off_t size, caddr_t id, int op,
    struct flock *fl, int flags)
{
	struct proc *p = curproc;
	struct lockf_state *ls;
	struct lockf *lock;
	off_t start, end;
	int error = 0;

	/*
	 * Convert the flock structure into a start and end.
	 */
	switch (fl->l_whence) {
	case SEEK_SET:
	case SEEK_CUR:
		/*
		 * Caller is responsible for adding any necessary offset
		 * when SEEK_CUR is used.
		 */
		start = fl->l_start;
		break;
	case SEEK_END:
		start = size + fl->l_start;
		break;
	default:
		return (EINVAL);
	}
	if (start < 0)
		return (EINVAL);
	if (fl->l_len > 0) {
		if (fl->l_len - 1 > LLONG_MAX - start)
			return (EOVERFLOW);
		end = start + (fl->l_len - 1);
		/* Avoid ambiguity at the end of the range. */
		if (end == LLONG_MAX)
			end = -1;
	} else if (fl->l_len < 0) {
		if (start + fl->l_len < 0)
			return (EINVAL);
		end = start - 1;
		start += fl->l_len;
	} else {
		end = -1;
	}

	rw_enter_write(&lockf_lock);
	ls = *state;

	/*
	 * Avoid the common case of unlocking when inode has no locks.
	 */
	if (ls == NULL && op != F_SETLK) {
		fl->l_type = F_UNLCK;
		goto out;
	}

	if (ls == NULL) {
		ls = pool_get(&lockf_state_pool, PR_WAITOK | PR_ZERO);
		ls->ls_owner = state;
		TAILQ_INIT(&ls->ls_locks);
		TAILQ_INIT(&ls->ls_pending);
		*state = ls;
	}
	ls_ref(ls);

	lock = lf_alloc(p->p_ucred->cr_uid, op == F_SETLK ? 1 : 2);
	if (!lock) {
		ls_rele(ls);
		error = ENOLCK;
		goto out;
	}
	lock->lf_flags = flags;
	lock->lf_type = fl->l_type;
	lock->lf_start = start;
	lock->lf_end = end;
	lock->lf_id = id;
	lock->lf_state = ls;
	lock->lf_blk = NULL;
	lock->lf_pid = (flags & F_POSIX) ? p->p_p->ps_pid : -1;
	TAILQ_INIT(&lock->lf_blkhd);

	switch (op) {
	case F_SETLK:
		error = lf_setlock(lock);
		break;
	case F_UNLCK:
		error = lf_clearlock(lock);
		lf_free(lock);
		break;
	case F_GETLK:
		error = lf_getlock(lock, fl);
		lf_free(lock);
		break;
	default:
		lf_free(lock);
		error = EINVAL;
		break;
	}

out:
	rw_exit_write(&lockf_lock);
	return (error);
}

/*
 * Set a byte-range lock.
 */
int
lf_setlock(struct lockf *lock)
{
	struct lockf *block;
	struct lockf *overlap, *ltmp;
	int ovcase, priority, needtolink, error;

	rw_assert_wrlock(&lockf_lock);

	LFPRINT(("lf_setlock", lock), DEBUG_SETLOCK);

	priority = PLOCK;
	if (lock->lf_type == F_WRLCK)
		priority += 4;
	priority |= PCATCH;
	/*
	 * Scan lock list for this file looking for locks that would block us.
	 */
	for (;;) {
		block = lf_getblock(TAILQ_FIRST(&lock->lf_state->ls_locks),
		    lock);
		if (block == NULL)
			break;

		if ((lock->lf_flags & F_WAIT) == 0) {
			lf_free(lock);
			return (EAGAIN);
		}

		/*
		 * Lock is blocked, check for deadlock before proceeding.
		 * Note: flock style locks cover the whole file, there is no
		 * chance for deadlock.
		 */
		if ((lock->lf_flags & F_POSIX) && lf_deadlock(lock)) {
			lf_free(lock);
			return (EDEADLK);
		}

		/*
		 * For flock type locks, we must first remove
		 * any shared locks that we hold before we sleep
		 * waiting for an exclusive lock.
		 */
		if ((lock->lf_flags & F_FLOCK) && lock->lf_type == F_WRLCK) {
			lock->lf_type = F_UNLCK;
			(void)lf_clearlock(lock);
			lock->lf_type = F_WRLCK;
		}
		/*
		 * Add our lock to the blocked list and sleep until we're free.
		 * Remember who blocked us (for deadlock detection).
		 */
		lock->lf_blk = block;
		LFPRINT(("lf_setlock", lock), DEBUG_SETLOCK);
		LFPRINT(("lf_setlock: blocking on", block), DEBUG_SETLOCK);
		TAILQ_INSERT_TAIL(&block->lf_blkhd, lock, lf_block);
		TAILQ_INSERT_TAIL(&lock->lf_state->ls_pending, lock, lf_entry);
		error = rwsleep_nsec(lock, &lockf_lock, priority, "lockf",
		    INFSLP);
		TAILQ_REMOVE(&lock->lf_state->ls_pending, lock, lf_entry);
		wakeup_one(lock->lf_state);
		if (lock->lf_blk != NULL) {
			TAILQ_REMOVE(&lock->lf_blk->lf_blkhd, lock, lf_block);
			lock->lf_blk = NULL;
		}
		if (error) {
			lf_free(lock);
			return (error);
		}
		if (lock->lf_flags & F_INTR) {
			lf_free(lock);
			return (EINTR);
		}
	}
	/*
	 * No blocks!!  Add the lock.  Note that we will
	 * downgrade or upgrade any overlapping locks this
	 * process already owns.
	 *
	 * Skip over locks owned by other processes.
	 * Handle any locks that overlap and are owned by ourselves.
	 */
	block = TAILQ_FIRST(&lock->lf_state->ls_locks);
	overlap = NULL;
	needtolink = 1;
	for (;;) {
		ovcase = lf_findoverlap(block, lock, SELF, &overlap);
		if (ovcase)
			block = TAILQ_NEXT(overlap, lf_entry);
		/*
		 * Six cases:
		 *	0) no overlap
		 *	1) overlap == lock
		 *	2) overlap contains lock
		 *	3) lock contains overlap
		 *	4) overlap starts before lock
		 *	5) overlap ends after lock
		 */
		switch (ovcase) {
		case 0: /* no overlap */
			if (needtolink) {
				if (overlap)	/* insert before overlap */
					TAILQ_INSERT_BEFORE(overlap, lock,
					    lf_entry);
				else		/* first or last lock in list */
					TAILQ_INSERT_TAIL(&lock->lf_state->ls_locks,
					    lock, lf_entry);
			}
			break;
		case 1: /* overlap == lock */
			/*
			 * If downgrading lock, others may be
			 * able to acquire it.
			 */
			if (lock->lf_type == F_RDLCK &&
			    overlap->lf_type == F_WRLCK)
				lf_wakelock(overlap, 0);
			overlap->lf_type = lock->lf_type;
			lf_free(lock);
			lock = overlap; /* for debug output below */
			break;
		case 2: /* overlap contains lock */
			/*
			 * Check for common starting point and different types.
			 */
			if (overlap->lf_type == lock->lf_type) {
				if (!needtolink)
					TAILQ_REMOVE(&lock->lf_state->ls_locks,
					    lock, lf_entry);
				lf_free(lock);
				lock = overlap; /* for debug output below */
				break;
			}
			if (overlap->lf_start == lock->lf_start) {
				if (!needtolink)
					TAILQ_REMOVE(&lock->lf_state->ls_locks,
					    lock, lf_entry);
				TAILQ_INSERT_BEFORE(overlap, lock, lf_entry);
				overlap->lf_start = lock->lf_end + 1;
			} else
				lf_split(overlap, lock);
			lf_wakelock(overlap, 0);
			break;
		case 3: /* lock contains overlap */
			/*
			 * If downgrading lock, others may be able to
			 * acquire it, otherwise take the list.
			 */
			if (lock->lf_type == F_RDLCK &&
			    overlap->lf_type == F_WRLCK) {
				lf_wakelock(overlap, 0);
			} else {
				while ((ltmp =
				    TAILQ_FIRST(&overlap->lf_blkhd))) {
					TAILQ_REMOVE(&overlap->lf_blkhd, ltmp,
					    lf_block);
					ltmp->lf_blk = lock;
					TAILQ_INSERT_TAIL(&lock->lf_blkhd,
					    ltmp, lf_block);
				}
			}
			/*
			 * Add the new lock if necessary and delete the overlap.
			 */
			if (needtolink) {
				TAILQ_INSERT_BEFORE(overlap, lock, lf_entry);
				needtolink = 0;
			}
			TAILQ_REMOVE(&lock->lf_state->ls_locks, overlap, lf_entry);
			lf_free(overlap);
			continue;
		case 4: /* overlap starts before lock */
			/*
			 * Add lock after overlap on the list.
			 */
			if (!needtolink)
				TAILQ_REMOVE(&lock->lf_state->ls_locks, lock,
				    lf_entry);
			TAILQ_INSERT_AFTER(&lock->lf_state->ls_locks, overlap,
			    lock, lf_entry);
			overlap->lf_end = lock->lf_start - 1;
			lf_wakelock(overlap, 0);
			needtolink = 0;
			continue;
		case 5: /* overlap ends after lock */
			/*
			 * Add the new lock before overlap.
			 */
			if (needtolink)
				TAILQ_INSERT_BEFORE(overlap, lock, lf_entry);
			overlap->lf_start = lock->lf_end + 1;
			lf_wakelock(overlap, 0);
			break;
		}
		break;
	}
	LFPRINT(("lf_setlock: got the lock", lock), DEBUG_SETLOCK);
	return (0);
}

/*
 * Remove a byte-range lock on an inode.
 *
 * Generally, find the lock (or an overlap to that lock)
 * and remove it (or shrink it), then wakeup anyone we can.
 */
int
lf_clearlock(struct lockf *lock)
{
	struct lockf *lf, *overlap;
	int ovcase;

	rw_assert_wrlock(&lockf_lock);

	lf = TAILQ_FIRST(&lock->lf_state->ls_locks);
	if (lf == NULL)
		return (0);

	LFPRINT(("lf_clearlock", lock), DEBUG_CLEARLOCK);
	while ((ovcase = lf_findoverlap(lf, lock, SELF, &overlap))) {
		lf_wakelock(overlap, 0);

		switch (ovcase) {
		case 1: /* overlap == lock */
			TAILQ_REMOVE(&lock->lf_state->ls_locks, overlap,
			    lf_entry);
			lf_free(overlap);
			break;
		case 2: /* overlap contains lock: split it */
			if (overlap->lf_start == lock->lf_start) {
				overlap->lf_start = lock->lf_end + 1;
				break;
			}
			lf_split(overlap, lock);
			/*
			 * The lock is now part of the list, lf_clearlock() must
			 * ensure that the lock remains detached from the list.
			 */
			TAILQ_REMOVE(&lock->lf_state->ls_locks, lock, lf_entry);
			break;
		case 3: /* lock contains overlap */
			lf = TAILQ_NEXT(overlap, lf_entry);
			TAILQ_REMOVE(&lock->lf_state->ls_locks, overlap,
			    lf_entry);
			lf_free(overlap);
			continue;
		case 4: /* overlap starts before lock */
			overlap->lf_end = lock->lf_start - 1;
			lf = TAILQ_NEXT(overlap, lf_entry);
			continue;
		case 5: /* overlap ends after lock */
			overlap->lf_start = lock->lf_end + 1;
			break;
		}
		break;
	}
	return (0);
}

/*
 * Check whether there is a blocking lock,
 * and if so return its process identifier.
 */
int
lf_getlock(struct lockf *lock, struct flock *fl)
{
	struct lockf *block, *lf;

	rw_assert_wrlock(&lockf_lock);

	LFPRINT(("lf_getlock", lock), DEBUG_CLEARLOCK);

	lf = TAILQ_FIRST(&lock->lf_state->ls_locks);
	if ((block = lf_getblock(lf, lock)) != NULL) {
		fl->l_type = block->lf_type;
		fl->l_whence = SEEK_SET;
		fl->l_start = block->lf_start;
		if (block->lf_end == -1)
			fl->l_len = 0;
		else
			fl->l_len = block->lf_end - block->lf_start + 1;
		fl->l_pid = block->lf_pid;
	} else {
		fl->l_type = F_UNLCK;
	}
	return (0);
}

/*
 * Walk the list of locks for an inode and
 * return the first blocking lock.
 */
struct lockf *
lf_getblock(struct lockf *lf, struct lockf *lock)
{
	struct lockf *overlap;

	rw_assert_wrlock(&lockf_lock);

	while (lf_findoverlap(lf, lock, OTHERS, &overlap) != 0) {
		/*
		 * We've found an overlap, see if it blocks us
		 */
		if ((lock->lf_type == F_WRLCK || overlap->lf_type == F_WRLCK))
			return (overlap);
		/*
		 * Nope, point to the next one on the list and
		 * see if it blocks us
		 */
		lf = TAILQ_NEXT(overlap, lf_entry);
	}
	return (NULL);
}

/*
 * Walk the list of locks for an inode to
 * find an overlapping lock (if any).
 *
 * NOTE: this returns only the FIRST overlapping lock.  There
 *	 may be more than one.
 */
int
lf_findoverlap(struct lockf *lf, struct lockf *lock, int type,
    struct lockf **overlap)
{
	off_t start, end;

	rw_assert_wrlock(&lockf_lock);

	LFPRINT(("lf_findoverlap: looking for overlap in", lock), DEBUG_FINDOVR);

	*overlap = lf;
	start = lock->lf_start;
	end = lock->lf_end;
	while (lf != NULL) {
		if (((type & SELF) && lf->lf_id != lock->lf_id) ||
		    ((type & OTHERS) && lf->lf_id == lock->lf_id)) {
			*overlap = lf = TAILQ_NEXT(lf, lf_entry);
			continue;
		}
		LFPRINT(("\tchecking", lf), DEBUG_FINDOVR);
		/*
		 * OK, check for overlap
		 *
		 * Six cases:
		 *	0) no overlap
		 *	1) overlap == lock
		 *	2) overlap contains lock
		 *	3) lock contains overlap
		 *	4) overlap starts before lock
		 *	5) overlap ends after lock
		 */

		/* Case 0 */
		if ((lf->lf_end != -1 && start > lf->lf_end) ||
		    (end != -1 && lf->lf_start > end)) {
			DPRINTF(("no overlap\n"), DEBUG_FINDOVR);
			if ((type & SELF) && end != -1 && lf->lf_start > end)
				return (0);
			*overlap = lf = TAILQ_NEXT(lf, lf_entry);
			continue;
		}
		/* Case 1 */
		if ((lf->lf_start == start) && (lf->lf_end == end)) {
			DPRINTF(("overlap == lock\n"), DEBUG_FINDOVR);
			return (1);
		}
		/* Case 2 */
		if ((lf->lf_start <= start) &&
		    (lf->lf_end == -1 || (end != -1 && lf->lf_end >= end))) {
			DPRINTF(("overlap contains lock\n"), DEBUG_FINDOVR);
			return (2);
		}
		/* Case 3 */
		if (start <= lf->lf_start &&
		    (end == -1 || (lf->lf_end != -1 && end >= lf->lf_end))) {
			DPRINTF(("lock contains overlap\n"), DEBUG_FINDOVR);
			return (3);
		}
		/* Case 4 */
		if ((lf->lf_start < start) &&
		    ((lf->lf_end >= start) || (lf->lf_end == -1))) {
			DPRINTF(("overlap starts before lock\n"),
			    DEBUG_FINDOVR);
			return (4);
		}
		/* Case 5 */
		if ((lf->lf_start > start) && (end != -1) &&
		    ((lf->lf_end > end) || (lf->lf_end == -1))) {
			DPRINTF(("overlap ends after lock\n"), DEBUG_FINDOVR);
			return (5);
		}
		panic("lf_findoverlap: default");
	}
	return (0);
}

/*
 * Purge all locks associated with the given lock state.
 */
void
lf_purgelocks(struct lockf_state **state)
{
	struct lockf_state *ls;
	struct lockf *lock;

	rw_enter_write(&lockf_lock);

	ls = *state;
	if (ls == NULL)
		goto out;

	ls_ref(ls);

	/* Interrupt blocked locks and wait for all of them to finish. */
	TAILQ_FOREACH(lock, &ls->ls_locks, lf_entry) {
		LFPRINT(("lf_purgelocks: wakeup", lock), DEBUG_SETLOCK);
		lf_wakelock(lock, F_INTR);
	}
	while (!TAILQ_EMPTY(&ls->ls_pending))
		rwsleep_nsec(ls, &lockf_lock, PLOCK, "lockfp", INFSLP);

	/*
	 * Any remaining locks cannot block other locks at this point and can
	 * safely be removed.
	 */
	while ((lock = TAILQ_FIRST(&ls->ls_locks))) {
		TAILQ_REMOVE(&ls->ls_locks, lock, lf_entry);
		lf_free(lock);
	}

	/* This is the last expected thread to hold a lock state reference. */
	KASSERT(ls->ls_refs == 1);
	ls_rele(ls);

out:
	rw_exit_write(&lockf_lock);
}

/*
 * Split a lock and a contained region into
 * two or three locks as necessary.
 */
void
lf_split(struct lockf *lock1, struct lockf *lock2)
{
	struct lockf *splitlock;

	rw_assert_wrlock(&lockf_lock);

	LFPRINT(("lf_split", lock1), DEBUG_SPLIT);
	LFPRINT(("splitting from", lock2), DEBUG_SPLIT);

	/*
	 * Check to see if splitting into only two pieces.
	 */
	if (lock1->lf_start == lock2->lf_start) {
		lock1->lf_start = lock2->lf_end + 1;
		TAILQ_INSERT_BEFORE(lock1, lock2, lf_entry);
		return;
	}
	if (lock1->lf_end == lock2->lf_end) {
		lock1->lf_end = lock2->lf_start - 1;
		TAILQ_INSERT_AFTER(&lock1->lf_state->ls_locks, lock1, lock2,
		    lf_entry);
		return;
	}
	/*
	 * Make a new lock consisting of the last part of
	 * the encompassing lock
	 */
	splitlock = lf_alloc(lock1->lf_uid, 0);
	splitlock->lf_flags = lock1->lf_flags;
	splitlock->lf_type = lock1->lf_type;
	splitlock->lf_start = lock2->lf_end + 1;
	splitlock->lf_end = lock1->lf_end;
	splitlock->lf_id = lock1->lf_id;
	splitlock->lf_state = lock1->lf_state;
	splitlock->lf_blk = NULL;
	splitlock->lf_pid = lock1->lf_pid;
	TAILQ_INIT(&splitlock->lf_blkhd);
	ls_ref(splitlock->lf_state);
	lock1->lf_end = lock2->lf_start - 1;

	TAILQ_INSERT_AFTER(&lock1->lf_state->ls_locks, lock1, lock2, lf_entry);
	TAILQ_INSERT_AFTER(&lock1->lf_state->ls_locks, lock2, splitlock,
	    lf_entry);
}

/*
 * Wakeup a blocklist
 */
void
lf_wakelock(struct lockf *lock, int flags)
{
	struct lockf *wakelock;

	rw_assert_wrlock(&lockf_lock);

	while ((wakelock = TAILQ_FIRST(&lock->lf_blkhd))) {
		TAILQ_REMOVE(&lock->lf_blkhd, wakelock, lf_block);
		wakelock->lf_blk = NULL;
		wakelock->lf_flags |= flags;
		wakeup_one(wakelock);
	}
}

/*
 * Returns non-zero if the given lock would cause a deadlock.
 */
int
lf_deadlock(struct lockf *lock)
{
	struct lockf *block, *lf, *pending;

	lf = TAILQ_FIRST(&lock->lf_state->ls_locks);
	for (; (block = lf_getblock(lf, lock)) != NULL;
	    lf = TAILQ_NEXT(block, lf_entry)) {
		if ((block->lf_flags & F_POSIX) == 0)
			continue;

		TAILQ_FOREACH(pending, &lock->lf_state->ls_pending, lf_entry) {
			if (pending->lf_blk == NULL)
				continue; /* lock already unblocked */

			if (pending->lf_pid == block->lf_pid &&
			    pending->lf_blk->lf_pid == lock->lf_pid)
				return (1);
		}
	}

	return (0);
}

#ifdef LOCKF_DEBUG
/*
 * Print out a lock.
 */
void
lf_print(const char *tag, struct lockf *lock)
{
	struct lockf	*block;

	if (tag)
		printf("%s: ", tag);
	printf("lock %p", lock);
	if (lock == NULL) {
		printf("\n");
		return;
	}
	printf(", %s %p %s, start %lld, end %lld",
		lock->lf_flags & F_POSIX ? "posix" : "flock",
		lock->lf_id,
		lock->lf_type == F_RDLCK ? "shared" :
		lock->lf_type == F_WRLCK ? "exclusive" :
		lock->lf_type == F_UNLCK ? "unlock" :
		"unknown", lock->lf_start, lock->lf_end);
	printf(", next %p, state %p",
	    TAILQ_NEXT(lock, lf_entry), lock->lf_state);
	block = TAILQ_FIRST(&lock->lf_blkhd);
	if (block)
		printf(", block");
	TAILQ_FOREACH(block, &lock->lf_blkhd, lf_block)
		printf(" %p,", block);
	printf("\n");
}

void
lf_printlist(const char *tag, struct lockf *lock)
{
	struct lockf *lf;

	printf("%s: Lock list:\n", tag);
	TAILQ_FOREACH(lf, &lock->lf_state->ls_locks, lf_entry) {
		if (lock == lf)
			printf(" * ");
		else
			printf("   ");
		lf_print(NULL, lf);
	}
}
#endif /* LOCKF_DEBUG */
