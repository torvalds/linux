/**
 * util/locks.h - unbound locking primitives
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 * 
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef UTIL_LOCKS_H
#define UTIL_LOCKS_H

/**
 * \file
 * Locking primitives.
 * If pthreads is available, these are used.
 * If no locking exists, they do nothing.
 *
 * The idea is to have different sorts of locks for different tasks.
 * This allows the locking code to be ported more easily.
 *
 * Types of locks that are supported.
 *   o lock_rw: lock that has many readers and one writer (to a data entry).
 *   o lock_basic: simple mutex. Blocking, one person has access only.
 *     This lock is meant for non performance sensitive uses.
 *   o lock_quick: speed lock. For performance sensitive locking of critical
 *     sections. Could be implemented by a mutex or a spinlock.
 * 
 * Also thread creation and deletion functions are defined here.
 */

/* if you define your own LOCKRET before including locks.h, you can get most
 * locking functions without the dependency on log_err. */
#ifndef LOCKRET
#include "util/log.h"
/**
 * The following macro is used to check the return value of the
 * pthread calls. They return 0 on success and an errno on error.
 * The errno is logged to the logfile with a descriptive comment.
 */
#define LOCKRET(func) do {\
	int lockret_err;		\
	if( (lockret_err=(func)) != 0)		\
		log_err("%s at %d could not " #func ": %s", \
		__FILE__, __LINE__, strerror(lockret_err));	\
 	} while(0)
#endif

/** DEBUG: use thread debug whenever possible */
#if defined(HAVE_PTHREAD) && defined(HAVE_PTHREAD_SPINLOCK_T) && defined(ENABLE_LOCK_CHECKS)
#  define USE_THREAD_DEBUG
#endif

#ifdef USE_THREAD_DEBUG
/******************* THREAD DEBUG ************************/
/* (some) checking; to detect races and deadlocks. */
#include "testcode/checklocks.h"

#else /* USE_THREAD_DEBUG */
#define lock_protect(lock, area, size) /* nop */
#define lock_unprotect(lock, area) /* nop */
#define lock_get_mem(lock) (0) /* nothing */
#define checklock_start() /* nop */
#define checklock_stop() /* nop */
#define checklock_set_output_name(name) /* nop */

#ifdef HAVE_PTHREAD
#include <pthread.h>

/******************* PTHREAD ************************/

/** use pthread mutex for basic lock */
typedef pthread_mutex_t lock_basic_type;
/** small front for pthread init func, NULL is default attrs. */
#define lock_basic_init(lock) LOCKRET(pthread_mutex_init(lock, NULL))
#define lock_basic_destroy(lock) LOCKRET(pthread_mutex_destroy(lock))
#define lock_basic_lock(lock) LOCKRET(pthread_mutex_lock(lock))
#define lock_basic_unlock(lock) LOCKRET(pthread_mutex_unlock(lock))

#ifndef HAVE_PTHREAD_RWLOCK_T
/** in case rwlocks are not supported, use a mutex. */
typedef pthread_mutex_t lock_rw_type;
#define lock_rw_init(lock) LOCKRET(pthread_mutex_init(lock, NULL))
#define lock_rw_destroy(lock) LOCKRET(pthread_mutex_destroy(lock))
#define lock_rw_rdlock(lock) LOCKRET(pthread_mutex_lock(lock))
#define lock_rw_wrlock(lock) LOCKRET(pthread_mutex_lock(lock))
#define lock_rw_unlock(lock) LOCKRET(pthread_mutex_unlock(lock))
#else /* HAVE_PTHREAD_RWLOCK_T */
/** we use the pthread rwlock */
typedef pthread_rwlock_t lock_rw_type;
/** small front for pthread init func, NULL is default attrs. */
#define lock_rw_init(lock) LOCKRET(pthread_rwlock_init(lock, NULL))
#define lock_rw_destroy(lock) LOCKRET(pthread_rwlock_destroy(lock))
#define lock_rw_rdlock(lock) LOCKRET(pthread_rwlock_rdlock(lock))
#define lock_rw_wrlock(lock) LOCKRET(pthread_rwlock_wrlock(lock))
#define lock_rw_unlock(lock) LOCKRET(pthread_rwlock_unlock(lock))
#endif /* HAVE_PTHREAD_RWLOCK_T */

#ifndef HAVE_PTHREAD_SPINLOCK_T
/** in case spinlocks are not supported, use a mutex. */
typedef pthread_mutex_t lock_quick_type;
/** small front for pthread init func, NULL is default attrs. */
#define lock_quick_init(lock) LOCKRET(pthread_mutex_init(lock, NULL))
#define lock_quick_destroy(lock) LOCKRET(pthread_mutex_destroy(lock))
#define lock_quick_lock(lock) LOCKRET(pthread_mutex_lock(lock))
#define lock_quick_unlock(lock) LOCKRET(pthread_mutex_unlock(lock))

