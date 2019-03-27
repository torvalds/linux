/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
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
 *	from BSDI $Id: mutex.h,v 2.7.2.35 2000/04/27 03:10:26 cp Exp $
 * $FreeBSD$
 */

#ifndef _SYS_MUTEX_H_
#define _SYS_MUTEX_H_

#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>

#ifdef _KERNEL
#include <sys/pcpu.h>
#include <sys/lock_profile.h>
#include <sys/lockstat.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>

/*
 * Mutex types and options passed to mtx_init().  MTX_QUIET and MTX_DUPOK
 * can also be passed in.
 */
#define	MTX_DEF		0x00000000	/* DEFAULT (sleep) lock */ 
#define MTX_SPIN	0x00000001	/* Spin lock (disables interrupts) */
#define MTX_RECURSE	0x00000004	/* Option: lock allowed to recurse */
#define	MTX_NOWITNESS	0x00000008	/* Don't do any witness checking. */
#define MTX_NOPROFILE   0x00000020	/* Don't profile this lock */
#define	MTX_NEW		0x00000040	/* Don't check for double-init */

/*
 * Option flags passed to certain lock/unlock routines, through the use
 * of corresponding mtx_{lock,unlock}_flags() interface macros.
 */
#define	MTX_QUIET	LOP_QUIET	/* Don't log a mutex event */
#define	MTX_DUPOK	LOP_DUPOK	/* Don't log a duplicate acquire */

/*
 * State bits kept in mutex->mtx_lock, for the DEFAULT lock type. None of this,
 * with the exception of MTX_UNOWNED, applies to spin locks.
 */
#define	MTX_UNOWNED	0x00000000	/* Cookie for free mutex */
#define	MTX_RECURSED	0x00000001	/* lock recursed (for MTX_DEF only) */
#define	MTX_CONTESTED	0x00000002	/* lock contested (for MTX_DEF only) */
#define	MTX_DESTROYED	0x00000004	/* lock destroyed */
#define	MTX_FLAGMASK	(MTX_RECURSED | MTX_CONTESTED | MTX_DESTROYED)

/*
 * Prototypes
 *
 * NOTE: Functions prepended with `_' (underscore) are exported to other parts
 *	 of the kernel via macros, thus allowing us to use the cpp LOCK_FILE
 *	 and LOCK_LINE or for hiding the lock cookie crunching to the
 *	 consumers. These functions should not be called directly by any
 *	 code using the API. Their macros cover their functionality.
 *	 Functions with a `_' suffix are the entrypoint for the common
 *	 KPI covering both compat shims and fast path case.  These can be
 *	 used by consumers willing to pass options, file and line
 *	 informations, in an option-independent way.
 *
 * [See below for descriptions]
 *
 */
void	_mtx_init(volatile uintptr_t *c, const char *name, const char *type,
	    int opts);
void	_mtx_destroy(volatile uintptr_t *c);
void	mtx_sysinit(void *arg);
int	_mtx_trylock_flags_int(struct mtx *m, int opts LOCK_FILE_LINE_ARG_DEF);
int	_mtx_trylock_flags_(volatile uintptr_t *c, int opts, const char *file,
	    int line);
void	mutex_init(void);
#if LOCK_DEBUG > 0
void	__mtx_lock_sleep(volatile uintptr_t *c, uintptr_t v, int opts,
	    const char *file, int line);
void	__mtx_unlock_sleep(volatile uintptr_t *c, uintptr_t v, int opts,
	    const char *file, int line);
#else
void	__mtx_lock_sleep(volatile uintptr_t *c, uintptr_t v);
void	__mtx_unlock_sleep(volatile uintptr_t *c, uintptr_t v);
#endif

#ifdef SMP
#if LOCK_DEBUG > 0
void	_mtx_lock_spin_cookie(volatile uintptr_t *c, uintptr_t v, int opts,
	    const char *file, int line);
#else
void	_mtx_lock_spin_cookie(volatile uintptr_t *c, uintptr_t v);
#endif
#endif
void	__mtx_lock_flags(volatile uintptr_t *c, int opts, const char *file,
	    int line);
