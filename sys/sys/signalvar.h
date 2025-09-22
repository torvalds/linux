/*	$OpenBSD: signalvar.h,v 1.58 2025/03/10 09:28:57 claudio Exp $	*/
/*	$NetBSD: signalvar.h,v 1.17 1996/04/22 01:23:31 christos Exp $	*/

/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)signalvar.h	8.3 (Berkeley) 1/4/94
 */

#ifndef	_SYS_SIGNALVAR_H_		/* tmp for user.h */
#define	_SYS_SIGNALVAR_H_

/*
 * Kernel signal definitions and data structures,
 * not exported to user programs.
 */

/*
 * Process signal actions and state, needed only within the process
 * (not necessarily resident).
 *
 * Locks used to protect struct members in struct sigacts:
 *	a	atomic operations
 *	m	this process' `ps_mtx'
 */
struct	sigacts {
	sig_t	ps_sigact[NSIG];	/* [m] disposition of signals */
	sigset_t ps_catchmask[NSIG];	/* [m] signals to be blocked */
	sigset_t ps_sigonstack;		/* [m] signals to take on sigstack */
	sigset_t ps_sigintr;		/* [m] signals interrupt syscalls */
	sigset_t ps_sigreset;		/* [m] signals that reset when caught */
	sigset_t ps_siginfo;		/* [m] signals that provide siginfo */
	sigset_t ps_sigignore;		/* [m] signals being ignored */
	sigset_t ps_sigcatch;		/* [m] signals being caught by user */
	int	ps_sigflags;		/* [a] signal flags, below */
};

/* signal flags */
#define	SAS_NOCLDSTOP	0x01	/* No SIGCHLD when children stop. */
#define	SAS_NOCLDWAIT	0x02	/* No zombies if child dies */

/* additional signal action values, used only temporarily/internally */
#define	SIG_CATCH	(void (*)(int))2
#define	SIG_HOLD	(void (*)(int))3

/*
 * Check if process p has an unmasked signal pending.
 * Return mask of pending signals.
 */
#define SIGPENDING(p)							\
	(((p)->p_siglist | (p)->p_p->ps_siglist) & ~(p)->p_sigmask)

/*
 * Signal properties and actions.
 */
#define	SA_KILL		0x01		/* terminates process by default */
#define	SA_CORE		0x02		/* ditto and coredumps */
#define	SA_STOP		0x04		/* suspend process */
#define	SA_TTYSTOP	0x08		/* ditto, from tty */
#define	SA_IGNORE	0x10		/* ignore by default */
#define	SA_CONT		0x20		/* continue if suspended */
#define	SA_CANTMASK	0x40		/* non-maskable, catchable */

#define	sigcantmask	(sigmask(SIGKILL) | sigmask(SIGSTOP))

#ifdef _KERNEL
enum signal_type { SPROCESS, STHREAD };

struct sigio_ref;

struct sigctx {
	sig_t		sig_action;
	sigset_t	sig_catchmask;
	int		sig_onstack;
	int		sig_intr;
	int		sig_reset;
	int		sig_info;
	int		sig_ignore;
	int		sig_catch;
	int		sig_stop;
};

/*
 * Machine-independent functions:
 */
int	coredump(struct proc *p);
void	execsigs(struct proc *p);
int	cursig(struct proc *p, struct sigctx *, int);
void	pgsigio(struct sigio_ref *sir, int sig, int checkctty);
void	pgsignal(struct pgrp *pgrp, int sig, int checkctty);
void	psignal(struct proc *p, int sig);
void	ptsignal(struct proc *p, int sig, enum signal_type type);
void	prsignal(struct process *pr, int sig);
void	trapsignal(struct proc *p, int sig, u_long code, int type,
	    union sigval val);
__dead void sigexit(struct proc *, int);
void	sigabort(struct proc *);
int	sigismasked(struct proc *, int);
int	sigonstack(size_t);
int	killpg1(struct proc *, int, int, int);

void	signal_init(void);

void	sigstkinit(struct sigaltstack *);
struct sigacts	*sigactsinit(struct process *);
void	sigactsfree(struct sigacts *);
void	siginit(struct sigacts *);

/*
 * Machine-dependent functions:
 */
int	sendsig(sig_t _catcher, int _sig, sigset_t _mask, const siginfo_t *_si,
	    int _info, int _onstack);
#endif	/* _KERNEL */
#endif	/* !_SYS_SIGNALVAR_H_ */
