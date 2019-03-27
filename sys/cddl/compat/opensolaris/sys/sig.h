/*-
 * Copyright (c) 2008 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#ifndef _OPENSOLARIS_SYS_SIG_H_
#define	_OPENSOLARIS_SYS_SIG_H_

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/debug.h>

#define	FORREAL		0
#define	JUSTLOOKING	1

static __inline int
issig(int why)
{
	struct thread *td = curthread;
	struct proc *p;
	int sig;

	ASSERT(why == FORREAL || why == JUSTLOOKING);
	if (SIGPENDING(td)) {
		if (why == JUSTLOOKING)
			return (1);
		p = td->td_proc;
		PROC_LOCK(p);
		mtx_lock(&p->p_sigacts->ps_mtx);
		sig = cursig(td);
		mtx_unlock(&p->p_sigacts->ps_mtx);
		PROC_UNLOCK(p);
		if (sig != 0)
			return (1);
	}
	return (0);
}

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_SIG_H_ */