#else /* HAVE_PTHREAD_SPINLOCK_T */
/** use pthread spinlock for the quick lock */
typedef pthread_spinlock_t lock_quick_type;
/** 
 * allocate process private since this is available whether
 * Thread Process-Shared Synchronization is supported or not.
 * This means only threads inside this process may access the lock.
 * (not threads from another process that shares memory).
 * spinlocks are not supported on all pthread platforms. 
 */
#define lock_quick_init(lock) LOCKRET(pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE))
#define lock_quick_destroy(lock) LOCKRET(pthread_spin_destroy(lock))
#define lock_quick_lock(lock) LOCKRET(pthread_spin_lock(lock))
#define lock_quick_unlock(lock) LOCKRET(pthread_spin_unlock(lock))

#endif /* HAVE SPINLOCK */

/** Thread creation */
typedef pthread_t ub_thread_type;
/** On alpine linux default thread stack size is 80 Kb. See
http://wiki.musl-libc.org/wiki/Functional_differences_from_glibc#Thread_stack_size
This is not enough and cause segfault. Other linux distros have 2 Mb at least.
Wrapper for set up thread stack size */
#define PTHREADSTACKSIZE 2*1024*1024
#define PTHREADCREATE(thr, stackrequired, func, arg) do {\
	pthread_attr_t attr; \
	size_t stacksize; \
	LOCKRET(pthread_attr_init(&attr)); \
	LOCKRET(pthread_attr_getstacksize(&attr, &stacksize)); \
	if (stacksize < stackrequired) { \
		LOCKRET(pthread_attr_setstacksize(&attr, stackrequired)); \
		LOCKRET(pthread_create(thr, &attr, func, arg)); \
		LOCKRET(pthread_attr_getstacksize(&attr, &stacksize)); \
		verbose(VERB_ALGO, "Thread stack size set to %u", (unsigned)stacksize); \
	} else {LOCKRET(pthread_create(thr, NULL, func, arg));} \
	} while(0)
/** Use wrapper for set thread stack size on attributes. */
#define ub_thread_create(thr, func, arg) PTHREADCREATE(thr, PTHREADSTACKSIZE, func, arg)
/** get self id. */
#define ub_thread_self() pthread_self()
/** wait for another thread to terminate */
#define ub_thread_join(thread) LOCKRET(pthread_join(thread, NULL))
typedef pthread_key_t ub_thread_key_type;
#define ub_thread_key_create(key, f) LOCKRET(pthread_key_create(key, f))
#define ub_thread_key_set(key, v) LOCKRET(pthread_setspecific(key, v))
#define ub_thread_key_get(key) pthread_getspecific(key)

#else /* we do not HAVE_PTHREAD */
#ifdef HAVE_SOLARIS_THREADS

/******************* SOLARIS THREADS ************************/
#include <synch.h>
#include <thread.h>

typedef rwlock_t lock_rw_type;
#define lock_rw_init(lock) LOCKRET(rwlock_init(lock, USYNC_THREAD, NULL))
#define lock_rw_destroy(lock) LOCKRET(rwlock_destroy(lock))
#define lock_rw_rdlock(lock) LOCKRET(rw_rdlock(lock))
#define lock_rw_wrlock(lock) LOCKRET(rw_wrlock(lock))
#define lock_rw_unlock(lock) LOCKRET(rw_unlock(lock))

/** use basic mutex */
typedef mutex_t lock_basic_type;
#define lock_basic_init(lock) LOCKRET(mutex_init(lock, USYNC_THREAD, NULL))
#define lock_basic_destroy(lock) LOCKRET(mutex_destroy(lock))
#define lock_basic_lock(lock) LOCKRET(mutex_lock(lock))
#define lock_basic_unlock(lock) LOCKRET(mutex_unlock(lock))

/** No spinlocks in solaris threads API. Use a mutex. */
typedef mutex_t lock_quick_type;
#define lock_quick_init(lock) LOCKRET(mutex_init(lock, USYNC_THREAD, NULL))
#define lock_quick_destroy(lock) LOCKRET(mutex_destroy(lock))
#define lock_quick_lock(lock) LOCKRET(mutex_lock(lock))
#define lock_quick_unlock(lock) LOCKRET(mutex_unlock(lock))

