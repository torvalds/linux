/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2005 Daniel M. Eischen <deischen@freebsd.org>
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * Copyright (c) 1995-1998 John Birrell <jb@cimlogic.com.au>.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _THR_PRIVATE_H
#define _THR_PRIVATE_H

/*
 * Include files.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/cdefs.h>
#include <sys/queue.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <machine/atomic.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <ucontext.h>
#include <sys/thr.h>
#include <pthread.h>

__NULLABILITY_PRAGMA_PUSH

#define	SYM_FB10(sym)			__CONCAT(sym, _fb10)
#define	SYM_FBP10(sym)			__CONCAT(sym, _fbp10)
#define	WEAK_REF(sym, alias)		__weak_reference(sym, alias)
#define	SYM_COMPAT(sym, impl, ver)	__sym_compat(sym, impl, ver)
#define	SYM_DEFAULT(sym, impl, ver)	__sym_default(sym, impl, ver)

#define	FB10_COMPAT(func, sym)				\
	WEAK_REF(func, SYM_FB10(sym));			\
	SYM_COMPAT(sym, SYM_FB10(sym), FBSD_1.0)

#define	FB10_COMPAT_PRIVATE(func, sym)			\
	WEAK_REF(func, SYM_FBP10(sym));			\
	SYM_DEFAULT(sym, SYM_FBP10(sym), FBSDprivate_1.0)

struct pthread;
extern struct pthread	*_thr_initial __hidden;

#include "pthread_md.h"
#include "thr_umtx.h"
#include "thread_db.h"

#ifdef _PTHREAD_FORCED_UNWIND
#define _BSD_SOURCE
#include <unwind.h>
#endif

typedef TAILQ_HEAD(pthreadlist, pthread) pthreadlist;
typedef TAILQ_HEAD(atfork_head, pthread_atfork) atfork_head;
TAILQ_HEAD(mutex_queue, pthread_mutex);

/* Signal to do cancellation */
#define	SIGCANCEL		SIGTHR

/*
 * Kernel fatal error handler macro.
 */
#define PANIC(args...)		_thread_exitf(__FILE__, __LINE__, ##args)

/* Output debug messages like this: */
#define stdout_debug(args...)	_thread_printf(STDOUT_FILENO, ##args)
#define stderr_debug(args...)	_thread_printf(STDERR_FILENO, ##args)

#ifdef _PTHREADS_INVARIANTS
#define THR_ASSERT(cond, msg) do {	\
	if (__predict_false(!(cond)))	\
		PANIC(msg);		\
} while (0)
#else
#define THR_ASSERT(cond, msg)
#endif

#ifdef PIC
# define STATIC_LIB_REQUIRE(name)
#else
# define STATIC_LIB_REQUIRE(name) __asm (".globl " #name)
#endif

#define	TIMESPEC_ADD(dst, src, val)				\
	do { 							\
		(dst)->tv_sec = (src)->tv_sec + (val)->tv_sec;	\
		(dst)->tv_nsec = (src)->tv_nsec + (val)->tv_nsec; \
		if ((dst)->tv_nsec >= 1000000000) {		\
			(dst)->tv_sec++;			\
			(dst)->tv_nsec -= 1000000000;		\
		}						\
	} while (0)

#define	TIMESPEC_SUB(dst, src, val)				\
	do { 							\
		(dst)->tv_sec = (src)->tv_sec - (val)->tv_sec;	\
		(dst)->tv_nsec = (src)->tv_nsec - (val)->tv_nsec; \
		if ((dst)->tv_nsec < 0) {			\
			(dst)->tv_sec--;			\
			(dst)->tv_nsec += 1000000000;		\
		}						\
	} while (0)

/* Magic cookie set for shared pthread locks and cv's pointers */
#define	THR_PSHARED_PTR						\
    ((void *)(uintptr_t)((1ULL << (NBBY * sizeof(long) - 1)) | 1))

/* XXX These values should be same as those defined in pthread.h */
#define	THR_MUTEX_INITIALIZER		((struct pthread_mutex *)NULL)
#define	THR_ADAPTIVE_MUTEX_INITIALIZER	((struct pthread_mutex *)1)
#define	THR_MUTEX_DESTROYED		((struct pthread_mutex *)2)
#define	THR_COND_INITIALIZER		((struct pthread_cond *)NULL)
#define	THR_COND_DESTROYED		((struct pthread_cond *)1)
#define	THR_RWLOCK_INITIALIZER		((struct pthread_rwlock *)NULL)
#define	THR_RWLOCK_DESTROYED		((struct pthread_rwlock *)1)