void	__mtx_unlock_flags(volatile uintptr_t *c, int opts, const char *file,
	    int line);
void	__mtx_lock_spin_flags(volatile uintptr_t *c, int opts, const char *file,
	     int line);
int	__mtx_trylock_spin_flags(volatile uintptr_t *c, int opts,
	     const char *file, int line);
void	__mtx_unlock_spin_flags(volatile uintptr_t *c, int opts,
	    const char *file, int line);
void	mtx_spin_wait_unlocked(struct mtx *m);

#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
void	__mtx_assert(const volatile uintptr_t *c, int what, const char *file,
	    int line);
#endif
void	thread_lock_flags_(struct thread *, int, const char *, int);
#if LOCK_DEBUG > 0
void	_thread_lock(struct thread *td, int opts, const char *file, int line);
#else
void	_thread_lock(struct thread *);
#endif

#if defined(LOCK_PROFILING) || (defined(KLD_MODULE) && !defined(KLD_TIED))
#define	thread_lock(tdp)						\
	thread_lock_flags_((tdp), 0, __FILE__, __LINE__)
#elif LOCK_DEBUG > 0
#define	thread_lock(tdp)						\
	_thread_lock((tdp), 0, __FILE__, __LINE__)
#else
#define	thread_lock(tdp)						\
	_thread_lock((tdp))
#endif

#if LOCK_DEBUG > 0
#define	thread_lock_flags(tdp, opt)					\
	thread_lock_flags_((tdp), (opt), __FILE__, __LINE__)
#else
#define	thread_lock_flags(tdp, opt)					\
	_thread_lock(tdp)
#endif

#define	thread_unlock(tdp)						\
       mtx_unlock_spin((tdp)->td_lock)

/*
 * Top-level macros to provide lock cookie once the actual mtx is passed.
 * They will also prevent passing a malformed object to the mtx KPI by
 * failing compilation as the mtx_lock reserved member will not be found.
 */
#define	mtx_init(m, n, t, o)						\
	_mtx_init(&(m)->mtx_lock, n, t, o)
#define	mtx_destroy(m)							\
	_mtx_destroy(&(m)->mtx_lock)
#define	mtx_trylock_flags_(m, o, f, l)					\
	_mtx_trylock_flags_(&(m)->mtx_lock, o, f, l)
#if LOCK_DEBUG > 0
#define	_mtx_lock_sleep(m, v, o, f, l)					\
	__mtx_lock_sleep(&(m)->mtx_lock, v, o, f, l)
#define	_mtx_unlock_sleep(m, v, o, f, l)				\
	__mtx_unlock_sleep(&(m)->mtx_lock, v, o, f, l)
#else
#define	_mtx_lock_sleep(m, v, o, f, l)					\
	__mtx_lock_sleep(&(m)->mtx_lock, v)
#define	_mtx_unlock_sleep(m, v, o, f, l)				\
	__mtx_unlock_sleep(&(m)->mtx_lock, v)
#endif
#ifdef SMP
#if LOCK_DEBUG > 0
#define	_mtx_lock_spin(m, v, o, f, l)					\
	_mtx_lock_spin_cookie(&(m)->mtx_lock, v, o, f, l)
#else
#define	_mtx_lock_spin(m, v, o, f, l)					\
	_mtx_lock_spin_cookie(&(m)->mtx_lock, v)
#endif
#endif
#define	_mtx_lock_flags(m, o, f, l)					\
	__mtx_lock_flags(&(m)->mtx_lock, o, f, l)
#define	_mtx_unlock_flags(m, o, f, l)					\
	__mtx_unlock_flags(&(m)->mtx_lock, o, f, l)
#define	_mtx_lock_spin_flags(m, o, f, l)				\
	__mtx_lock_spin_flags(&(m)->mtx_lock, o, f, l)
#define	_mtx_trylock_spin_flags(m, o, f, l)				\
	__mtx_trylock_spin_flags(&(m)->mtx_lock, o, f, l)
