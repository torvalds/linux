/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
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
 *	from BSDI Id: mutex.h,v 2.7.2.35 2000/04/27 03:10:26 cp
 * $FreeBSD$
 */

#ifndef _SYS_LOCK_H_
#define _SYS_LOCK_H_

#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/ktr_class.h>

struct lock_list_entry;
struct thread;

/*
 * Lock classes.  Each lock has a class which describes characteristics
 * common to all types of locks of a given class.
 *
 * Spin locks in general must always protect against preemption, as it is
 * an error to perform any type of context switch while holding a spin lock.
 * Also, for an individual lock to be recursable, its class must allow
 * recursion and the lock itself must explicitly allow recursion.
 *
 * The 'lc_ddb_show' function pointer is used to dump class-specific
 * data for the 'show lock' DDB command.  The 'lc_lock' and
 * 'lc_unlock' function pointers are used in sleep(9) and cv_wait(9)
 * to lock and unlock locks while blocking on a sleep queue.  The
 * return value of 'lc_unlock' will be passed to 'lc_lock' on resume
 * to allow communication of state between the two routines.
 */

struct lock_class {
	const		char *lc_name;
	u_int		lc_flags;
	void		(*lc_assert)(const struct lock_object *lock, int what);
	void		(*lc_ddb_show)(const struct lock_object *lock);
	void		(*lc_lock)(struct lock_object *lock, uintptr_t how);
	int		(*lc_owner)(const struct lock_object *lock,
			    struct thread **owner);
	uintptr_t	(*lc_unlock)(struct lock_object *lock);
};

#define	LC_SLEEPLOCK	0x00000001	/* Sleep lock. */
#define	LC_SPINLOCK	0x00000002	/* Spin lock. */
#define	LC_SLEEPABLE	0x00000004	/* Sleeping allowed with this lock. */
#define	LC_RECURSABLE	0x00000008	/* Locks of this type may recurse. */
#define	LC_UPGRADABLE	0x00000010	/* Upgrades and downgrades permitted. */

#define	LO_CLASSFLAGS	0x0000ffff	/* Class specific flags. */
#define	LO_INITIALIZED	0x00010000	/* Lock has been initialized. */
#define	LO_WITNESS	0x00020000	/* Should witness monitor this lock. */
#define	LO_QUIET	0x00040000	/* Don't log locking operations. */
#define	LO_RECURSABLE	0x00080000	/* Lock may recurse. */
#define	LO_SLEEPABLE	0x00100000	/* Lock may be held while sleeping. */
#define	LO_UPGRADABLE	0x00200000	/* Lock may be upgraded/downgraded. */
#define	LO_DUPOK	0x00400000	/* Don't check for duplicate acquires */
#define	LO_IS_VNODE	0x00800000	/* Tell WITNESS about a VNODE lock */
#define	LO_CLASSMASK	0x0f000000	/* Class index bitmask. */
#define LO_NOPROFILE    0x10000000      /* Don't profile this lock */
#define	LO_NEW		0x20000000	/* Don't check for double-init */

/*
 * Lock classes are statically assigned an index into the gobal lock_classes
 * array.  Debugging code looks up the lock class for a given lock object
 * by indexing the array.
 */
#define	LO_CLASSSHIFT		24
#define	LO_CLASSINDEX(lock)	((((lock)->lo_flags) & LO_CLASSMASK) >> LO_CLASSSHIFT)
#define	LOCK_CLASS(lock)	(lock_classes[LO_CLASSINDEX((lock))])
#define	LOCK_CLASS_MAX		(LO_CLASSMASK >> LO_CLASSSHIFT)

/*
 * Option flags passed to lock operations that witness also needs to know
 * about or that are generic across all locks.
 */
