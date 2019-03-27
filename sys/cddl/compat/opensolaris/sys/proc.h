/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef _OPENSOLARIS_SYS_PROC_H_
#define	_OPENSOLARIS_SYS_PROC_H_

#include <sys/param.h>
#include <sys/kthread.h>
#include_next <sys/proc.h>
#include <sys/stdint.h>
#include <sys/smp.h>
#include <sys/sched.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/unistd.h>
#include <sys/debug.h>

#ifdef _KERNEL

#define	CPU		curcpu
#define	minclsyspri	PRIBIO
#define	maxclsyspri	PVM
#define	max_ncpus	(mp_maxid + 1)
#define	boot_max_ncpus	(mp_maxid + 1)

#define	TS_RUN	0

#define	p0	proc0

#define	t_tid	td_tid

typedef	short		pri_t;
typedef	struct thread	_kthread;
typedef	struct thread	kthread_t;
typedef struct thread	*kthread_id_t;
typedef struct proc	proc_t;

extern struct proc *zfsproc;

static __inline kthread_t *
do_thread_create(caddr_t stk, size_t stksize, void (*proc)(void *), void *arg,
    size_t len, proc_t *pp, int state, pri_t pri)
{
	kthread_t *td = NULL;
	int error;

	/*
	 * Be sure there are no surprises.
	 */
	ASSERT(stk == NULL);
	ASSERT(len == 0);
	ASSERT(state == TS_RUN);
	ASSERT(pp == &p0);

	error = kproc_kthread_add(proc, arg, &zfsproc, &td, RFSTOPPED,
	    stksize / PAGE_SIZE, "zfskern", "solthread %p", proc);
	if (error == 0) {
		thread_lock(td);
		sched_prio(td, pri);
		sched_add(td, SRQ_BORING);
		thread_unlock(td);
	}
	return (td);
}

#define	thread_create(stk, stksize, proc, arg, len, pp, state, pri) \
	do_thread_create(stk, stksize, proc, arg, len, pp, state, pri)
#define	thread_exit()	kthread_exit()

int	uread(proc_t *, void *, size_t, uintptr_t);
int	uwrite(proc_t *, void *, size_t, uintptr_t);

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_PROC_H_ */
