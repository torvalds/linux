/*	$OpenBSD: pthread.h,v 1.4 2018/03/05 01:15:26 deraadt Exp $	*/

/*
 * Copyright (c) 1993, 1994 by Chris Provenzano, proven@mit.edu
 * Copyright (c) 1995-1998 by John Birrell <jb@cimlogic.com.au>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by Chris Provenzano.
 * 4. The name of Chris Provenzano may not be used to endorse or promote 
 *	  products derived from this software without specific prior written
 *	  permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CHRIS PROVENZANO ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CHRIS PROVENZANO BE LIABLE FOR ANY 
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 *
 * $FreeBSD: pthread.h,v 1.13 1999/07/31 08:36:07 rse Exp $
 */
#ifndef _PTHREAD_H_
#define _PTHREAD_H_

/*
 * Header files.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <limits.h>
#include <sched.h>

/*
 * Run-time invariant values:
 */
#define PTHREAD_DESTRUCTOR_ITERATIONS		4
#define PTHREAD_KEYS_MAX			256
#define PTHREAD_STACK_MIN			(1U << _MAX_PAGE_SHIFT)
#define PTHREAD_THREADS_MAX			ULONG_MAX

/*
 * Flags for threads and thread attributes.
 */
#define PTHREAD_DETACHED            0x1
#define PTHREAD_SCOPE_SYSTEM        0x2
#define PTHREAD_INHERIT_SCHED       0x4
#define PTHREAD_NOFLOAT             0x8

#define PTHREAD_CREATE_DETACHED     PTHREAD_DETACHED
#define PTHREAD_CREATE_JOINABLE     0
#define PTHREAD_SCOPE_PROCESS       0
#define PTHREAD_EXPLICIT_SCHED      0

/*
 * Flags for read/write lock attributes
 */
#define PTHREAD_PROCESS_PRIVATE     0
#define PTHREAD_PROCESS_SHARED      1	

/*
 * Flags for cancelling threads
 */
#define PTHREAD_CANCEL_ENABLE		0
#define PTHREAD_CANCEL_DISABLE		1
#define PTHREAD_CANCEL_DEFERRED		0
#define PTHREAD_CANCEL_ASYNCHRONOUS	2
#define PTHREAD_CANCELED		((void *) 1)

/*
 * Barrier flags
 */
#define PTHREAD_BARRIER_SERIAL_THREAD -1

/*
 * Forward structure definitions.
 *
 * These are mostly opaque to the user.
 */
struct pthread;
struct pthread_attr;
struct pthread_cond;
struct pthread_cond_attr;
struct pthread_mutex;
struct pthread_mutex_attr;
struct pthread_once;
struct pthread_rwlock;
struct pthread_rwlockattr;

/*
 * Primitive system data type definitions required by P1003.1c.
 *
 * Note that P1003.1c specifies that there are no defined comparison
 * or assignment operators for the types pthread_attr_t, pthread_cond_t,
 * pthread_condattr_t, pthread_mutex_t, pthread_mutexattr_t.
 */
typedef struct	pthread			*pthread_t;
typedef struct	pthread_attr		*pthread_attr_t;
typedef volatile struct pthread_mutex	*pthread_mutex_t;
typedef struct	pthread_mutex_attr	*pthread_mutexattr_t;
typedef struct	pthread_cond		*pthread_cond_t;
typedef struct	pthread_cond_attr	*pthread_condattr_t;
typedef int				pthread_key_t;
typedef struct	pthread_once		pthread_once_t;
typedef struct	pthread_rwlock		*pthread_rwlock_t;
typedef struct	pthread_rwlockattr	*pthread_rwlockattr_t;
typedef struct	pthread_barrier		*pthread_barrier_t;
typedef struct	pthread_barrierattr	*pthread_barrierattr_t;
typedef struct	pthread_spinlock	*pthread_spinlock_t;

/*
 * Additional type definitions:
 *
 * Note that P1003.1c reserves the prefixes pthread_ and PTHREAD_ for
 * use in header symbols.
 */
typedef void	*pthread_addr_t;
typedef void	*(*pthread_startroutine_t)(void *);

/*
 * Once definitions.
 */
struct pthread_once {
	int		state;
	pthread_mutex_t	mutex;
};

/*
 * Flags for once initialization.
 */