#define PMUTEX_FLAG_TYPE_MASK	0x0ff
#define PMUTEX_FLAG_PRIVATE	0x100
#define PMUTEX_FLAG_DEFERRED	0x200
#define PMUTEX_TYPE(mtxflags)	((mtxflags) & PMUTEX_FLAG_TYPE_MASK)

#define	PMUTEX_OWNER_ID(m)	((m)->m_lock.m_owner & ~UMUTEX_CONTESTED)

#define MAX_DEFER_WAITERS       50

/*
 * Values for pthread_mutex m_ps indicator.
 */
#define	PMUTEX_INITSTAGE_ALLOC	0
#define	PMUTEX_INITSTAGE_BUSY	1
#define	PMUTEX_INITSTAGE_DONE	2

struct pthread_mutex {
	/*
	 * Lock for accesses to this structure.
	 */
	struct umutex			m_lock;
	int				m_flags;
	int				m_count;
	int				m_spinloops;
	int				m_yieldloops;
	int				m_ps;	/* pshared init stage */
	/*
	 * Link for all mutexes a thread currently owns, of the same
	 * prio type.
	 */
	TAILQ_ENTRY(pthread_mutex)	m_qe;
	/* Link for all private mutexes a thread currently owns. */
	TAILQ_ENTRY(pthread_mutex)	m_pqe;
	struct pthread_mutex		*m_rb_prev;
};

struct pthread_mutex_attr {
	enum pthread_mutextype	m_type;
	int			m_protocol;
	int			m_ceiling;
	int			m_pshared;
	int			m_robust;
};

#define PTHREAD_MUTEXATTR_STATIC_INITIALIZER \
	{ PTHREAD_MUTEX_DEFAULT, PTHREAD_PRIO_NONE, 0, MUTEX_FLAGS_PRIVATE, \
	    PTHREAD_MUTEX_STALLED }

struct pthread_cond {
	__uint32_t	__has_user_waiters;
	struct ucond	kcond;
};

struct pthread_cond_attr {
	int		c_pshared;
	int		c_clockid;
};

struct pthread_barrier {
	struct umutex		b_lock;
	struct ucond		b_cv;
	int64_t			b_cycle;
	int			b_count;
	int			b_waiters;
	int			b_refcount;
	int			b_destroying;
};

struct pthread_barrierattr {
	int		pshared;
};

struct pthread_spinlock {
	struct umutex	s_lock;
};

/*
 * Flags for condition variables.
 */
#define COND_FLAGS_PRIVATE	0x01
#define COND_FLAGS_INITED	0x02
#define COND_FLAGS_BUSY		0x04

/*
 * Cleanup definitions.
 */
struct pthread_cleanup {
	struct pthread_cleanup	*prev;
	void			(*routine)(void *);
	void			*routine_arg;
	int			onheap;
};

#define	THR_CLEANUP_PUSH(td, func, arg) {		\
	struct pthread_cleanup __cup;			\
							\
	__cup.routine = func;				\
	__cup.routine_arg = arg;			\
	__cup.onheap = 0;				\
	__cup.prev = (td)->cleanup;			\
	(td)->cleanup = &__cup;

#define	THR_CLEANUP_POP(td, exec)			\
	(td)->cleanup = __cup.prev;			\
	if ((exec) != 0)				\
		__cup.routine(__cup.routine_arg);	\
}

struct pthread_atfork {
	TAILQ_ENTRY(pthread_atfork) qe;
	void (*prepare)(void);
	void (*parent)(void);
	void (*child)(void);
};

struct pthread_attr {
#define pthread_attr_start_copy	sched_policy
	int	sched_policy;
	int	sched_inherit;
	int	prio;
	int	suspend;
#define	THR_STACK_USER		0x100	/* 0xFF reserved for <pthread.h> */
	int	flags;
	void	*stackaddr_attr;
	size_t	stacksize_attr;
	size_t	guardsize_attr;
#define pthread_attr_end_copy	cpuset
	cpuset_t	*cpuset;
	size_t	cpusetsize;
};

struct wake_addr {
	struct wake_addr *link;
	unsigned int	value;
	char		pad[12];
};

struct sleepqueue {
	TAILQ_HEAD(, pthread)    sq_blocked;
	SLIST_HEAD(, sleepqueue) sq_freeq;
	LIST_ENTRY(sleepqueue)   sq_hash;
	SLIST_ENTRY(sleepqueue)  sq_flink;
	void			 *sq_wchan;
	int			 sq_type;
};

/*
 * Thread creation state attributes.
 */
#define THR_CREATE_RUNNING		0
#define THR_CREATE_SUSPENDED		1

/*
 * Miscellaneous definitions.
 */
