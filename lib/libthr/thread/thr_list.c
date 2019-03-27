/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * Copyright (C) 2003 Daniel M. Eischen <deischen@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/queue.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "libc_private.h"
#include "thr_private.h"

/*#define DEBUG_THREAD_LIST */
#ifdef DEBUG_THREAD_LIST
#define DBG_MSG		stdout_debug
#else
#define DBG_MSG(x...)
#endif

#define MAX_THREADS		100000

/*
 * Define a high water mark for the maximum number of threads that
 * will be cached.  Once this level is reached, any extra threads
 * will be free()'d.
 */
#define	MAX_CACHED_THREADS	100

/*
 * We've got to keep track of everything that is allocated, not only
 * to have a speedy free list, but also so they can be deallocated
 * after a fork().
 */
static TAILQ_HEAD(, pthread)	free_threadq;
static struct umutex		free_thread_lock = DEFAULT_UMUTEX;
static struct umutex		tcb_lock = DEFAULT_UMUTEX;
static int			free_thread_count = 0;
static int			inited = 0;
static int			total_threads;

LIST_HEAD(thread_hash_head, pthread);
#define HASH_QUEUES	128
static struct thread_hash_head	thr_hashtable[HASH_QUEUES];
#define	THREAD_HASH(thrd)	(((unsigned long)thrd >> 8) % HASH_QUEUES)

static void thr_destroy(struct pthread *curthread, struct pthread *thread);

void
_thr_list_init(void)
{
	int i;

	_gc_count = 0;
	total_threads = 1;
	_thr_urwlock_init(&_thr_list_lock);
	TAILQ_INIT(&_thread_list);
	TAILQ_INIT(&free_threadq);
	_thr_umutex_init(&free_thread_lock);
	_thr_umutex_init(&tcb_lock);
	if (inited) {
		for (i = 0; i < HASH_QUEUES; ++i)
			LIST_INIT(&thr_hashtable[i]);
	}
	inited = 1;
}

void
_thr_gc(struct pthread *curthread)
{
	struct pthread *td, *td_next;
	TAILQ_HEAD(, pthread) worklist;

	TAILQ_INIT(&worklist);
	THREAD_LIST_WRLOCK(curthread);

	/* Check the threads waiting for GC. */
	TAILQ_FOREACH_SAFE(td, &_thread_gc_list, gcle, td_next) {
		if (td->tid != TID_TERMINATED) {
			/* make sure we are not still in userland */
			continue;
		}
		_thr_stack_free(&td->attr);
		THR_GCLIST_REMOVE(td);
		TAILQ_INSERT_HEAD(&worklist, td, gcle);
	}
	THREAD_LIST_UNLOCK(curthread);

	while ((td = TAILQ_FIRST(&worklist)) != NULL) {
		TAILQ_REMOVE(&worklist, td, gcle);
		/*
		 * XXX we don't free initial thread, because there might
		 * have some code referencing initial thread.
		 */
		if (td == _thr_initial) {
			DBG_MSG("Initial thread won't be freed\n");
			continue;
		}

		_thr_free(curthread, td);
	}
}

struct pthread *
_thr_alloc(struct pthread *curthread)
{
	struct pthread	*thread = NULL;
	struct tcb	*tcb;

	if (curthread != NULL) {
		if (GC_NEEDED())
			_thr_gc(curthread);
		if (free_thread_count > 0) {
			THR_LOCK_ACQUIRE(curthread, &free_thread_lock);
			if ((thread = TAILQ_FIRST(&free_threadq)) != NULL) {
				TAILQ_REMOVE(&free_threadq, thread, tle);
				free_thread_count--;
			}
			THR_LOCK_RELEASE(curthread, &free_thread_lock);
		}
	}
	if (thread == NULL) {
		if (total_threads > MAX_THREADS)
			return (NULL);
		atomic_fetchadd_int(&total_threads, 1);
		thread = calloc(1, sizeof(struct pthread));
		if (thread == NULL) {
			atomic_fetchadd_int(&total_threads, -1);
			return (NULL);
		}
		if ((thread->sleepqueue = _sleepq_alloc()) == NULL ||
		    (thread->wake_addr = _thr_alloc_wake_addr()) == NULL) {
			thr_destroy(curthread, thread);
			atomic_fetchadd_int(&total_threads, -1);
			return (NULL);
		}
	} else {
		bzero(&thread->_pthread_startzero, 
			__rangeof(struct pthread, _pthread_startzero, _pthread_endzero));
	}
	if (curthread != NULL) {
		THR_LOCK_ACQUIRE(curthread, &tcb_lock);
		tcb = _tcb_ctor(thread, 0 /* not initial tls */);
		THR_LOCK_RELEASE(curthread, &tcb_lock);
	} else {
		tcb = _tcb_ctor(thread, 1 /* initial tls */);
	}
	if (tcb != NULL) {
		thread->tcb = tcb;
	} else {
		thr_destroy(curthread, thread);
		atomic_fetchadd_int(&total_threads, -1);
		thread = NULL;
	}
	return (thread);
}

