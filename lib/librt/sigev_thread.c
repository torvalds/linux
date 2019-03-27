/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
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
 *
 */

#include <sys/types.h>

#include "namespace.h"
#include <err.h>
#include <errno.h>
#include <ucontext.h>
#include <sys/thr.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include "un-namespace.h"

#include "sigev_thread.h"

LIST_HEAD(sigev_list_head, sigev_node);
#define HASH_QUEUES		17
#define	HASH(t, id)		((((id) << 3) + (t)) % HASH_QUEUES)

static struct sigev_list_head	sigev_hash[HASH_QUEUES];
static struct sigev_list_head	sigev_all;
static LIST_HEAD(,sigev_thread)	sigev_threads;
static atomic_int		sigev_generation;
static pthread_mutex_t		*sigev_list_mtx;
static pthread_once_t		sigev_once = PTHREAD_ONCE_INIT;
static pthread_once_t		sigev_once_default = PTHREAD_ONCE_INIT;
static struct sigev_thread	*sigev_default_thread;
static pthread_attr_t		sigev_default_attr;
static int			atfork_registered;

static void	__sigev_fork_prepare(void);
static void	__sigev_fork_parent(void);
static void	__sigev_fork_child(void);
static struct sigev_thread	*sigev_thread_create(int);
static void	*sigev_service_loop(void *);
static void	*worker_routine(void *);
static void	worker_cleanup(void *);

#pragma weak _pthread_create

static void
attrcopy(pthread_attr_t *src, pthread_attr_t *dst)
{
	struct sched_param sched;
	void *a;
	size_t u;
	int v;

	_pthread_attr_getschedpolicy(src, &v);
	_pthread_attr_setschedpolicy(dst, v);

	_pthread_attr_getinheritsched(src, &v);
	_pthread_attr_setinheritsched(dst, v);

	_pthread_attr_getschedparam(src, &sched);
	_pthread_attr_setschedparam(dst, &sched);

	_pthread_attr_getscope(src, &v);
	_pthread_attr_setscope(dst, v);

	_pthread_attr_getstacksize(src, &u);
	_pthread_attr_setstacksize(dst, u);

	_pthread_attr_getstackaddr(src, &a);
	_pthread_attr_setstackaddr(src, a);

	_pthread_attr_getguardsize(src, &u);
	_pthread_attr_setguardsize(dst, u);
}

static __inline int
have_threads(void)
{
	return (&_pthread_create != NULL);
}

void
__sigev_thread_init(void)
{
	static int inited = 0;
	pthread_mutexattr_t mattr;
	int i;

	_pthread_mutexattr_init(&mattr);
	_pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_NORMAL);
	sigev_list_mtx = malloc(sizeof(pthread_mutex_t));
	_pthread_mutex_init(sigev_list_mtx, &mattr);
	_pthread_mutexattr_destroy(&mattr);

	for (i = 0; i < HASH_QUEUES; ++i)
		LIST_INIT(&sigev_hash[i]);
	LIST_INIT(&sigev_all);
	LIST_INIT(&sigev_threads);
	sigev_default_thread = NULL;
	if (atfork_registered == 0) {
		_pthread_atfork(
			__sigev_fork_prepare,
			__sigev_fork_parent,
			__sigev_fork_child);
		atfork_registered = 1;
	}
	if (!inited) {
		_pthread_attr_init(&sigev_default_attr);
		_pthread_attr_setscope(&sigev_default_attr,
			PTHREAD_SCOPE_SYSTEM);
		_pthread_attr_setdetachstate(&sigev_default_attr,
			PTHREAD_CREATE_DETACHED);
		inited = 1;
	}
	sigev_default_thread = sigev_thread_create(0);
}

int
__sigev_check_init(void)
{
	if (!have_threads())
		return (-1);

	_pthread_once(&sigev_once, __sigev_thread_init);
	return (sigev_default_thread != NULL) ? 0 : -1;
}

static void
__sigev_fork_prepare(void)
{
}

static void
__sigev_fork_parent(void)
{
}

static void
__sigev_fork_child(void)
{
	/*
	 * This is a hack, the thread libraries really should
	 * check if the handlers were already registered in
	 * pthread_atfork().
	 */
	atfork_registered = 1;
	memcpy(&sigev_once, &sigev_once_default, sizeof(sigev_once));
	__sigev_thread_init();
}