#define	LOP_NEWORDER	0x00000001	/* Define a new lock order. */
#define	LOP_QUIET	0x00000002	/* Don't log locking operations. */
#define	LOP_TRYLOCK	0x00000004	/* Don't check lock order. */
#define	LOP_EXCLUSIVE	0x00000008	/* Exclusive lock. */
#define	LOP_DUPOK	0x00000010	/* Don't check for duplicate acquires */

/* Flags passed to witness_assert. */
#define	LA_MASKASSERT	0x000000ff	/* Mask for witness defined asserts. */
#define	LA_UNLOCKED	0x00000000	/* Lock is unlocked. */
#define	LA_LOCKED	0x00000001	/* Lock is at least share locked. */
#define	LA_SLOCKED	0x00000002	/* Lock is exactly share locked. */
#define	LA_XLOCKED	0x00000004	/* Lock is exclusively locked. */
#define	LA_RECURSED	0x00000008	/* Lock is recursed. */
#define	LA_NOTRECURSED	0x00000010	/* Lock is not recursed. */

#ifdef _KERNEL
/*
 * If any of WITNESS, INVARIANTS, or KTR_LOCK KTR tracing has been enabled,
 * then turn on LOCK_DEBUG.  When this option is on, extra debugging
 * facilities such as tracking the file and line number of lock operations
 * are enabled.  Also, mutex locking operations are not inlined to avoid
 * bloat from all the extra debugging code.  We also have to turn on all the
 * calling conventions for this debugging code in modules so that modules can
 * work with both debug and non-debug kernels.
 */
#if (defined(KLD_MODULE) && !defined(KLD_TIED)) || defined(WITNESS) || defined(INVARIANTS) || \
    defined(LOCK_PROFILING) || defined(KTR)
#define	LOCK_DEBUG	1
#else
#define	LOCK_DEBUG	0
#endif

/*
 * In the LOCK_DEBUG case, use the filename and line numbers for debugging
 * operations.  Otherwise, use default values to avoid the unneeded bloat.
 */
#if LOCK_DEBUG > 0
#define LOCK_FILE_LINE_ARG_DEF	, const char *file, int line
#define LOCK_FILE_LINE_ARG	, file, line
#define	LOCK_FILE	__FILE__
#define	LOCK_LINE	__LINE__
#else
#define LOCK_FILE_LINE_ARG_DEF
#define LOCK_FILE_LINE_ARG
#define	LOCK_FILE	NULL
#define	LOCK_LINE	0
#endif

/*
 * Macros for KTR_LOCK tracing.
 *
 * opname  - name of this operation (LOCK/UNLOCK/SLOCK, etc.)
 * lo      - struct lock_object * for this lock
 * flags   - flags passed to the lock operation
 * recurse - this locks recursion level (or 0 if class is not recursable)
 * result  - result of a try lock operation
 * file    - file name
 * line    - line number
 */
#if LOCK_DEBUG > 0
#define	LOCK_LOG_TEST(lo, flags)					\
	(((flags) & LOP_QUIET) == 0 && ((lo)->lo_flags & LO_QUIET) == 0)
#else
#define	LOCK_LOG_TEST(lo, flags)	0
#endif


#define	LOCK_LOG_LOCK(opname, lo, flags, recurse, file, line) do {	\
	if (LOCK_LOG_TEST((lo), (flags)))				\
		CTR6(KTR_LOCK, opname " (%s) %s %p r = %d at %s:%d",	\
		    LOCK_CLASS(lo)->lc_name, (lo)->lo_name,		\
		    (lo), (u_int)(recurse), (file), (line));		\
} while (0)

#define	LOCK_LOG_TRY(opname, lo, flags, result, file, line) do {	\
	if (LOCK_LOG_TEST((lo), (flags)))				\
		CTR6(KTR_LOCK, "TRY_" opname " (%s) %s %p result=%d at %s:%d",\
		    LOCK_CLASS(lo)->lc_name, (lo)->lo_name,		\
		    (lo), (u_int)(result), (file), (line));		\
} while (0)

