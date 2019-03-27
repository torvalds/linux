/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2001 Julian Elischer <julian@freebsd.org>
 * for the FreeBSD Foundation.
 *
 *  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible 
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_KSE_H_
#define _SYS_KSE_H_

#include <sys/ucontext.h>
#include <sys/time.h>
#include <sys/signal.h>

/*
 * This file defines the structures needed for communication between
 * the userland and the kernel when running a KSE-based threading system.
 * The only programs that should see this file are the user thread
 * scheduler (UTS) and the kernel.
 */
struct kse_mailbox;

typedef void	kse_func_t(struct kse_mailbox *);

/*
 * Thread mailbox.
 *
 * This describes a user thread to the kernel scheduler.
 */
struct kse_thr_mailbox {
	ucontext_t		tm_context;	/* User and machine context */
	uint32_t		tm_flags;	/* Thread flags */
	struct kse_thr_mailbox	*tm_next;	/* Next thread in list */
	void			*tm_udata;	/* For use by the UTS */
	uint32_t		tm_uticks;	/* Time in userland */
	uint32_t		tm_sticks;	/* Time in kernel */
	siginfo_t		tm_syncsig;
	uint32_t		tm_dflags;	/* Debug flags */
	lwpid_t			tm_lwp;		/* kernel thread UTS runs on */
	uint32_t		__spare__[6];
};

/*
 * KSE mailbox.
 *
 * Communication path between the UTS and the kernel scheduler specific to
 * a single KSE.
 */
struct kse_mailbox {
	uint32_t		km_version;	/* Mailbox version */
	struct kse_thr_mailbox	*km_curthread;	/* Currently running thread */
	struct kse_thr_mailbox	*km_completed;	/* Threads back from kernel */
	sigset_t		km_sigscaught;	/* Caught signals */
	uint32_t		km_flags;	/* Mailbox flags */
	kse_func_t		*km_func;	/* UTS function */
	stack_t			km_stack;	/* UTS stack */
	void			*km_udata;	/* For use by the UTS */
	struct timespec		km_timeofday;	/* Time of day */
	uint32_t		km_quantum;	/* Upcall quantum in msecs */
	lwpid_t			km_lwp;		/* kernel thread UTS runs on */
	uint32_t		__spare2__[7];
};

#define KSE_VER_0		0
#define KSE_VERSION		KSE_VER_0

/* These flags are kept in km_flags */
#define KMF_NOUPCALL		0x01
#define KMF_NOCOMPLETED		0x02
#define KMF_DONE		0x04
#define KMF_BOUND		0x08
#define KMF_WAITSIGEVENT	0x10

/* These flags are kept in tm_flags */
#define TMF_NOUPCALL		0x01

/* These flags are kept in tm_dlfags */
#define TMDF_SSTEP		0x01
#define TMDF_SUSPEND		0x02

/* Flags for kse_switchin */
#define KSE_SWITCHIN_SETTMBX	0x01

/* Commands for kse_thr_interrupt */
#define KSE_INTR_INTERRUPT	1
#define KSE_INTR_RESTART	2
#define KSE_INTR_SENDSIG	3
#define KSE_INTR_SIGEXIT	4
#define KSE_INTR_DBSUSPEND	5
#define KSE_INTR_EXECVE		6

struct kse_execve_args {
	sigset_t	sigmask;
	sigset_t	sigpend;
	char		*path;
	char		**argv;
	char		**envp;
	void		*reserved;
};

#ifndef _KERNEL
int	kse_create(struct kse_mailbox *, int);
int	kse_exit(void);
int	kse_release(struct timespec *);
int	kse_thr_interrupt(struct kse_thr_mailbox *, int, long);
int	kse_wakeup(struct kse_mailbox *);
int	kse_switchin(struct kse_thr_mailbox *, int flags);
#endif	/* !_KERNEL */

#endif	/* !_SYS_KSE_H_ */