#define THR_STACK_DEFAULT		(sizeof(void *) / 4 * 1024 * 1024)

/*
 * Maximum size of initial thread's stack.  This perhaps deserves to be larger
 * than the stacks of other threads, since many applications are likely to run
 * almost entirely on this stack.
 */
#define THR_STACK_INITIAL		(THR_STACK_DEFAULT * 2)

/*
 * Define priorities returned by kernel.
 */
#define THR_MIN_PRIORITY		(_thr_priorities[SCHED_OTHER-1].pri_min)
#define THR_MAX_PRIORITY		(_thr_priorities[SCHED_OTHER-1].pri_max)
#define THR_DEF_PRIORITY		(_thr_priorities[SCHED_OTHER-1].pri_default)

#define THR_MIN_RR_PRIORITY		(_thr_priorities[SCHED_RR-1].pri_min)
#define THR_MAX_RR_PRIORITY		(_thr_priorities[SCHED_RR-1].pri_max)
#define THR_DEF_RR_PRIORITY		(_thr_priorities[SCHED_RR-1].pri_default)

/* XXX The SCHED_FIFO should have same priority range as SCHED_RR */
#define THR_MIN_FIFO_PRIORITY		(_thr_priorities[SCHED_FIFO_1].pri_min)
#define THR_MAX_FIFO_PRIORITY		(_thr_priorities[SCHED_FIFO-1].pri_max)
#define THR_DEF_FIFO_PRIORITY		(_thr_priorities[SCHED_FIFO-1].pri_default)

struct pthread_prio {
	int	pri_min;
	int	pri_max;
	int	pri_default;
};

struct pthread_rwlockattr {
	int		pshared;
};

struct pthread_rwlock {
	struct urwlock 	lock;
	uint32_t	owner;
};

/*
 * Thread states.
 */
enum pthread_state {
	PS_RUNNING,
	PS_DEAD
};

struct pthread_specific_elem {
	const void	*data;
	int		seqno;
};

struct pthread_key {
	volatile int	allocated;
	int		seqno;
	void            (*destructor)(void *);
};

/*
 * lwpid_t is 32bit but kernel thr API exports tid as long type
 * to preserve the ABI for M:N model in very early date (r131431).
 */
#define TID(thread)	((uint32_t) ((thread)->tid))

/*
 * Thread structure.
 */
struct pthread {
#define _pthread_startzero	tid
	/* Kernel thread id. */
	long			tid;
#define	TID_TERMINATED		1

	/*
	 * Lock for accesses to this thread structure.
	 */
	struct umutex		lock;

	/* Internal condition variable cycle number. */
	uint32_t		cycle;

	/* How many low level locks the thread held. */
	int			locklevel;

	/*
	 * Set to non-zero when this thread has entered a critical
	 * region.  We allow for recursive entries into critical regions.
	 */
	int			critical_count;

	/* Signal blocked counter. */
	int			sigblock;

	/* Queue entry for list of all threads. */
	TAILQ_ENTRY(pthread)	tle;	/* link for all threads in process */

	/* Queue entry for GC lists. */
	TAILQ_ENTRY(pthread)	gcle;

	/* Hash queue entry. */
	LIST_ENTRY(pthread)	hle;

	/* Sleep queue entry */
	TAILQ_ENTRY(pthread)    wle;

	/* Threads reference count. */
	int			refcount;

	/*
	 * Thread start routine, argument, stack pointer and thread
	 * attributes.
	 */
	void			*(*start_routine)(void *);
	void			*arg;
	struct pthread_attr	attr;

#define	SHOULD_CANCEL(thr)					\
	((thr)->cancel_pending && (thr)->cancel_enable &&	\
	 (thr)->no_cancel == 0)

	/* Cancellation is enabled */
	int			cancel_enable;

	/* Cancellation request is pending */
	int			cancel_pending;

	/* Thread is at cancellation point */
	int			cancel_point;

	/* Cancellation is temporarily disabled */
	int			no_cancel;

	/* Asynchronouse cancellation is enabled */
	int			cancel_async;

	/* Cancellation is in progress */
	int			cancelling;

	/* Thread temporary signal mask. */
	sigset_t		sigmask;

	/* Thread should unblock SIGCANCEL. */
	int			unblock_sigcancel;

	/* In sigsuspend state */
	int			in_sigsuspend;

	/* deferred signal info	*/
	siginfo_t		deferred_siginfo;

	/* signal mask to restore. */
	sigset_t		deferred_sigmask;

	/* the sigaction should be used for deferred signal. */
	struct sigaction	deferred_sigact;

	/* deferred signal delivery is performed, do not reenter. */
	int			deferred_run;

