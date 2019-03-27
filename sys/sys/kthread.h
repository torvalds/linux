/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Peter Wemm <peter@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _SYS_KTHREAD_H_
#define	_SYS_KTHREAD_H_

#include <sys/cdefs.h>

/*
 * A kernel process descriptor; used to start "internal" daemons.
 *
 * Note: global_procpp may be NULL for no global save area.
 */
struct kproc_desc {
	const char	*arg0;			/* arg 0 (for 'ps' listing) */
	void		(*func)(void);		/* "main" for kernel process */
	struct proc	**global_procpp;	/* ptr to proc ptr save area */
};

 /* A kernel thread descriptor; used to start "internal" daemons. */
struct kthread_desc {
	const char	*arg0;			/* arg 0 (for 'ps' listing) */
	void		(*func)(void);		/* "main" for kernel thread */
	struct thread	**global_threadpp;	/* ptr to thread ptr save area */
};

int     kproc_create(void (*)(void *), void *, struct proc **,
	    int flags, int pages, const char *, ...) __printflike(6, 7);
void    kproc_exit(int) __dead2;
int	kproc_resume(struct proc *);
void	kproc_shutdown(void *, int);
void	kproc_start(const void *);
int	kproc_suspend(struct proc *, int);
void	kproc_suspend_check(struct proc *);

/* create a thread inthe given process. create the process if needed */
int     kproc_kthread_add(void (*)(void *), void *,
	    struct proc **,
	    struct thread **,
	    int flags, int pages,
	    const char *procname, const char *, ...) __printflike(8, 9);

int     kthread_add(void (*)(void *), void *,
	    struct proc *, struct thread **,
	    int flags, int pages, const char *, ...) __printflike(7, 8);
void    kthread_exit(void) __dead2;
int	kthread_resume(struct thread *);
void	kthread_shutdown(void *, int);
void	kthread_start(const void *);
int	kthread_suspend(struct thread *, int);
void	kthread_suspend_check(void);


#endif /* !_SYS_KTHREAD_H_ */
