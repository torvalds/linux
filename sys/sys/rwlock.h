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
 *
 * $FreeBSD$
 */

#ifndef _SYS_RWLOCK_H_
#define _SYS_RWLOCK_H_

#include <sys/_lock.h>
#include <sys/_rwlock.h>
#include <sys/lock_profile.h>
#include <sys/lockstat.h>

#ifdef _KERNEL
#include <sys/pcpu.h>
#include <machine/atomic.h>
#endif

/*
 * The rw_lock field consists of several fields.  The low bit indicates
 * if the lock is locked with a read (shared) or write (exclusive) lock.
 * A value of 0 indicates a write lock, and a value of 1 indicates a read
 * lock.  Bit 1 is a boolean indicating if there are any threads waiting
 * for a read lock.  Bit 2 is a boolean indicating if there are any threads
 * waiting for a write lock.  The rest of the variable's definition is
 * dependent on the value of the first bit.  For a write lock, it is a
 * pointer to the thread holding the lock, similar to the mtx_lock field of
 * mutexes.  For read locks, it is a count of read locks that are held.
 *
 * When the lock is not locked by any thread, it is encoded as a read lock
 * with zero waiters.
 */

#define	RW_LOCK_READ		0x01
#define	RW_LOCK_READ_WAITERS	0x02
#define	RW_LOCK_WRITE_WAITERS	0x04
#define	RW_LOCK_WRITE_SPINNER	0x08
#define	RW_LOCK_WRITER_RECURSED	0x10
#define	RW_LOCK_FLAGMASK						\
	(RW_LOCK_READ | RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS |	\
	RW_LOCK_WRITE_SPINNER | RW_LOCK_WRITER_RECURSED)
#define	RW_LOCK_WAITERS		(RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS)

#define	RW_OWNER(x)		((x) & ~RW_LOCK_FLAGMASK)
#define	RW_READERS_SHIFT	5
#define	RW_READERS(x)		(RW_OWNER((x)) >> RW_READERS_SHIFT)
#define	RW_READERS_LOCK(x)	((x) << RW_READERS_SHIFT | RW_LOCK_READ)
#define	RW_ONE_READER		(1 << RW_READERS_SHIFT)

#define	RW_UNLOCKED		RW_READERS_LOCK(0)
#define	RW_DESTROYED		(RW_LOCK_READ_WAITERS | RW_LOCK_WRITE_WAITERS)

#ifdef _KERNEL

#define	rw_recurse	lock_object.lo_data

#define	RW_READ_VALUE(x)	((x)->rw_lock)

/* Very simple operations on rw_lock. */

/* Try to obtain a write lock once. */
#define	_rw_write_lock(rw, tid)						\
	atomic_cmpset_acq_ptr(&(rw)->rw_lock, RW_UNLOCKED, (tid))

#define	_rw_write_lock_fetch(rw, vp, tid)				\
	atomic_fcmpset_acq_ptr(&(rw)->rw_lock, vp, (tid))

/* Release a write lock quickly if there are no waiters. */
#define	_rw_write_unlock(rw, tid)					\
	atomic_cmpset_rel_ptr(&(rw)->rw_lock, (tid), RW_UNLOCKED)

#define	_rw_write_unlock_fetch(rw, tid)					\
	atomic_fcmpset_rel_ptr(&(rw)->rw_lock, (tid), RW_UNLOCKED)

/*
 * Full lock operations that are suitable to be inlined in non-debug
 * kernels.  If the lock cannot be acquired or released trivially then
 * the work is deferred to another function.
 */

/* Acquire a write lock. */
#define	__rw_wlock(rw, tid, file, line) do {				\
	uintptr_t _tid = (uintptr_t)(tid);				\
	uintptr_t _v = RW_UNLOCKED;					\
									\
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(rw__acquire) ||	\
	    !_rw_write_lock_fetch((rw), &_v, _tid)))			\
		_rw_wlock_hard((rw), _v, (file), (line));		\
} while (0)

/* Release a write lock. */
#define	__rw_wunlock(rw, tid, file, line) do {				\
	uintptr_t _v = (uintptr_t)(tid);				\
									\
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(rw__release) ||	\
	    !_rw_write_unlock_fetch((rw), &_v)))			\
		_rw_wunlock_hard((rw), _v, (file), (line));		\
} while (0)