	/* Force new thread to exit. */
	int			force_exit;

	/* Thread state: */
	enum pthread_state 	state;

	/*
	 * Error variable used instead of errno. The function __error()
	 * returns a pointer to this. 
	 */
	int			error;

	/*
	 * The joiner is the thread that is joining to this thread.  The
	 * join status keeps track of a join operation to another thread.
	 */
	struct pthread		*joiner;

	/* Miscellaneous flags; only set with scheduling lock held. */
	int			flags;
#define THR_FLAGS_PRIVATE	0x0001
#define	THR_FLAGS_NEED_SUSPEND	0x0002	/* thread should be suspended */
#define	THR_FLAGS_SUSPENDED	0x0004	/* thread is suspended */
#define	THR_FLAGS_DETACHED	0x0008	/* thread is detached */

	/* Thread list flags; only set with thread list lock held. */
	int			tlflags;
#define	TLFLAGS_GC_SAFE		0x0001	/* thread safe for cleaning */
#define	TLFLAGS_IN_TDLIST	0x0002	/* thread in all thread list */
#define	TLFLAGS_IN_GCLIST	0x0004	/* thread in gc list */

	/*
	 * Queues of the owned mutexes.  Private queue must have index
	 * + 1 of the corresponding full queue.
	 */
#define	TMQ_NORM		0	/* NORMAL or PRIO_INHERIT normal */
#define	TMQ_NORM_PRIV		1	/* NORMAL or PRIO_INHERIT normal priv */
#define	TMQ_NORM_PP		2	/* PRIO_PROTECT normal mutexes */
#define	TMQ_NORM_PP_PRIV	3	/* PRIO_PROTECT normal priv */
#define	TMQ_ROBUST_PP		4	/* PRIO_PROTECT robust mutexes */
#define	TMQ_ROBUST_PP_PRIV	5	/* PRIO_PROTECT robust priv */	
#define	TMQ_NITEMS		6
	struct mutex_queue	mq[TMQ_NITEMS];

	void				*ret;
	struct pthread_specific_elem	*specific;
	int				specific_data_count;

	/* Number rwlocks rdlocks held. */
	int			rdlock_count;

	/*
	 * Current locks bitmap for rtld. */
	int			rtld_bits;

	/* Thread control block */
	struct tcb		*tcb;

	/* Cleanup handlers Link List */
	struct pthread_cleanup	*cleanup;

#ifdef _PTHREAD_FORCED_UNWIND
	struct _Unwind_Exception	ex;
	void			*unwind_stackend;
	int			unwind_disabled;
#endif

	/*
	 * Magic value to help recognize a valid thread structure
	 * from an invalid one:
	 */
#define	THR_MAGIC		((u_int32_t) 0xd09ba115)
	u_int32_t		magic;

	/* Enable event reporting */
	int			report_events;

	/* Event mask */
	int			event_mask;

	/* Event */
	td_event_msg_t		event_buf;

	/* Wait channel */
	void			*wchan;

	/* Referenced mutex. */
	struct pthread_mutex	*mutex_obj;

	/* Thread will sleep. */
	int			will_sleep;

	/* Number of threads deferred. */
	int			nwaiter_defer;

	int			robust_inited;
	uintptr_t		robust_list;
	uintptr_t		priv_robust_list;
	uintptr_t		inact_mtx;

	/* Deferred threads from pthread_cond_signal. */
	unsigned int 		*defer_waiters[MAX_DEFER_WAITERS];
#define _pthread_endzero	wake_addr

	struct wake_addr	*wake_addr;
#define WAKE_ADDR(td)           ((td)->wake_addr)

	/* Sleep queue */
	struct	sleepqueue	*sleepqueue;

	/* pthread_set/get_name_np */
	char			*name;
};

#define THR_SHOULD_GC(thrd) 						\
	((thrd)->refcount == 0 && (thrd)->state == PS_DEAD &&		\
	 ((thrd)->flags & THR_FLAGS_DETACHED) != 0)

#define	THR_IN_CRITICAL(thrd)				\
	(((thrd)->locklevel > 0) ||			\
	((thrd)->critical_count > 0))

#define	THR_CRITICAL_ENTER(thrd)			\
	(thrd)->critical_count++

#define	THR_CRITICAL_LEAVE(thrd)			\
	do {						\
		(thrd)->critical_count--;		\
		_thr_ast(thrd);				\
	} while (0)

#define THR_UMUTEX_TRYLOCK(thrd, lck)			\
	_thr_umutex_trylock((lck), TID(thrd))

#define	THR_UMUTEX_LOCK(thrd, lck)			\
	_thr_umutex_lock((lck), TID(thrd))