#define	LOCK_LOG_INIT(lo, flags) do {					\
	if (LOCK_LOG_TEST((lo), (flags)))				\
		CTR4(KTR_LOCK, "%s: %p (%s) %s", __func__, (lo),	\
 		    LOCK_CLASS(lo)->lc_name, (lo)->lo_name);		\
} while (0)

#define	LOCK_LOG_DESTROY(lo, flags)	LOCK_LOG_INIT(lo, flags)

#define	lock_initialized(lo)	((lo)->lo_flags & LO_INITIALIZED)

/*
 * Helpful macros for quickly coming up with assertions with informative
 * panic messages.
 */
#define MPASS(ex)		MPASS4(ex, #ex, __FILE__, __LINE__)
#define MPASS2(ex, what)	MPASS4(ex, what, __FILE__, __LINE__)
#define MPASS3(ex, file, line)	MPASS4(ex, #ex, file, line)
#define MPASS4(ex, what, file, line)					\
	KASSERT((ex), ("Assertion %s failed at %s:%d", what, file, line))

extern struct lock_class lock_class_mtx_sleep;
extern struct lock_class lock_class_mtx_spin;
extern struct lock_class lock_class_sx;
extern struct lock_class lock_class_rw;
extern struct lock_class lock_class_rm;
extern struct lock_class lock_class_rm_sleepable;
extern struct lock_class lock_class_lockmgr;

extern struct lock_class *lock_classes[];

struct lock_delay_config {
	u_int base;
	u_int max;
};

struct lock_delay_arg {
	struct lock_delay_config *config;
	u_int delay;
	u_int spin_cnt;
};

static inline void
lock_delay_arg_init(struct lock_delay_arg *la, struct lock_delay_config *lc)
{
	la->config = lc;
	la->delay = lc->base;
	la->spin_cnt = 0;
}

#define lock_delay_spin(n)	do {	\
	u_int _i;			\
					\
	for (_i = (n); _i > 0; _i--)	\
		cpu_spinwait();		\
} while (0)