void
__sigev_list_lock(void)
{
	_pthread_mutex_lock(sigev_list_mtx);
}

void
__sigev_list_unlock(void)
{
	_pthread_mutex_unlock(sigev_list_mtx);
}

struct sigev_node *
__sigev_alloc(int type, const struct sigevent *evp, struct sigev_node *prev,
	int usedefault)
{
	struct sigev_node *sn;

	sn = calloc(1, sizeof(*sn));
	if (sn != NULL) {
		sn->sn_value = evp->sigev_value;
		sn->sn_func  = evp->sigev_notify_function;
		sn->sn_gen   = atomic_fetch_add_explicit(&sigev_generation, 1,
		    memory_order_relaxed);
		sn->sn_type  = type;
		_pthread_attr_init(&sn->sn_attr);
		_pthread_attr_setdetachstate(&sn->sn_attr, PTHREAD_CREATE_DETACHED);
		if (evp->sigev_notify_attributes)
			attrcopy(evp->sigev_notify_attributes, &sn->sn_attr);
		if (prev) {
			__sigev_list_lock();
			prev->sn_tn->tn_refcount++;
			__sigev_list_unlock();
			sn->sn_tn = prev->sn_tn;
		} else {
			sn->sn_tn = sigev_thread_create(usedefault);
			if (sn->sn_tn == NULL) {
				_pthread_attr_destroy(&sn->sn_attr);
				free(sn);
				sn = NULL;
			}
		}
	}
	return (sn);
}

void
__sigev_get_sigevent(struct sigev_node *sn, struct sigevent *newevp,
	sigev_id_t id)
{
	/*
	 * Build a new sigevent, and tell kernel to deliver SIGLIBRT
	 * signal to the new thread.
	 */
	newevp->sigev_notify = SIGEV_THREAD_ID;
	newevp->sigev_signo  = SIGLIBRT;
	newevp->sigev_notify_thread_id = (lwpid_t)sn->sn_tn->tn_lwpid;
	newevp->sigev_value.sival_ptr = (void *)id;
}

void
__sigev_free(struct sigev_node *sn)
{
	_pthread_attr_destroy(&sn->sn_attr);
	free(sn);
}

struct sigev_node *
__sigev_find(int type, sigev_id_t id)
{
	struct sigev_node *sn;
	int chain = HASH(type, id);

	LIST_FOREACH(sn, &sigev_hash[chain], sn_link) {
		if (sn->sn_type == type && sn->sn_id == id)
			break;
	}
	return (sn);
}

int
__sigev_register(struct sigev_node *sn)
{
	int chain = HASH(sn->sn_type, sn->sn_id);

	LIST_INSERT_HEAD(&sigev_hash[chain], sn, sn_link);
	return (0);
}

int
__sigev_delete(int type, sigev_id_t id)
{
	struct sigev_node *sn;

	sn = __sigev_find(type, id);
	if (sn != NULL)
		return (__sigev_delete_node(sn));
	return (0);
}

int
__sigev_delete_node(struct sigev_node *sn)
{
	LIST_REMOVE(sn, sn_link);

	if (--sn->sn_tn->tn_refcount == 0)
		_pthread_kill(sn->sn_tn->tn_thread, SIGLIBRT);
	if (sn->sn_flags & SNF_WORKING)
		sn->sn_flags |= SNF_REMOVED;
	else
		__sigev_free(sn);
	return (0);
}

static sigev_id_t
sigev_get_id(siginfo_t *si)
{
	switch(si->si_code) {
	case SI_TIMER:
		return (si->si_timerid);
	case SI_MESGQ:
		return (si->si_mqd);
	case SI_ASYNCIO:
		return (sigev_id_t)si->si_value.sival_ptr;
	}
	return (-1);
}