#define	THR_UMUTEX_TIMEDLOCK(thrd, lck, timo)		\
	_thr_umutex_timedlock((lck), TID(thrd), (timo))

#define	THR_UMUTEX_UNLOCK(thrd, lck)			\
	_thr_umutex_unlock((lck), TID(thrd))

#define	THR_LOCK_ACQUIRE(thrd, lck)			\
do {							\
	(thrd)->locklevel++;				\
	_thr_umutex_lock(lck, TID(thrd));		\
} while (0)

#define	THR_LOCK_ACQUIRE_SPIN(thrd, lck)		\
do {							\
	(thrd)->locklevel++;				\
	_thr_umutex_lock_spin(lck, TID(thrd));		\
} while (0)

#ifdef	_PTHREADS_INVARIANTS
#define	THR_ASSERT_LOCKLEVEL(thrd)			\
do {							\
	if (__predict_false((thrd)->locklevel <= 0))	\
		_thr_assert_lock_level();		\
} while (0)
#else
#define THR_ASSERT_LOCKLEVEL(thrd)
#endif

#define	THR_LOCK_RELEASE(thrd, lck)			\
do {							\
	THR_ASSERT_LOCKLEVEL(thrd);			\
	_thr_umutex_unlock((lck), TID(thrd));		\
	(thrd)->locklevel--;				\
	_thr_ast(thrd);					\
} while (0)

#define	THR_LOCK(curthrd)		THR_LOCK_ACQUIRE(curthrd, &(curthrd)->lock)
#define	THR_UNLOCK(curthrd)		THR_LOCK_RELEASE(curthrd, &(curthrd)->lock)
#define	THR_THREAD_LOCK(curthrd, thr)	THR_LOCK_ACQUIRE(curthrd, &(thr)->lock)
#define	THR_THREAD_UNLOCK(curthrd, thr)	THR_LOCK_RELEASE(curthrd, &(thr)->lock)

#define	THREAD_LIST_RDLOCK(curthrd)				\
do {								\
	(curthrd)->locklevel++;					\
	_thr_rwl_rdlock(&_thr_list_lock);			\
} while (0)

#define	THREAD_LIST_WRLOCK(curthrd)				\
do {								\
	(curthrd)->locklevel++;					\
	_thr_rwl_wrlock(&_thr_list_lock);			\
} while (0)

#define	THREAD_LIST_UNLOCK(curthrd)				\
do {								\
	_thr_rwl_unlock(&_thr_list_lock);			\
	(curthrd)->locklevel--;					\
	_thr_ast(curthrd);					\
} while (0)

/*
 * Macros to insert/remove threads to the all thread list and
 * the gc list.
 */
#define	THR_LIST_ADD(thrd) do {					\
	if (((thrd)->tlflags & TLFLAGS_IN_TDLIST) == 0) {	\
		TAILQ_INSERT_HEAD(&_thread_list, thrd, tle);	\
		_thr_hash_add(thrd);				\
		(thrd)->tlflags |= TLFLAGS_IN_TDLIST;		\
	}							\
} while (0)
#define	THR_LIST_REMOVE(thrd) do {				\
	if (((thrd)->tlflags & TLFLAGS_IN_TDLIST) != 0) {	\
		TAILQ_REMOVE(&_thread_list, thrd, tle);		\
		_thr_hash_remove(thrd);				\
		(thrd)->tlflags &= ~TLFLAGS_IN_TDLIST;		\
	}							\
} while (0)
#define	THR_GCLIST_ADD(thrd) do {				\
	if (((thrd)->tlflags & TLFLAGS_IN_GCLIST) == 0) {	\
		TAILQ_INSERT_HEAD(&_thread_gc_list, thrd, gcle);\
		(thrd)->tlflags |= TLFLAGS_IN_GCLIST;		\
		_gc_count++;					\
	}							\
} while (0)
#define	THR_GCLIST_REMOVE(thrd) do {				\
	if (((thrd)->tlflags & TLFLAGS_IN_GCLIST) != 0) {	\
		TAILQ_REMOVE(&_thread_gc_list, thrd, gcle);	\
		(thrd)->tlflags &= ~TLFLAGS_IN_GCLIST;		\
		_gc_count--;					\
	}							\
} while (0)

#define THR_REF_ADD(curthread, pthread) {			\
	THR_CRITICAL_ENTER(curthread);				\
	pthread->refcount++;					\
} while (0)

#define THR_REF_DEL(curthread, pthread) {			\
	pthread->refcount--;					\
	THR_CRITICAL_LEAVE(curthread);				\
} while (0)

