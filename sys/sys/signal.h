/*	$OpenBSD: signal.h,v 1.29 2018/04/18 16:05:20 deraadt Exp $	*/
/*	$NetBSD: signal.h,v 1.21 1996/02/09 18:25:32 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)signal.h	8.2 (Berkeley) 1/21/94
 */

#ifndef	_SYS_SIGNAL_H_
#define	_SYS_SIGNAL_H_

#include <machine/signal.h>	/* sigcontext; codes for SIGILL, SIGFPE */

#define _NSIG	33		/* counting 0 (mask is 1-32) */

#if __BSD_VISIBLE
#define NSIG _NSIG
#endif

#define	SIGHUP	1	/* hangup */
#define	SIGINT	2	/* interrupt */
#define	SIGQUIT	3	/* quit */
#define	SIGILL	4	/* illegal instruction (not reset when caught) */
#define	SIGTRAP	5	/* trace trap (not reset when caught) */
#define	SIGABRT	6	/* abort() */
#if __BSD_VISIBLE
#define	SIGIOT	SIGABRT	/* compatibility */
#define	SIGEMT	7	/* EMT instruction */
#endif
#define	SIGFPE	8	/* floating point exception */
#define	SIGKILL	9	/* kill (cannot be caught or ignored) */
#define	SIGBUS	10	/* bus error */
#define	SIGSEGV	11	/* segmentation violation */
#define	SIGSYS	12	/* bad argument to system call */
#define	SIGPIPE	13	/* write on a pipe with no one to read it */
#define	SIGALRM	14	/* alarm clock */
#define	SIGTERM	15	/* software termination signal from kill */
#define	SIGURG	16	/* urgent condition on IO channel */
#define	SIGSTOP	17	/* sendable stop signal not from tty */
#define	SIGTSTP	18	/* stop signal from tty */
#define	SIGCONT	19	/* continue a stopped process */
#define	SIGCHLD	20	/* to parent on child stop or exit */
#define	SIGTTIN	21	/* to readers pgrp upon background tty read */
#define	SIGTTOU	22	/* like TTIN for output if (tp->t_local&LTOSTOP) */
#if __BSD_VISIBLE
#define	SIGIO	23	/* input/output possible signal */
#endif
#define	SIGXCPU	24	/* exceeded CPU time limit */
#define	SIGXFSZ	25	/* exceeded file size limit */
#define	SIGVTALRM 26	/* virtual time alarm */
#define	SIGPROF	27	/* profiling time alarm */
#if __BSD_VISIBLE
#define SIGWINCH 28	/* window size changes */
#define SIGINFO	29	/* information request */
#endif
#define SIGUSR1 30	/* user defined signal 1 */
#define SIGUSR2 31	/* user defined signal 2 */
#if __BSD_VISIBLE
#define SIGTHR  32	/* thread library AST */
#endif

/*
 * Language spec says we must list exactly one parameter, even though we
 * actually supply three.  Ugh!
 */
#define	SIG_DFL		(void (*)(int))0
#define	SIG_IGN		(void (*)(int))1
#define	SIG_ERR		(void (*)(int))-1

#if __POSIX_VISIBLE || __XPG_VISIBLE
#ifndef _SIGSET_T_DEFINED_
#define _SIGSET_T_DEFINED_
typedef unsigned int sigset_t;
#endif

#include <sys/siginfo.h>

/*
 * Signal vector "template" used in sigaction call.
 */
struct	sigaction {
	union {		/* signal handler */
		void	(*__sa_handler)(int);
		void	(*__sa_sigaction)(int, siginfo_t *, void *);
	} __sigaction_u;
	sigset_t sa_mask;		/* signal mask to apply */
	int	sa_flags;		/* see signal options below */
};

/* if SA_SIGINFO is set, sa_sigaction is to be used instead of sa_handler. */
#define sa_handler      __sigaction_u.__sa_handler
#define sa_sigaction    __sigaction_u.__sa_sigaction

#if __XPG_VISIBLE >= 500
#define SA_ONSTACK	0x0001	/* take signal on signal stack */
#define SA_RESTART	0x0002	/* restart system on signal return */
#define SA_RESETHAND	0x0004	/* reset to SIG_DFL when taking signal */
#define SA_NODEFER	0x0010	/* don't mask the signal we're delivering */
#define SA_NOCLDWAIT	0x0020	/* don't create zombies (assign to pid 1) */
#endif /* __XPG_VISIBLE >= 500 */
#define SA_NOCLDSTOP	0x0008	/* do not generate SIGCHLD on child stop */
#if __POSIX_VISIBLE >= 199309 || __XPG_VISIBLE >= 500
#define SA_SIGINFO	0x0040	/* generate siginfo_t */
#endif

/*
 * Flags for sigprocmask:
 */
#define	SIG_BLOCK	1	/* block specified signal set */
#define	SIG_UNBLOCK	2	/* unblock specified signal set */
#define	SIG_SETMASK	3	/* set specified signal set */
#endif	/* __POSIX_VISIBLE || __XPG_VISIBLE */

#if __BSD_VISIBLE
typedef	void (*sig_t)(int);	/* type of signal function */

/*
 * 4.3 compatibility:
 * Signal vector "template" used in sigvec call.
 */
struct	sigvec {
	void	(*sv_handler)(int);	/* signal handler */
	int	sv_mask;		/* signal mask to apply */
	int	sv_flags;		/* see signal options below */
};
#define SV_ONSTACK	SA_ONSTACK
#define SV_INTERRUPT	SA_RESTART	/* same bit, opposite sense */
#define SV_RESETHAND	SA_RESETHAND
#define sv_onstack	sv_flags	/* isn't compatibility wonderful! */

/*
 * Macro for converting signal number to a mask suitable for
 * sigblock().
 */
#define sigmask(m)	(1U << ((m)-1))

#define	BADSIG		SIG_ERR

#endif	/* __BSD_VISIBLE */

#if __BSD_VISIBLE || __XPG_VISIBLE >= 420
/*
 * Structure used in sigaltstack call.
 */
typedef struct sigaltstack {
	void	*ss_sp;			/* signal stack base */
	size_t	ss_size;		/* signal stack length */
	int	ss_flags;		/* SS_DISABLE and/or SS_ONSTACK */
} stack_t;
#define SS_ONSTACK	0x0001	/* take signals on alternate stack */
#define SS_DISABLE	0x0004	/* disable taking signals on alternate stack */
#define	MINSIGSTKSZ	(3U << _MAX_PAGE_SHIFT) /* minimum allowable stack */
#if _MAX_PAGE_SHIFT < 14			/* recommended stack size */
#define	SIGSTKSZ	(MINSIGSTKSZ + (1U << _MAX_PAGE_SHIFT) * 4)
#else
#define	SIGSTKSZ	(MINSIGSTKSZ + (1U << _MAX_PAGE_SHIFT) * 2)
#endif

typedef struct sigcontext ucontext_t;
#endif /* __BSD_VISIBLE || __XPG_VISIBLE >= 420 */

#ifndef _KERNEL
/*
 * For historical reasons; programs expect signal's return value to be
 * defined by <sys/signal.h>.
 */
__BEGIN_DECLS
void	(*signal(int, void (*)(int)))(int);
__END_DECLS
#endif /* !_KERNEL */
#endif	/* !_SYS_SIGNAL_H_ */