#define	_mtx_unlock_spin_flags(m, o, f, l)				\
	__mtx_unlock_spin_flags(&(m)->mtx_lock, o, f, l)
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
#define	_mtx_assert(m, w, f, l)						\
	__mtx_assert(&(m)->mtx_lock, w, f, l)
#endif

#define	mtx_recurse	lock_object.lo_data

/* Very simple operations on mtx_lock. */

/* Try to obtain mtx_lock once. */
#define _mtx_obtain_lock(mp, tid)					\
	atomic_cmpset_acq_ptr(&(mp)->mtx_lock, MTX_UNOWNED, (tid))

#define _mtx_obtain_lock_fetch(mp, vp, tid)				\
	atomic_fcmpset_acq_ptr(&(mp)->mtx_lock, vp, (tid))

/* Try to release mtx_lock if it is unrecursed and uncontested. */
#define _mtx_release_lock(mp, tid)					\
	atomic_cmpset_rel_ptr(&(mp)->mtx_lock, (tid), MTX_UNOWNED)

/* Release mtx_lock quickly, assuming we own it. */
#define _mtx_release_lock_quick(mp)					\
	atomic_store_rel_ptr(&(mp)->mtx_lock, MTX_UNOWNED)

#define	_mtx_release_lock_fetch(mp, vp)					\
	atomic_fcmpset_rel_ptr(&(mp)->mtx_lock, (vp), MTX_UNOWNED)

/*
 * Full lock operations that are suitable to be inlined in non-debug
 * kernels.  If the lock cannot be acquired or released trivially then
 * the work is deferred to another function.
 */

/* Lock a normal mutex. */
#define __mtx_lock(mp, tid, opts, file, line) do {			\
	uintptr_t _tid = (uintptr_t)(tid);				\
	uintptr_t _v = MTX_UNOWNED;					\
									\
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(adaptive__acquire) ||\
	    !_mtx_obtain_lock_fetch((mp), &_v, _tid)))			\
		_mtx_lock_sleep((mp), _v, (opts), (file), (line));	\
} while (0)

/*
 * Lock a spin mutex.  For spinlocks, we handle recursion inline (it
 * turns out that function calls can be significantly expensive on
 * some architectures).  Since spin locks are not _too_ common,
 * inlining this code is not too big a deal.
 */
#ifdef SMP
#define __mtx_lock_spin(mp, tid, opts, file, line) do {			\
	uintptr_t _tid = (uintptr_t)(tid);				\
	uintptr_t _v = MTX_UNOWNED;					\
									\
	spinlock_enter();						\
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(spin__acquire) ||	\
	    !_mtx_obtain_lock_fetch((mp), &_v, _tid))) 			\
		_mtx_lock_spin((mp), _v, (opts), (file), (line)); 	\
} while (0)
#define __mtx_trylock_spin(mp, tid, opts, file, line) __extension__  ({	\
	uintptr_t _tid = (uintptr_t)(tid);				\
	int _ret;							\
									\
	spinlock_enter();						\
	if (((mp)->mtx_lock != MTX_UNOWNED || !_mtx_obtain_lock((mp), _tid))) {\
		spinlock_exit();					\
		_ret = 0;						\
	} else {							\
		LOCKSTAT_PROFILE_OBTAIN_LOCK_SUCCESS(spin__acquire,	\
		    mp, 0, 0, file, line);				\
		_ret = 1;						\
	}								\
	_ret;								\
})
#else /* SMP */
#define __mtx_lock_spin(mp, tid, opts, file, line) do {			\
	uintptr_t _tid = (uintptr_t)(tid);				\
									\
	spinlock_enter();						\
	if ((mp)->mtx_lock == _tid)					\
		(mp)->mtx_recurse++;					\
	else {								\
		KASSERT((mp)->mtx_lock == MTX_UNOWNED, ("corrupt spinlock")); \
		(mp)->mtx_lock = _tid;					\
	}								\
} while (0)
#define __mtx_trylock_spin(mp, tid, opts, file, line) __extension__  ({	\
	uintptr_t _tid = (uintptr_t)(tid);				\
	int _ret;							\
									\
	spinlock_enter();						\
	if ((mp)->mtx_lock != MTX_UNOWNED) {				\
		spinlock_exit();					\
		_ret = 0;						\
	} else {							\
		(mp)->mtx_lock = _tid;					\
		_ret = 1;						\
	}								\
	_ret;								\
})
#endif /* SMP */