#define	LOCK_DELAY_SYSINIT(func) \
	SYSINIT(func##_ld, SI_SUB_LOCK, SI_ORDER_ANY, func, NULL)

#define	LOCK_DELAY_SYSINIT_DEFAULT(lc) \
	SYSINIT(lock_delay_##lc##_ld, SI_SUB_LOCK, SI_ORDER_ANY, \
	    lock_delay_default_init, &lc)

void	lock_init(struct lock_object *, struct lock_class *,
	    const char *, const char *, int);
void	lock_destroy(struct lock_object *);
void	lock_delay(struct lock_delay_arg *);
void	lock_delay_default_init(struct lock_delay_config *);
void	spinlock_enter(void);
void	spinlock_exit(void);
void	witness_init(struct lock_object *, const char *);
void	witness_destroy(struct lock_object *);
int	witness_defineorder(struct lock_object *, struct lock_object *);
void	witness_checkorder(struct lock_object *, int, const char *, int,
	    struct lock_object *);
void	witness_lock(struct lock_object *, int, const char *, int);
void	witness_upgrade(struct lock_object *, int, const char *, int);
void	witness_downgrade(struct lock_object *, int, const char *, int);
void	witness_unlock(struct lock_object *, int, const char *, int);
void	witness_save(struct lock_object *, const char **, int *);
void	witness_restore(struct lock_object *, const char *, int);
int	witness_list_locks(struct lock_list_entry **,
	    int (*)(const char *, ...));
int	witness_warn(int, struct lock_object *, const char *, ...);
void	witness_assert(const struct lock_object *, int, const char *, int);
void	witness_display_spinlock(struct lock_object *, struct thread *,
	    int (*)(const char *, ...));
int	witness_line(struct lock_object *);
void	witness_norelease(struct lock_object *);
void	witness_releaseok(struct lock_object *);
const char *witness_file(struct lock_object *);
void	witness_thread_exit(struct thread *);

#ifdef	WITNESS
int	witness_startup_count(void);
void	witness_startup(void *);

/* Flags for witness_warn(). */
#define	WARN_GIANTOK	0x01	/* Giant is exempt from this check. */
#define	WARN_PANIC	0x02	/* Panic if check fails. */
#define	WARN_SLEEPOK	0x04	/* Sleepable locks are exempt from check. */

#define	WITNESS_INIT(lock, type)					\
	witness_init((lock), (type))

#define WITNESS_DESTROY(lock)						\
	witness_destroy(lock)

#define	WITNESS_CHECKORDER(lock, flags, file, line, interlock)		\
	witness_checkorder((lock), (flags), (file), (line), (interlock))

#define	WITNESS_DEFINEORDER(lock1, lock2)				\
	witness_defineorder((struct lock_object *)(lock1),		\
	    (struct lock_object *)(lock2))

#define	WITNESS_LOCK(lock, flags, file, line)				\
	witness_lock((lock), (flags), (file), (line))

#define	WITNESS_UPGRADE(lock, flags, file, line)			\
	witness_upgrade((lock), (flags), (file), (line))

#define	WITNESS_DOWNGRADE(lock, flags, file, line)			\
	witness_downgrade((lock), (flags), (file), (line))

#define	WITNESS_UNLOCK(lock, flags, file, line)				\
	witness_unlock((lock), (flags), (file), (line))

#define	WITNESS_CHECK(flags, lock, fmt, ...)				\
	witness_warn((flags), (lock), (fmt), ## __VA_ARGS__)

#define	WITNESS_WARN(flags, lock, fmt, ...)				\
	witness_warn((flags), (lock), (fmt), ## __VA_ARGS__)

#define	WITNESS_SAVE_DECL(n)						\
	const char * __CONCAT(n, __wf);					\
	int __CONCAT(n, __wl)

#define	WITNESS_SAVE(lock, n) 						\
	witness_save((lock), &__CONCAT(n, __wf), &__CONCAT(n, __wl))

#define	WITNESS_RESTORE(lock, n) 					\
	witness_restore((lock), __CONCAT(n, __wf), __CONCAT(n, __wl))

#define	WITNESS_NORELEASE(lock)						\
	witness_norelease(&(lock)->lock_object)

#define	WITNESS_RELEASEOK(lock)						\
	witness_releaseok(&(lock)->lock_object)

#define	WITNESS_FILE(lock) 						\
	witness_file(lock)

#define	WITNESS_LINE(lock) 						\
	witness_line(lock)

#else	/* WITNESS */
#define	WITNESS_INIT(lock, type)				(void)0
#define	WITNESS_DESTROY(lock)					(void)0
#define	WITNESS_DEFINEORDER(lock1, lock2)	0
#define	WITNESS_CHECKORDER(lock, flags, file, line, interlock)	(void)0
#define	WITNESS_LOCK(lock, flags, file, line)			(void)0
#define	WITNESS_UPGRADE(lock, flags, file, line)		(void)0
#define	WITNESS_DOWNGRADE(lock, flags, file, line)		(void)0
#define	WITNESS_UNLOCK(lock, flags, file, line)			(void)0
#define	WITNESS_CHECK(flags, lock, fmt, ...)	0
#define	WITNESS_WARN(flags, lock, fmt, ...)			(void)0
#define	WITNESS_SAVE_DECL(n)					(void)0
#define	WITNESS_SAVE(lock, n)					(void)0
#define	WITNESS_RESTORE(lock, n)				(void)0
#define	WITNESS_NORELEASE(lock)					(void)0
#define	WITNESS_RELEASEOK(lock)					(void)0
#define	WITNESS_FILE(lock) ("?")
#define	WITNESS_LINE(lock) (0)
#endif	/* WITNESS */

#endif	/* _KERNEL */
#endif	/* _SYS_LOCK_H_ */