/** Thread creation, create a default thread. */
typedef thread_t ub_thread_type;
#define ub_thread_create(thr, func, arg) LOCKRET(thr_create(NULL, NULL, func, arg, NULL, thr))
#define ub_thread_self() thr_self()
#define ub_thread_join(thread) LOCKRET(thr_join(thread, NULL, NULL))
typedef thread_key_t ub_thread_key_type;
#define ub_thread_key_create(key, f) LOCKRET(thr_keycreate(key, f))
#define ub_thread_key_set(key, v) LOCKRET(thr_setspecific(key, v))
void* ub_thread_key_get(ub_thread_key_type key);


#else /* we do not HAVE_SOLARIS_THREADS and no PTHREADS */
/******************* WINDOWS THREADS ************************/
#ifdef HAVE_WINDOWS_THREADS
#include <windows.h>

/* Use a mutex */
typedef LONG lock_rw_type;
#define lock_rw_init(lock) lock_basic_init(lock)
#define lock_rw_destroy(lock) lock_basic_destroy(lock)
#define lock_rw_rdlock(lock) lock_basic_lock(lock)
#define lock_rw_wrlock(lock) lock_basic_lock(lock)
#define lock_rw_unlock(lock) lock_basic_unlock(lock)

/** the basic lock is a mutex, implemented opaquely, for error handling. */
typedef LONG lock_basic_type;
void lock_basic_init(lock_basic_type* lock);
void lock_basic_destroy(lock_basic_type* lock);
void lock_basic_lock(lock_basic_type* lock);
void lock_basic_unlock(lock_basic_type* lock);

/** on windows no spinlock, use mutex too. */
typedef LONG lock_quick_type;
#define lock_quick_init(lock) lock_basic_init(lock)
#define lock_quick_destroy(lock) lock_basic_destroy(lock)
#define lock_quick_lock(lock) lock_basic_lock(lock)
#define lock_quick_unlock(lock) lock_basic_unlock(lock)

/** Thread creation, create a default thread. */
typedef HANDLE ub_thread_type;
void ub_thread_create(ub_thread_type* thr, void* (*func)(void*), void* arg);
ub_thread_type ub_thread_self(void);
void ub_thread_join(ub_thread_type thr);
typedef DWORD ub_thread_key_type;
void ub_thread_key_create(ub_thread_key_type* key, void* f);
void ub_thread_key_set(ub_thread_key_type key, void* v);
void* ub_thread_key_get(ub_thread_key_type key);

#else /* we do not HAVE_SOLARIS_THREADS, PTHREADS or WINDOWS_THREADS */

/******************* NO THREADS ************************/
#define THREADS_DISABLED 1
/** In case there is no thread support, define locks to do nothing */
typedef int lock_rw_type;
#define lock_rw_init(lock) /* nop */
#define lock_rw_destroy(lock) /* nop */
#define lock_rw_rdlock(lock) /* nop */
#define lock_rw_wrlock(lock) /* nop */
#define lock_rw_unlock(lock) /* nop */

/** define locks to do nothing */
typedef int lock_basic_type;
#define lock_basic_init(lock) /* nop */
#define lock_basic_destroy(lock) /* nop */
#define lock_basic_lock(lock) /* nop */
#define lock_basic_unlock(lock) /* nop */

/** define locks to do nothing */
typedef int lock_quick_type;
#define lock_quick_init(lock) /* nop */
#define lock_quick_destroy(lock) /* nop */
#define lock_quick_lock(lock) /* nop */
#define lock_quick_unlock(lock) /* nop */

/** Thread creation, threads do not exist */
typedef pid_t ub_thread_type;
/** ub_thread_create is simulated with fork (extremely heavy threads,
  * with no shared memory). */
#define ub_thread_create(thr, func, arg) \
	ub_thr_fork_create(thr, func, arg)
#define ub_thread_self() getpid()
#define ub_thread_join(thread) ub_thr_fork_wait(thread)
void ub_thr_fork_wait(ub_thread_type thread);
void ub_thr_fork_create(ub_thread_type* thr, void* (*func)(void*), void* arg);
typedef void* ub_thread_key_type;
#define ub_thread_key_create(key, f) (*(key)) = NULL
#define ub_thread_key_set(key, v) (key) = (v)
#define ub_thread_key_get(key) (key)

#endif /* HAVE_WINDOWS_THREADS */
#endif /* HAVE_SOLARIS_THREADS */
#endif /* HAVE_PTHREAD */
#endif /* USE_THREAD_DEBUG */

/**
 * Block all signals for this thread.
 * fatal exit on error.
 */
void ub_thread_blocksigs(void);

/**
 * unblock one signal for this thread.
 */
void ub_thread_sig_unblock(int sig);

#endif /* UTIL_LOCKS_H */