void
_thr_free(struct pthread *curthread, struct pthread *thread)
{
	DBG_MSG("Freeing thread %p\n", thread);

	/*
	 * Always free tcb, as we only know it is part of RTLD TLS
	 * block, but don't know its detail and can not assume how
	 * it works, so better to avoid caching it here.
	 */
	if (curthread != NULL) {
		THR_LOCK_ACQUIRE(curthread, &tcb_lock);
		_tcb_dtor(thread->tcb);
		THR_LOCK_RELEASE(curthread, &tcb_lock);
	} else {
		_tcb_dtor(thread->tcb);
	}
	thread->tcb = NULL;
	if ((curthread == NULL) || (free_thread_count >= MAX_CACHED_THREADS)) {
		thr_destroy(curthread, thread);
		atomic_fetchadd_int(&total_threads, -1);
	} else {
		/*
		 * Add the thread to the free thread list, this also avoids
		 * pthread id is reused too quickly, may help some buggy apps.
		 */
		THR_LOCK_ACQUIRE(curthread, &free_thread_lock);
		TAILQ_INSERT_TAIL(&free_threadq, thread, tle);
		free_thread_count++;
		THR_LOCK_RELEASE(curthread, &free_thread_lock);
	}
}

static void
thr_destroy(struct pthread *curthread __unused, struct pthread *thread)
{
	if (thread->sleepqueue != NULL)
		_sleepq_free(thread->sleepqueue);
	if (thread->wake_addr != NULL)
		_thr_release_wake_addr(thread->wake_addr);
	free(thread);
}

/*
 * Add the thread to the list of all threads and increment
 * number of active threads.
 */
void
_thr_link(struct pthread *curthread, struct pthread *thread)
{
	THREAD_LIST_WRLOCK(curthread);
	THR_LIST_ADD(thread);
	THREAD_LIST_UNLOCK(curthread);
	atomic_add_int(&_thread_active_threads, 1);
}

/*
 * Remove an active thread.
 */
void
_thr_unlink(struct pthread *curthread, struct pthread *thread)
{
	THREAD_LIST_WRLOCK(curthread);
	THR_LIST_REMOVE(thread);
	THREAD_LIST_UNLOCK(curthread);
	atomic_add_int(&_thread_active_threads, -1);
}

void
_thr_hash_add(struct pthread *thread)
{
	struct thread_hash_head *head;

	head = &thr_hashtable[THREAD_HASH(thread)];
	LIST_INSERT_HEAD(head, thread, hle);
}

void
_thr_hash_remove(struct pthread *thread)
{
	LIST_REMOVE(thread, hle);
}

struct pthread *
_thr_hash_find(struct pthread *thread)
{
	struct pthread *td;
	struct thread_hash_head *head;

	head = &thr_hashtable[THREAD_HASH(thread)];
	LIST_FOREACH(td, head, hle) {
		if (td == thread)
			return (thread);
	}
	return (NULL);
}

/*
 * Find a thread in the linked list of active threads and add a reference
 * to it.  Threads with positive reference counts will not be deallocated
 * until all references are released.
 */
int
_thr_ref_add(struct pthread *curthread, struct pthread *thread,
    int include_dead)
{
	int ret;

	if (thread == NULL)
		/* Invalid thread: */
		return (EINVAL);

	if ((ret = _thr_find_thread(curthread, thread, include_dead)) == 0) {
		thread->refcount++;
		THR_CRITICAL_ENTER(curthread);
		THR_THREAD_UNLOCK(curthread, thread);
	}

	/* Return zero if the thread exists: */
	return (ret);
}

void
_thr_ref_delete(struct pthread *curthread, struct pthread *thread)
{
	THR_THREAD_LOCK(curthread, thread);
	thread->refcount--;
	_thr_try_gc(curthread, thread);
	THR_CRITICAL_LEAVE(curthread);
}

/* entered with thread lock held, exit with thread lock released */
void
_thr_try_gc(struct pthread *curthread, struct pthread *thread)
{
	if (THR_SHOULD_GC(thread)) {
		THR_REF_ADD(curthread, thread);
		THR_THREAD_UNLOCK(curthread, thread);
		THREAD_LIST_WRLOCK(curthread);
		THR_THREAD_LOCK(curthread, thread);
		THR_REF_DEL(curthread, thread);
		if (THR_SHOULD_GC(thread)) {
			THR_LIST_REMOVE(thread);
			THR_GCLIST_ADD(thread);
		}
		THR_THREAD_UNLOCK(curthread, thread);
		THREAD_LIST_UNLOCK(curthread);
	} else {
		THR_THREAD_UNLOCK(curthread, thread);
	}
}

/* return with thread lock held if thread is found */
int
_thr_find_thread(struct pthread *curthread, struct pthread *thread,
    int include_dead)
{
	struct pthread *pthread;
	int ret;

	if (thread == NULL)
		return (EINVAL);

	ret = 0;
	THREAD_LIST_RDLOCK(curthread);
	pthread = _thr_hash_find(thread);
	if (pthread) {
		THR_THREAD_LOCK(curthread, pthread);
		if (include_dead == 0 && pthread->state == PS_DEAD) {
			THR_THREAD_UNLOCK(curthread, pthread);
			ret = ESRCH;
		}
	} else {
		ret = ESRCH;
	}
	THREAD_LIST_UNLOCK(curthread);
	return (ret);
}
