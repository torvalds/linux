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
#include <sys/signalvar.h>
#include <sys/rtprio.h>
#include <sys/mman.h>
#include <pthread.h>

#include "thr_private.h"

/*#define DEBUG_THREAD_KERN */
#ifdef DEBUG_THREAD_KERN
#define DBG_MSG		stdout_debug
#else
#define DBG_MSG(x...)
#endif

static struct umutex	addr_lock;
static struct wake_addr *wake_addr_head;
static struct wake_addr default_wake_addr;

/*
 * This is called when the first thread (other than the initial
 * thread) is created.
 */
void
_thr_setthreaded(int threaded)
{
	__isthreaded = threaded;
}

void
_thr_assert_lock_level(void)
{
	PANIC("locklevel <= 0");
}

int
_rtp_to_schedparam(const struct rtprio *rtp, int *policy,
	struct sched_param *param)
{
	switch(rtp->type) {
	case RTP_PRIO_REALTIME:
		*policy = SCHED_RR;
		param->sched_priority = RTP_PRIO_MAX - rtp->prio;
		break;
	case RTP_PRIO_FIFO:
		*policy = SCHED_FIFO;
		param->sched_priority = RTP_PRIO_MAX - rtp->prio;
		break;
	default:
		*policy = SCHED_OTHER;
		param->sched_priority = 0;
		break;
	}
	return (0);
}

int
_schedparam_to_rtp(int policy, const struct sched_param *param,
	struct rtprio *rtp)
{
	switch(policy) {
	case SCHED_RR:
		rtp->type = RTP_PRIO_REALTIME;
		rtp->prio = RTP_PRIO_MAX - param->sched_priority;
		break;
	case SCHED_FIFO:
		rtp->type = RTP_PRIO_FIFO;
		rtp->prio = RTP_PRIO_MAX - param->sched_priority;
		break;
	case SCHED_OTHER:
	default:
		rtp->type = RTP_PRIO_NORMAL;
		rtp->prio = 0;
		break;
	}
	return (0);
}

int
_thr_getscheduler(lwpid_t lwpid, int *policy, struct sched_param *param)
{
	struct rtprio rtp;
	int ret;

	ret = rtprio_thread(RTP_LOOKUP, lwpid, &rtp);
	if (ret == -1)
		return (ret);
	_rtp_to_schedparam(&rtp, policy, param);
	return (0);
}

int
_thr_setscheduler(lwpid_t lwpid, int policy, const struct sched_param *param)
{
	struct rtprio rtp;

	_schedparam_to_rtp(policy, param, &rtp);
	return (rtprio_thread(RTP_SET, lwpid, &rtp));
}

void
_thr_wake_addr_init(void)
{
	_thr_umutex_init(&addr_lock);
	wake_addr_head = NULL;
}

/*
 * Allocate wake-address, the memory area is never freed after
 * allocated, this becauses threads may be referencing it.
 */
struct wake_addr *
_thr_alloc_wake_addr(void)
{
	struct pthread *curthread;
	struct wake_addr *p;

	if (_thr_initial == NULL) {
		return &default_wake_addr;
	}

	curthread = _get_curthread();

	THR_LOCK_ACQUIRE(curthread, &addr_lock);
	if (wake_addr_head == NULL) {
		unsigned i;
		unsigned pagesize = getpagesize();
		struct wake_addr *pp = (struct wake_addr *)
			mmap(NULL, getpagesize(), PROT_READ|PROT_WRITE,
			MAP_ANON|MAP_PRIVATE, -1, 0);
		for (i = 1; i < pagesize/sizeof(struct wake_addr); ++i)
			pp[i].link = &pp[i+1];
		pp[i-1].link = NULL;	
		wake_addr_head = &pp[1];
		p = &pp[0];
	} else {
		p = wake_addr_head;
		wake_addr_head = p->link;
	}
	THR_LOCK_RELEASE(curthread, &addr_lock);
	p->value = 0;
	return (p);
}

void
_thr_release_wake_addr(struct wake_addr *wa)
{
	struct pthread *curthread = _get_curthread();

	if (wa == &default_wake_addr)
		return;
	THR_LOCK_ACQUIRE(curthread, &addr_lock);
	wa->link = wake_addr_head;
	wake_addr_head = wa;
	THR_LOCK_RELEASE(curthread, &addr_lock);
}

/* Sleep on thread wakeup address */
int
_thr_sleep(struct pthread *curthread, int clockid,
	const struct timespec *abstime)
{

	if (curthread->wake_addr->value != 0)
		return (0);

	return _thr_umtx_timedwait_uint(&curthread->wake_addr->value, 0,
                 clockid, abstime, 0);
}

void
_thr_wake_all(unsigned int *waddrs[], int count)
{
	int i;

	for (i = 0; i < count; ++i)
		*waddrs[i] = 1;
	_umtx_op(waddrs, UMTX_OP_NWAKE_PRIVATE, count, NULL, NULL);
}