/*
 * Function prototypes.  Routines that start with _ are not part of the
 * external API and should not be called directly.  Wrapper macros should
 * be used instead.
 */
void	_rw_init_flags(volatile uintptr_t *c, const char *name, int opts);
void	_rw_destroy(volatile uintptr_t *c);
void	rw_sysinit(void *arg);
int	_rw_wowned(const volatile uintptr_t *c);
void	_rw_wlock_cookie(volatile uintptr_t *c, const char *file, int line);
int	__rw_try_wlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
int	__rw_try_wlock(volatile uintptr_t *c, const char *file, int line);
void	_rw_wunlock_cookie(volatile uintptr_t *c, const char *file, int line);
void	__rw_rlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
void	__rw_rlock(volatile uintptr_t *c, const char *file, int line);
int	__rw_try_rlock_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
int	__rw_try_rlock(volatile uintptr_t *c, const char *file, int line);
void	_rw_runlock_cookie_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
void	_rw_runlock_cookie(volatile uintptr_t *c, const char *file, int line);
void	__rw_wlock_hard(volatile uintptr_t *c, uintptr_t v
	    LOCK_FILE_LINE_ARG_DEF);
void	__rw_wunlock_hard(volatile uintptr_t *c, uintptr_t v
	    LOCK_FILE_LINE_ARG_DEF);
int	__rw_try_upgrade_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
int	__rw_try_upgrade(volatile uintptr_t *c, const char *file, int line);
void	__rw_downgrade_int(struct rwlock *rw LOCK_FILE_LINE_ARG_DEF);
void	__rw_downgrade(volatile uintptr_t *c, const char *file, int line);
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
void	__rw_assert(const volatile uintptr_t *c, int what, const char *file,
	    int line);
#endif

/*
 * Top-level macros to provide lock cookie once the actual rwlock is passed.
 * They will also prevent passing a malformed object to the rwlock KPI by
 * failing compilation as the rw_lock reserved member will not be found.
 */
#define	rw_init(rw, n)							\
	_rw_init_flags(&(rw)->rw_lock, n, 0)
#define	rw_init_flags(rw, n, o)						\
	_rw_init_flags(&(rw)->rw_lock, n, o)
#define	rw_destroy(rw)							\
	_rw_destroy(&(rw)->rw_lock)
#define	rw_wowned(rw)							\
	_rw_wowned(&(rw)->rw_lock)
#define	_rw_wlock(rw, f, l)						\
	_rw_wlock_cookie(&(rw)->rw_lock, f, l)
#define	_rw_try_wlock(rw, f, l)						\
	__rw_try_wlock(&(rw)->rw_lock, f, l)
#define	_rw_wunlock(rw, f, l)						\
	_rw_wunlock_cookie(&(rw)->rw_lock, f, l)
#define	_rw_try_rlock(rw, f, l)						\
	__rw_try_rlock(&(rw)->rw_lock, f, l)
#if LOCK_DEBUG > 0
#define	_rw_rlock(rw, f, l)						\
	__rw_rlock(&(rw)->rw_lock, f, l)
#define	_rw_runlock(rw, f, l)						\
	_rw_runlock_cookie(&(rw)->rw_lock, f, l)
#else
#define	_rw_rlock(rw, f, l)						\
	__rw_rlock_int((struct rwlock *)rw)
#define	_rw_runlock(rw, f, l)						\
	_rw_runlock_cookie_int((struct rwlock *)rw)
#endif
#if LOCK_DEBUG > 0
#define	_rw_wlock_hard(rw, v, f, l)					\
	__rw_wlock_hard(&(rw)->rw_lock, v, f, l)
#define	_rw_wunlock_hard(rw, v, f, l)					\
	__rw_wunlock_hard(&(rw)->rw_lock, v, f, l)
#define	_rw_try_upgrade(rw, f, l)					\
	__rw_try_upgrade(&(rw)->rw_lock, f, l)
#define	_rw_downgrade(rw, f, l)						\
	__rw_downgrade(&(rw)->rw_lock, f, l)