#define GC_NEEDED()	(_gc_count >= 5)

#define SHOULD_REPORT_EVENT(curthr, e)			\
	(curthr->report_events && 			\
	 (((curthr)->event_mask | _thread_event_mask ) & e) != 0)

#ifndef __LIBC_ISTHREADED_DECLARED
#define __LIBC_ISTHREADED_DECLARED
extern int __isthreaded;
#endif

/*
 * Global variables for the pthread kernel.
 */

extern char		*_usrstack __hidden;

/* For debugger */
extern int		_libthr_debug;
extern int		_thread_event_mask;
extern struct pthread	*_thread_last_event;
/* Used in symbol lookup of libthread_db */
extern struct pthread_key	_thread_keytable[];

/* List of all threads: */
extern pthreadlist	_thread_list;

/* List of threads needing GC: */
extern pthreadlist	_thread_gc_list __hidden;

extern int		_thread_active_threads;
extern atfork_head	_thr_atfork_list __hidden;
extern struct urwlock	_thr_atfork_lock __hidden;

/* Default thread attributes: */
extern struct pthread_attr _pthread_attr_default __hidden;

/* Default mutex attributes: */
extern struct pthread_mutex_attr _pthread_mutexattr_default __hidden;
extern struct pthread_mutex_attr _pthread_mutexattr_adaptive_default __hidden;

/* Default condition variable attributes: */
extern struct pthread_cond_attr _pthread_condattr_default __hidden;

extern struct pthread_prio _thr_priorities[] __hidden;

extern int	_thr_is_smp __hidden;

extern size_t	_thr_guard_default __hidden;
extern size_t	_thr_stack_default __hidden;
extern size_t	_thr_stack_initial __hidden;
extern int	_thr_page_size __hidden;
extern int	_thr_spinloops __hidden;
extern int	_thr_yieldloops __hidden;
extern int	_thr_queuefifo __hidden;

/* Garbage thread count. */
extern int	_gc_count __hidden;

extern struct umutex	_mutex_static_lock __hidden;
extern struct umutex	_cond_static_lock __hidden;
extern struct umutex	_rwlock_static_lock __hidden;
extern struct umutex	_keytable_lock __hidden;
extern struct urwlock	_thr_list_lock __hidden;
extern struct umutex	_thr_event_lock __hidden;
extern struct umutex	_suspend_all_lock __hidden;
extern int		_suspend_all_waiters __hidden;
extern int		_suspend_all_cycle __hidden;
extern struct pthread	*_single_thread __hidden;

/*
 * Function prototype definitions.
 */
__BEGIN_DECLS
void	_thr_setthreaded(int) __hidden;
int	_mutex_cv_lock(struct pthread_mutex *, int, bool) __hidden;
int	_mutex_cv_unlock(struct pthread_mutex *, int *, int *) __hidden;
int     _mutex_cv_attach(struct pthread_mutex *, int) __hidden;
int     _mutex_cv_detach(struct pthread_mutex *, int *) __hidden;
int     _mutex_owned(struct pthread *, const struct pthread_mutex *) __hidden;
int	_mutex_reinit(pthread_mutex_t *) __hidden;
void	_mutex_fork(struct pthread *curthread) __hidden;
int	_mutex_enter_robust(struct pthread *curthread, struct pthread_mutex *m)
	    __hidden;
void	_mutex_leave_robust(struct pthread *curthread, struct pthread_mutex *m)
	    __hidden;
void	_libpthread_init(struct pthread *) __hidden;
struct pthread *_thr_alloc(struct pthread *) __hidden;
void	_thread_exit(const char *, int, const char *) __hidden __dead2;
void	_thread_exitf(const char *, int, const char *, ...) __hidden __dead2
	    __printflike(3, 4);