/* Unlock a normal mutex. */
#define __mtx_unlock(mp, tid, opts, file, line) do {			\
	uintptr_t _v = (uintptr_t)(tid);				\
									\
	if (__predict_false(LOCKSTAT_PROFILE_ENABLED(adaptive__release) ||\
	    !_mtx_release_lock_fetch((mp), &_v)))			\
		_mtx_unlock_sleep((mp), _v, (opts), (file), (line));	\
} while (0)

/*
 * Unlock a spin mutex.  For spinlocks, we can handle everything
 * inline, as it's pretty simple and a function call would be too
 * expensive (at least on some architectures).  Since spin locks are
 * not _too_ common, inlining this code is not too big a deal.
 *
 * Since we always perform a spinlock_enter() when attempting to acquire a
 * spin lock, we need to always perform a matching spinlock_exit() when
 * releasing a spin lock.  This includes the recursion cases.
 */
#ifdef SMP
#define __mtx_unlock_spin(mp) do {					\
	if (mtx_recursed((mp)))						\
		(mp)->mtx_recurse--;					\
	else {								\
		LOCKSTAT_PROFILE_RELEASE_LOCK(spin__release, mp);	\
		_mtx_release_lock_quick((mp));				\
	}								\
	spinlock_exit();						\
} while (0)
#else /* SMP */
#define __mtx_unlock_spin(mp) do {					\
	if (mtx_recursed((mp)))						\
		(mp)->mtx_recurse--;					\
	else {								\
		LOCKSTAT_PROFILE_RELEASE_LOCK(spin__release, mp);	\
		(mp)->mtx_lock = MTX_UNOWNED;				\
	}								\
	spinlock_exit();						\
} while (0)
#endif /* SMP */

/*
 * Exported lock manipulation interface.
 *
 * mtx_lock(m) locks MTX_DEF mutex `m'
 *
 * mtx_lock_spin(m) locks MTX_SPIN mutex `m'
 *
 * mtx_unlock(m) unlocks MTX_DEF mutex `m'
 *
 * mtx_unlock_spin(m) unlocks MTX_SPIN mutex `m'
 *
 * mtx_lock_spin_flags(m, opts) and mtx_lock_flags(m, opts) locks mutex `m'
 *     and passes option flags `opts' to the "hard" function, if required.
 *     With these routines, it is possible to pass flags such as MTX_QUIET
 *     to the appropriate lock manipulation routines.
 *
 * mtx_trylock(m) attempts to acquire MTX_DEF mutex `m' but doesn't sleep if
 *     it cannot. Rather, it returns 0 on failure and non-zero on success.
 *     It does NOT handle recursion as we assume that if a caller is properly
 *     using this part of the interface, he will know that the lock in question
 *     is _not_ recursed.
 *
 * mtx_trylock_flags(m, opts) is used the same way as mtx_trylock() but accepts
 *     relevant option flags `opts.'
 *
 * mtx_trylock_spin(m) attempts to acquire MTX_SPIN mutex `m' but doesn't
 *     spin if it cannot.  Rather, it returns 0 on failure and non-zero on
 *     success.  It always returns failure for recursed lock attempts.
 *
 * mtx_initialized(m) returns non-zero if the lock `m' has been initialized.
 *
 * mtx_owned(m) returns non-zero if the current thread owns the lock `m'
 *
 * mtx_recursed(m) returns non-zero if the lock `m' is presently recursed.
 */ 