#define PTHREAD_NEEDS_INIT  0
#define PTHREAD_DONE_INIT   1

/*
 * Static once initialization values. 
 */
#define PTHREAD_ONCE_INIT   { PTHREAD_NEEDS_INIT, PTHREAD_MUTEX_INITIALIZER }

/*
 * Static initialization values. 
 */
#define PTHREAD_MUTEX_INITIALIZER	NULL
#define PTHREAD_COND_INITIALIZER	NULL
#define PTHREAD_RWLOCK_INITIALIZER	NULL

#define PTHREAD_PRIO_NONE	0
#define PTHREAD_PRIO_INHERIT	1
#define PTHREAD_PRIO_PROTECT	2

/*
 * Mutex types.
 */
enum pthread_mutextype {
	PTHREAD_MUTEX_ERRORCHECK	= 1,	/* Error checking mutex */
	PTHREAD_MUTEX_RECURSIVE		= 2,	/* Recursive mutex */
	PTHREAD_MUTEX_NORMAL		= 3,	/* No error checking */
	PTHREAD_MUTEX_STRICT_NP		= 4,	/* Strict error checking */
	PTHREAD_MUTEX_TYPE_MAX
};

#define PTHREAD_MUTEX_ERRORCHECK	PTHREAD_MUTEX_ERRORCHECK
#define PTHREAD_MUTEX_RECURSIVE		PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_NORMAL		PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_STRICT_NP		PTHREAD_MUTEX_STRICT_NP
#define PTHREAD_MUTEX_DEFAULT		PTHREAD_MUTEX_STRICT_NP

/*
 * Thread function prototype definitions:
 */
__BEGIN_DECLS
int		pthread_atfork(void (*)(void), void (*)(void), void (*)(void));
int		pthread_attr_destroy(pthread_attr_t *);
int		pthread_attr_getstack(const pthread_attr_t *,
			void **, size_t *);
int		pthread_attr_getstacksize(const pthread_attr_t *, size_t *);
int		pthread_attr_getstackaddr(const pthread_attr_t *, void **);
int		pthread_attr_getguardsize(const pthread_attr_t *, size_t *);
int		pthread_attr_getdetachstate(const pthread_attr_t *, int *);
int		pthread_attr_init(pthread_attr_t *);
int		pthread_attr_setstacksize(pthread_attr_t *, size_t);
int		pthread_attr_setstack(pthread_attr_t *, void *, size_t);
int		pthread_attr_setstackaddr(pthread_attr_t *, void *);
int		pthread_attr_setguardsize(pthread_attr_t *, size_t);
int		pthread_attr_setdetachstate(pthread_attr_t *, int);
void		pthread_cleanup_pop(int);
void		pthread_cleanup_push(void (*) (void *), void *routine_arg);
int		pthread_condattr_destroy(pthread_condattr_t *);
int		pthread_condattr_init(pthread_condattr_t *);

int		pthread_cond_broadcast(pthread_cond_t *);
int		pthread_cond_destroy(pthread_cond_t *);
int		pthread_cond_init(pthread_cond_t *,
			const pthread_condattr_t *);
int		pthread_cond_signal(pthread_cond_t *);
int		pthread_cond_timedwait(pthread_cond_t *,
			pthread_mutex_t *, const struct timespec *);
int		pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int		pthread_create(pthread_t *, const pthread_attr_t *,
			void *(*) (void *), void *);
int		pthread_detach(pthread_t);
int		pthread_equal(pthread_t, pthread_t);
__dead void	pthread_exit(void *);
void		*pthread_getspecific(pthread_key_t);
int		pthread_join(pthread_t, void **);
int		pthread_key_create(pthread_key_t *,
			void (*) (void *));
int		pthread_key_delete(pthread_key_t);
int		pthread_kill(pthread_t, int);
int		pthread_mutexattr_init(pthread_mutexattr_t *);
int		pthread_mutexattr_destroy(pthread_mutexattr_t *);
int		pthread_mutexattr_gettype(pthread_mutexattr_t *, int *);
int		pthread_mutexattr_settype(pthread_mutexattr_t *, int);
int		pthread_mutex_destroy(pthread_mutex_t *);
int		pthread_mutex_init(pthread_mutex_t *,
			const pthread_mutexattr_t *);
int		pthread_mutex_lock(pthread_mutex_t *);
int		pthread_mutex_timedlock(pthread_mutex_t *,
		    const struct timespec *);