static struct sigev_thread *
sigev_thread_create(int usedefault)
{
	struct sigev_thread *tn;
	sigset_t set, oset;
	int ret;

	if (usedefault && sigev_default_thread) {
		__sigev_list_lock();
		sigev_default_thread->tn_refcount++;
		__sigev_list_unlock();
		return (sigev_default_thread);	
	}

	tn = malloc(sizeof(*tn));
	tn->tn_cur = NULL;
	tn->tn_lwpid = -1;
	tn->tn_refcount = 1;
	_pthread_cond_init(&tn->tn_cv, NULL);

	/* for debug */
	__sigev_list_lock();
	LIST_INSERT_HEAD(&sigev_threads, tn, tn_link);
	__sigev_list_unlock();

	sigfillset(&set);	/* SIGLIBRT is masked. */
	sigdelset(&set, SIGBUS);
	sigdelset(&set, SIGILL);
	sigdelset(&set, SIGFPE);
	sigdelset(&set, SIGSEGV);
	sigdelset(&set, SIGTRAP);
	_sigprocmask(SIG_SETMASK, &set, &oset);
	ret = _pthread_create(&tn->tn_thread, &sigev_default_attr,
		 sigev_service_loop, tn);
	_sigprocmask(SIG_SETMASK, &oset, NULL);

	if (ret != 0) {
		__sigev_list_lock();
		LIST_REMOVE(tn, tn_link);
		__sigev_list_unlock();
		free(tn);
		tn = NULL;
	} else {
		/* wait the thread to get its lwpid */

		__sigev_list_lock();
		while (tn->tn_lwpid == -1)
			_pthread_cond_wait(&tn->tn_cv, sigev_list_mtx);
		__sigev_list_unlock();
	}
	return (tn);
}

/*
 * The thread receives notification from kernel and creates
 * a thread to call user callback function.
 */
static void *
sigev_service_loop(void *arg)
{
	static int failure;

	siginfo_t si;
	sigset_t set;
	struct sigev_thread *tn;
	struct sigev_node *sn;
	sigev_id_t id;
	pthread_t td;
	int ret;

	tn = arg;
	thr_self(&tn->tn_lwpid);
	__sigev_list_lock();
	_pthread_cond_broadcast(&tn->tn_cv);
	__sigev_list_unlock();

	sigemptyset(&set);
	sigaddset(&set, SIGLIBRT);
	for (;;) {
		ret = sigwaitinfo(&set, &si);

		__sigev_list_lock();
		if (tn->tn_refcount == 0) {
			LIST_REMOVE(tn, tn_link);
			__sigev_list_unlock();
			free(tn);
			break;
		}

		if (ret == -1) {
			__sigev_list_unlock();
			continue;
		}

		id = sigev_get_id(&si);
		sn = __sigev_find(si.si_code, id);
		if (sn == NULL) {
			__sigev_list_unlock();
			continue;
		}
	
		sn->sn_info = si;
		if (sn->sn_flags & SNF_SYNC)
			tn->tn_cur = sn;
		else
			tn->tn_cur = NULL;
		sn->sn_flags |= SNF_WORKING;
		__sigev_list_unlock();

		ret = _pthread_create(&td, &sn->sn_attr, worker_routine, sn);
		if (ret != 0) {
			if (failure++ < 5)
				warnc(ret, "%s:%s failed to create thread.\n",
					__FILE__, __func__);

			__sigev_list_lock();
			sn->sn_flags &= ~SNF_WORKING;
			if (sn->sn_flags & SNF_REMOVED)
				__sigev_free(sn);
			__sigev_list_unlock();
		} else if (tn->tn_cur) {
			__sigev_list_lock();
			while (tn->tn_cur)
				_pthread_cond_wait(&tn->tn_cv, sigev_list_mtx);
			__sigev_list_unlock();
		}
	}
	return (0);
}

/*
 * newly created worker thread to call user callback function.
 */
static void *
worker_routine(void *arg)
{
	struct sigev_node *sn = arg;

	pthread_cleanup_push(worker_cleanup, sn);
	sn->sn_dispatch(sn);
	pthread_cleanup_pop(1);

	return (0);
}

/* clean up a notification after dispatch. */
static void
worker_cleanup(void *arg)
{
	struct sigev_node *sn = arg;

	__sigev_list_lock();
	if (sn->sn_flags & SNF_SYNC) {
		sn->sn_tn->tn_cur = NULL;
		_pthread_cond_broadcast(&sn->sn_tn->tn_cv);
	}
	if (sn->sn_flags & SNF_REMOVED)
		__sigev_free(sn);
	else
		sn->sn_flags &= ~SNF_WORKING;
	__sigev_list_unlock();
}