#define mtx_lock(m)		mtx_lock_flags((m), 0)
#define mtx_lock_spin(m)	mtx_lock_spin_flags((m), 0)
#define mtx_trylock(m)		mtx_trylock_flags((m), 0)
#define mtx_trylock_spin(m)	mtx_trylock_spin_flags((m), 0)
#define mtx_unlock(m)		mtx_unlock_flags((m), 0)
#define mtx_unlock_spin(m)	mtx_unlock_spin_flags((m), 0)

struct mtx_pool;

struct mtx_pool *mtx_pool_create(const char *mtx_name, int pool_size, int opts);
void mtx_pool_destroy(struct mtx_pool **poolp);
struct mtx *mtx_pool_find(struct mtx_pool *pool, void *ptr);
struct mtx *mtx_pool_alloc(struct mtx_pool *pool);
#define mtx_pool_lock(pool, ptr)					\
	mtx_lock(mtx_pool_find((pool), (ptr)))
#define mtx_pool_lock_spin(pool, ptr)					\
	mtx_lock_spin(mtx_pool_find((pool), (ptr)))
#define mtx_pool_unlock(pool, ptr)					\
	mtx_unlock(mtx_pool_find((pool), (ptr)))
#define mtx_pool_unlock_spin(pool, ptr)					\
	mtx_unlock_spin(mtx_pool_find((pool), (ptr)))

/*
 * mtxpool_sleep is a general purpose pool of sleep mutexes.
 */
extern struct mtx_pool *mtxpool_sleep;

#ifndef LOCK_DEBUG
#error LOCK_DEBUG not defined, include <sys/lock.h> before <sys/mutex.h>
#endif
#if LOCK_DEBUG > 0 || defined(MUTEX_NOINLINE)
#define	mtx_lock_flags_(m, opts, file, line)				\
	_mtx_lock_flags((m), (opts), (file), (line))
#define	mtx_unlock_flags_(m, opts, file, line)				\
	_mtx_unlock_flags((m), (opts), (file), (line))
#define	mtx_lock_spin_flags_(m, opts, file, line)			\
	_mtx_lock_spin_flags((m), (opts), (file), (line))
#define	mtx_trylock_spin_flags_(m, opts, file, line)			\
	_mtx_trylock_spin_flags((m), (opts), (file), (line))
#define	mtx_unlock_spin_flags_(m, opts, file, line)			\
	_mtx_unlock_spin_flags((m), (opts), (file), (line))
#else	/* LOCK_DEBUG == 0 && !MUTEX_NOINLINE */
#define	mtx_lock_flags_(m, opts, file, line)				\
	__mtx_lock((m), curthread, (opts), (file), (line))
#define	mtx_unlock_flags_(m, opts, file, line)				\
	__mtx_unlock((m), curthread, (opts), (file), (line))
#define	mtx_lock_spin_flags_(m, opts, file, line)			\
	__mtx_lock_spin((m), curthread, (opts), (file), (line))
#define	mtx_trylock_spin_flags_(m, opts, file, line)			\
	__mtx_trylock_spin((m), curthread, (opts), (file), (line))
#define	mtx_unlock_spin_flags_(m, opts, file, line)			\
	__mtx_unlock_spin((m))
#endif	/* LOCK_DEBUG > 0 || MUTEX_NOINLINE */

#ifdef INVARIANTS
#define	mtx_assert_(m, what, file, line)				\
	_mtx_assert((m), (what), (file), (line))

#define GIANT_REQUIRED	mtx_assert_(&Giant, MA_OWNED, __FILE__, __LINE__)

#else	/* INVARIANTS */
#define mtx_assert_(m, what, file, line)	(void)0
#define GIANT_REQUIRED
#endif	/* INVARIANTS */

#define	mtx_lock_flags(m, opts)						\
	mtx_lock_flags_((m), (opts), LOCK_FILE, LOCK_LINE)
#define	mtx_unlock_flags(m, opts)					\
	mtx_unlock_flags_((m), (opts), LOCK_FILE, LOCK_LINE)
#define	mtx_lock_spin_flags(m, opts)					\
	mtx_lock_spin_flags_((m), (opts), LOCK_FILE, LOCK_LINE)