int	_thr_ref_add(struct pthread *, struct pthread *, int) __hidden;
void	_thr_ref_delete(struct pthread *, struct pthread *) __hidden;
void	_thr_ref_delete_unlocked(struct pthread *, struct pthread *) __hidden;
int	_thr_find_thread(struct pthread *, struct pthread *, int) __hidden;
void	_thr_rtld_init(void) __hidden;
void	_thr_rtld_postfork_child(void) __hidden;
int	_thr_stack_alloc(struct pthread_attr *) __hidden;
void	_thr_stack_free(struct pthread_attr *) __hidden;
void	_thr_free(struct pthread *, struct pthread *) __hidden;
void	_thr_gc(struct pthread *) __hidden;
void    _thread_cleanupspecific(void) __hidden;
void	_thread_printf(int, const char *, ...) __hidden __printflike(2, 3);
void	_thread_vprintf(int, const char *, va_list) __hidden;
void	_thr_spinlock_init(void) __hidden;
void	_thr_cancel_enter(struct pthread *) __hidden;
void	_thr_cancel_enter2(struct pthread *, int) __hidden;
void	_thr_cancel_leave(struct pthread *, int) __hidden;
void	_thr_testcancel(struct pthread *) __hidden;
void	_thr_signal_block(struct pthread *) __hidden;
void	_thr_signal_unblock(struct pthread *) __hidden;
void	_thr_signal_init(int) __hidden;
void	_thr_signal_deinit(void) __hidden;
int	_thr_send_sig(struct pthread *, int sig) __hidden;
void	_thr_list_init(void) __hidden;
void	_thr_hash_add(struct pthread *) __hidden;
void	_thr_hash_remove(struct pthread *) __hidden;
struct pthread *_thr_hash_find(struct pthread *) __hidden;
void	_thr_link(struct pthread *, struct pthread *) __hidden;
void	_thr_unlink(struct pthread *, struct pthread *) __hidden;
void	_thr_assert_lock_level(void) __hidden __dead2;
void	_thr_ast(struct pthread *) __hidden;
void	_thr_report_creation(struct pthread *curthread,
	    struct pthread *newthread) __hidden;
void	_thr_report_death(struct pthread *curthread) __hidden;
int	_thr_getscheduler(lwpid_t, int *, struct sched_param *) __hidden;
int	_thr_setscheduler(lwpid_t, int, const struct sched_param *) __hidden;
void	_thr_signal_prefork(void) __hidden;
void	_thr_signal_postfork(void) __hidden;
void	_thr_signal_postfork_child(void) __hidden;
void	_thr_suspend_all_lock(struct pthread *) __hidden;
void	_thr_suspend_all_unlock(struct pthread *) __hidden;
void	_thr_try_gc(struct pthread *, struct pthread *) __hidden;
int	_rtp_to_schedparam(const struct rtprio *rtp, int *policy,
		struct sched_param *param) __hidden;
int	_schedparam_to_rtp(int policy, const struct sched_param *param,
		struct rtprio *rtp) __hidden;
void	_thread_bp_create(void);
void	_thread_bp_death(void);
int	_sched_yield(void);

void	_pthread_cleanup_push(void (*)(void *), void *);
void	_pthread_cleanup_pop(int);
void	_pthread_exit_mask(void *status, sigset_t *mask) __dead2 __hidden;
#ifndef _LIBC_PRIVATE_H_
void	_pthread_cancel_enter(int maycancel);
void 	_pthread_cancel_leave(int maycancel);
#endif
int	_pthread_mutex_consistent(pthread_mutex_t * _Nonnull);
int	_pthread_mutexattr_getrobust(pthread_mutexattr_t * _Nonnull __restrict,
	    int * _Nonnull __restrict);
int	_pthread_mutexattr_setrobust(pthread_mutexattr_t * _Nonnull, int);

/* #include <fcntl.h> */
#ifdef  _SYS_FCNTL_H_
#ifndef _LIBC_PRIVATE_H_
int     __sys_fcntl(int, int, ...);
int     __sys_openat(int, const char *, int, ...);
#endif /* _LIBC_PRIVATE_H_ */
#endif /* _SYS_FCNTL_H_ */

/* #include <signal.h> */
#ifdef _SIGNAL_H_
#ifndef _LIBC_PRIVATE_H_
int     __sys_sigaction(int, const struct sigaction *, struct sigaction *);
int     __sys_sigprocmask(int, const sigset_t *, sigset_t *);
int     __sys_sigsuspend(const sigset_t *);
int	__sys_sigtimedwait(const sigset_t *, siginfo_t *,
		const struct timespec *);
int	__sys_sigwait(const sigset_t *, int *);
int	__sys_sigwaitinfo(const sigset_t *set, siginfo_t *info);
#endif /* _LIBC_PRIVATE_H_ */
#endif /* _SYS_FCNTL_H_ */

/* #include <time.h> */
#ifdef	_TIME_H_
#ifndef _LIBC_PRIVATE_H_
int	__sys_clock_nanosleep(clockid_t, int, const struct timespec *,
	    struct timespec *);
int	__sys_nanosleep(const struct timespec *, struct timespec *);
#endif /* _LIBC_PRIVATE_H_ */
#endif /* _SYS_FCNTL_H_ */

/* #include <sys/ucontext.h> */
#ifdef _SYS_UCONTEXT_H_
#ifndef _LIBC_PRIVATE_H_
int	__sys_setcontext(const ucontext_t *ucp);
int	__sys_swapcontext(ucontext_t *oucp, const ucontext_t *ucp);
#endif /* _LIBC_PRIVATE_H_ */
#endif /* _SYS_FCNTL_H_ */