#else
#define	_rw_wlock_hard(rw, v, f, l)					\
	__rw_wlock_hard(&(rw)->rw_lock, v)
#define	_rw_wunlock_hard(rw, v, f, l)					\
	__rw_wunlock_hard(&(rw)->rw_lock, v)
#define	_rw_try_upgrade(rw, f, l)					\
	__rw_try_upgrade_int(rw)
#define	_rw_downgrade(rw, f, l)						\
	__rw_downgrade_int(rw)
#endif
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
#define	_rw_assert(rw, w, f, l)						\
	__rw_assert(&(rw)->rw_lock, w, f, l)
#endif


/*
 * Public interface for lock operations.
 */

#ifndef LOCK_DEBUG
#error LOCK_DEBUG not defined, include <sys/lock.h> before <sys/rwlock.h>
#endif
#if LOCK_DEBUG > 0 || defined(RWLOCK_NOINLINE)
#define	rw_wlock(rw)		_rw_wlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_wunlock(rw)		_rw_wunlock((rw), LOCK_FILE, LOCK_LINE)
#else
#define	rw_wlock(rw)							\
	__rw_wlock((rw), curthread, LOCK_FILE, LOCK_LINE)
#define	rw_wunlock(rw)							\
	__rw_wunlock((rw), curthread, LOCK_FILE, LOCK_LINE)
#endif
#define	rw_rlock(rw)		_rw_rlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_runlock(rw)		_rw_runlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_try_rlock(rw)	_rw_try_rlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_try_upgrade(rw)	_rw_try_upgrade((rw), LOCK_FILE, LOCK_LINE)
#define	rw_try_wlock(rw)	_rw_try_wlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_downgrade(rw)	_rw_downgrade((rw), LOCK_FILE, LOCK_LINE)
#define	rw_unlock(rw)	do {						\
	if (rw_wowned(rw))						\
		rw_wunlock(rw);						\
	else								\
		rw_runlock(rw);						\
} while (0)
#define	rw_sleep(chan, rw, pri, wmesg, timo)				\
	_sleep((chan), &(rw)->lock_object, (pri), (wmesg),		\
	    tick_sbt * (timo), 0, C_HARDCLOCK)

#define	rw_initialized(rw)	lock_initialized(&(rw)->lock_object)

struct rw_args {
	void		*ra_rw;
	const char 	*ra_desc;
	int		ra_flags;
};

#define	RW_SYSINIT_FLAGS(name, rw, desc, flags)				\
	static struct rw_args name##_args = {				\
		(rw),							\
		(desc),							\
		(flags),						\
	};								\
	SYSINIT(name##_rw_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    rw_sysinit, &name##_args);					\
	SYSUNINIT(name##_rw_sysuninit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    _rw_destroy, __DEVOLATILE(void *, &(rw)->rw_lock))

#define	RW_SYSINIT(name, rw, desc)	RW_SYSINIT_FLAGS(name, rw, desc, 0)

/*
 * Options passed to rw_init_flags().
 */
#define	RW_DUPOK	0x01
#define	RW_NOPROFILE	0x02
#define	RW_NOWITNESS	0x04
#define	RW_QUIET	0x08
#define	RW_RECURSE	0x10
#define	RW_NEW		0x20

/*
 * The INVARIANTS-enabled rw_assert() functionality.
 *
 * The constants need to be defined for INVARIANT_SUPPORT infrastructure
 * support as _rw_assert() itself uses them and the latter implies that
 * _rw_assert() must build.
 */
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
#define	RA_LOCKED		LA_LOCKED
#define	RA_RLOCKED		LA_SLOCKED
#define	RA_WLOCKED		LA_XLOCKED
#define	RA_UNLOCKED		LA_UNLOCKED
#define	RA_RECURSED		LA_RECURSED
#define	RA_NOTRECURSED		LA_NOTRECURSED
#endif

#ifdef INVARIANTS
#define	rw_assert(rw, what)	_rw_assert((rw), (what), LOCK_FILE, LOCK_LINE)
#else
#define	rw_assert(rw, what)
#endif

#endif /* _KERNEL */
#endif /* !_SYS_RWLOCK_H_ */