#define	mtx_unlock_spin_flags(m, opts)					\
	mtx_unlock_spin_flags_((m), (opts), LOCK_FILE, LOCK_LINE)
#define mtx_trylock_flags(m, opts)					\
	mtx_trylock_flags_((m), (opts), LOCK_FILE, LOCK_LINE)
#define mtx_trylock_spin_flags(m, opts)					\
	mtx_trylock_spin_flags_((m), (opts), LOCK_FILE, LOCK_LINE)
#define	mtx_assert(m, what)						\
	mtx_assert_((m), (what), __FILE__, __LINE__)

#define	mtx_sleep(chan, mtx, pri, wmesg, timo)				\
	_sleep((chan), &(mtx)->lock_object, (pri), (wmesg),		\
	    tick_sbt * (timo), 0, C_HARDCLOCK)

#define	MTX_READ_VALUE(m)	((m)->mtx_lock)

#define	mtx_initialized(m)	lock_initialized(&(m)->lock_object)

#define lv_mtx_owner(v)	((struct thread *)((v) & ~MTX_FLAGMASK))

#define mtx_owner(m)	lv_mtx_owner(MTX_READ_VALUE(m))

#define mtx_owned(m)	(mtx_owner(m) == curthread)

#define mtx_recursed(m)	((m)->mtx_recurse != 0)

#define mtx_name(m)	((m)->lock_object.lo_name)

/*
 * Global locks.
 */
extern struct mtx Giant;
extern struct mtx blocked_lock;

/*
 * Giant lock manipulation and clean exit macros.
 * Used to replace return with an exit Giant and return.
 *
 * Note that DROP_GIANT*() needs to be paired with PICKUP_GIANT() 
 * The #ifndef is to allow lint-like tools to redefine DROP_GIANT.
 */
#ifndef DROP_GIANT
#define DROP_GIANT()							\
do {									\
	int _giantcnt = 0;						\
	WITNESS_SAVE_DECL(Giant);					\
									\
	if (__predict_false(mtx_owned(&Giant))) {			\
		WITNESS_SAVE(&Giant.lock_object, Giant);		\
		for (_giantcnt = 0; mtx_owned(&Giant) &&		\
		    !SCHEDULER_STOPPED(); _giantcnt++)			\
			mtx_unlock(&Giant);				\
	}

#define PICKUP_GIANT()							\
	PARTIAL_PICKUP_GIANT();						\
} while (0)

#define PARTIAL_PICKUP_GIANT()						\
	mtx_assert(&Giant, MA_NOTOWNED);				\
	if (__predict_false(_giantcnt > 0)) {				\
		while (_giantcnt--)					\
			mtx_lock(&Giant);				\
		WITNESS_RESTORE(&Giant.lock_object, Giant);		\
	}
#endif

struct mtx_args {
	void		*ma_mtx;
	const char 	*ma_desc;
	int		 ma_opts;
};

#define	MTX_SYSINIT(name, mtx, desc, opts)				\
	static struct mtx_args name##_args = {				\
		(mtx),							\
		(desc),							\
		(opts)							\
	};								\
	SYSINIT(name##_mtx_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    mtx_sysinit, &name##_args);					\
	SYSUNINIT(name##_mtx_sysuninit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    _mtx_destroy, __DEVOLATILE(void *, &(mtx)->mtx_lock))

/*
 * The INVARIANTS-enabled mtx_assert() functionality.
 *
 * The constants need to be defined for INVARIANT_SUPPORT infrastructure
 * support as _mtx_assert() itself uses them and the latter implies that
 * _mtx_assert() must build.
 */
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
#define MA_OWNED	LA_XLOCKED
#define MA_NOTOWNED	LA_UNLOCKED
#define MA_RECURSED	LA_RECURSED
#define MA_NOTRECURSED	LA_NOTRECURSED
#endif

/*
 * Common lock type names.
 */
#define	MTX_NETWORK_LOCK	"network driver"

#endif	/* _KERNEL */
#endif	/* _SYS_MUTEX_H_ */