/* #include <unistd.h> */
#ifdef  _UNISTD_H_
#ifndef _LIBC_PRIVATE_H_
int     __sys_close(int);
int	__sys_fork(void);
ssize_t __sys_read(int, void *, size_t);
#endif /* _LIBC_PRIVATE_H_ */
#endif /* _SYS_FCNTL_H_ */

static inline int
_thr_isthreaded(void)
{
	return (__isthreaded != 0);
}

static inline int
_thr_is_inited(void)
{
	return (_thr_initial != NULL);
}

static inline void
_thr_check_init(void)
{
	if (_thr_initial == NULL)
		_libpthread_init(NULL);
}

struct wake_addr *_thr_alloc_wake_addr(void);
void	_thr_release_wake_addr(struct wake_addr *);
int	_thr_sleep(struct pthread *, int, const struct timespec *);

void _thr_wake_addr_init(void) __hidden;

static inline void
_thr_clear_wake(struct pthread *td)
{
	td->wake_addr->value = 0;
}

static inline int
_thr_is_woken(struct pthread *td)
{
	return td->wake_addr->value != 0;
}

static inline void
_thr_set_wake(unsigned int *waddr)
{
	*waddr = 1;
	_thr_umtx_wake(waddr, INT_MAX, 0);
}

void _thr_wake_all(unsigned int *waddrs[], int) __hidden;

static inline struct pthread *
_sleepq_first(struct sleepqueue *sq)
{
	return TAILQ_FIRST(&sq->sq_blocked);
}

void	_sleepq_init(void) __hidden;
struct sleepqueue *_sleepq_alloc(void) __hidden;
void	_sleepq_free(struct sleepqueue *) __hidden;
void	_sleepq_lock(void *) __hidden;
void	_sleepq_unlock(void *) __hidden;
struct sleepqueue *_sleepq_lookup(void *) __hidden;
void	_sleepq_add(void *, struct pthread *) __hidden;
int	_sleepq_remove(struct sleepqueue *, struct pthread *) __hidden;
void	_sleepq_drop(struct sleepqueue *,
		void (*cb)(struct pthread *, void *arg), void *) __hidden;

int	_pthread_mutex_init_calloc_cb(pthread_mutex_t *mutex,
	    void *(calloc_cb)(size_t, size_t));

struct dl_phdr_info;
void __pthread_cxa_finalize(struct dl_phdr_info *phdr_info);
void _thr_tsd_unload(struct dl_phdr_info *phdr_info) __hidden;
void _thr_sigact_unload(struct dl_phdr_info *phdr_info) __hidden;
void _thr_stack_fix_protection(struct pthread *thrd);

int *__error_threaded(void) __hidden;
void __thr_interpose_libc(void) __hidden;
pid_t __thr_fork(void);
int __thr_setcontext(const ucontext_t *ucp);
int __thr_sigaction(int sig, const struct sigaction *act,
    struct sigaction *oact) __hidden;
int __thr_sigprocmask(int how, const sigset_t *set, sigset_t *oset);
int __thr_sigsuspend(const sigset_t * set);
int __thr_sigtimedwait(const sigset_t *set, siginfo_t *info,
    const struct timespec * timeout);
int __thr_sigwait(const sigset_t *set, int *sig);
int __thr_sigwaitinfo(const sigset_t *set, siginfo_t *info);
int __thr_swapcontext(ucontext_t *oucp, const ucontext_t *ucp);

void __thr_map_stacks_exec(void);

struct _spinlock;
void __thr_spinunlock(struct _spinlock *lck);
void __thr_spinlock(struct _spinlock *lck);

struct tcb *_tcb_ctor(struct pthread *, int);
void	_tcb_dtor(struct tcb *);

void __thr_pshared_init(void) __hidden;
void *__thr_pshared_offpage(void *key, int doalloc) __hidden;
void __thr_pshared_destroy(void *key) __hidden;
void __thr_pshared_atfork_pre(void) __hidden;
void __thr_pshared_atfork_post(void) __hidden;

void *__thr_calloc(size_t num, size_t size);
void __thr_free(void *cp);
void *__thr_malloc(size_t nbytes);
void *__thr_realloc(void *cp, size_t nbytes);
void __thr_malloc_init(void);
void __thr_malloc_prefork(struct pthread *curthread);
void __thr_malloc_postfork(struct pthread *curthread);

__END_DECLS
__NULLABILITY_PRAGMA_POP

#endif  /* !_THR_PRIVATE_H */