int		pthread_mutex_trylock(pthread_mutex_t *);
int		pthread_mutex_unlock(pthread_mutex_t *);
int		pthread_once(pthread_once_t *, void (*) (void));
int		pthread_rwlock_destroy(pthread_rwlock_t *);
int		pthread_rwlock_init(pthread_rwlock_t *,
			const pthread_rwlockattr_t *);
int		pthread_rwlock_rdlock(pthread_rwlock_t *);
int		pthread_rwlock_timedrdlock(pthread_rwlock_t *,
			const struct timespec *);
int		pthread_rwlock_timedwrlock(pthread_rwlock_t *,
			const struct timespec *);
int		pthread_rwlock_tryrdlock(pthread_rwlock_t *);
int		pthread_rwlock_trywrlock(pthread_rwlock_t *);
int		pthread_rwlock_unlock(pthread_rwlock_t *);
int		pthread_rwlock_wrlock(pthread_rwlock_t *);
int		pthread_rwlockattr_init(pthread_rwlockattr_t *);
int		pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *,
			int *);
int		pthread_rwlockattr_setpshared(pthread_rwlockattr_t *, int);
int		pthread_rwlockattr_destroy(pthread_rwlockattr_t *);
pthread_t	pthread_self(void);
int		pthread_setspecific(pthread_key_t, const void *);

int		pthread_cancel(pthread_t);
int		pthread_setcancelstate(int, int *);
int		pthread_setcanceltype(int, int *);
void		pthread_testcancel(void);

int		pthread_getprio(pthread_t);
int		pthread_setprio(pthread_t, int);
void		pthread_yield(void);

int		pthread_mutexattr_getprioceiling(pthread_mutexattr_t *,
			int *);
int		pthread_mutexattr_setprioceiling(pthread_mutexattr_t *,
			int);
int		pthread_mutex_getprioceiling(pthread_mutex_t *, int *);
int		pthread_mutex_setprioceiling(pthread_mutex_t *, int, int *);

int		pthread_mutexattr_getprotocol(pthread_mutexattr_t *, int *);
int		pthread_mutexattr_setprotocol(pthread_mutexattr_t *, int);

int		pthread_condattr_getclock(const pthread_condattr_t *,
		    clockid_t *);
int		pthread_condattr_setclock(pthread_condattr_t *, clockid_t);

int		pthread_attr_getinheritsched(const pthread_attr_t *, int *);
int		pthread_attr_getschedparam(const pthread_attr_t *,
			struct sched_param *);
int		pthread_attr_getschedpolicy(const pthread_attr_t *, int *);
int		pthread_attr_getscope(const pthread_attr_t *, int *);
int		pthread_attr_setinheritsched(pthread_attr_t *, int);
int		pthread_attr_setschedparam(pthread_attr_t *,
			const struct sched_param *);
int		pthread_attr_setschedpolicy(pthread_attr_t *, int);
int		pthread_attr_setscope(pthread_attr_t *, int);
int		pthread_getschedparam(pthread_t pthread, int *,
			struct sched_param *);
int		pthread_setschedparam(pthread_t, int,
			const struct sched_param *);
int		pthread_getconcurrency(void);
int		pthread_setconcurrency(int);
int		pthread_barrier_init(pthread_barrier_t *,
		    pthread_barrierattr_t *, unsigned int);
int		pthread_barrier_destroy(pthread_barrier_t *);
int		pthread_barrier_wait(pthread_barrier_t *);
int		pthread_barrierattr_init(pthread_barrierattr_t *);
int		pthread_barrierattr_destroy(pthread_barrierattr_t *);
int		pthread_barrierattr_getpshared(pthread_barrierattr_t *, int *);
int		pthread_barrierattr_setpshared(pthread_barrierattr_t *, int);
int		pthread_spin_init(pthread_spinlock_t *, int);
int		pthread_spin_destroy(pthread_spinlock_t *);
int		pthread_spin_trylock(pthread_spinlock_t *);
int		pthread_spin_lock(pthread_spinlock_t *);
int		pthread_spin_unlock(pthread_spinlock_t *);

#if __POSIX_VISIBLE >= 200112
int		pthread_getcpuclockid(pthread_t, clockid_t *);
#endif
__END_DECLS

#endif /* _PTHREAD_H_ */
