/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)signalvar.h	8.6 (Berkeley) 2/19/95
 * $FreeBSD$
 */

#ifndef _SYS_SIGNALVAR_H_
#define	_SYS_SIGNALVAR_H_

#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/signal.h>

/*
 * Kernel signal definitions and data structures.
 */

/*
 * Logical process signal actions and state, needed only within the process
 * The mapping between sigacts and proc structures is 1:1 except for rfork()
 * processes masquerading as threads which use one structure for the whole
 * group.  All members are locked by the included mutex.  The reference count
 * and mutex must be last for the bcopy in sigacts_copy() to work.
 */
struct sigacts {
	sig_t	ps_sigact[_SIG_MAXSIG];	/* Disposition of signals. */
	sigset_t ps_catchmask[_SIG_MAXSIG];	/* Signals to be blocked. */
	sigset_t ps_sigonstack;		/* Signals to take on sigstack. */
	sigset_t ps_sigintr;		/* Signals that interrupt syscalls. */
	sigset_t ps_sigreset;		/* Signals that reset when caught. */
	sigset_t ps_signodefer;		/* Signals not masked while handled. */
	sigset_t ps_siginfo;		/* Signals that want SA_SIGINFO args. */
	sigset_t ps_sigignore;		/* Signals being ignored. */
	sigset_t ps_sigcatch;		/* Signals being caught by user. */
	sigset_t ps_freebsd4;		/* Signals using freebsd4 ucontext. */
	sigset_t ps_osigset;		/* Signals using <= 3.x osigset_t. */
	sigset_t ps_usertramp;		/* SunOS compat; libc sigtramp. XXX */
	int	ps_flag;
	u_int	ps_refcnt;
	struct mtx ps_mtx;
};

#define	PS_NOCLDWAIT	0x0001	/* No zombies if child dies */
#define	PS_NOCLDSTOP	0x0002	/* No SIGCHLD when children stop. */
#define	PS_CLDSIGIGN	0x0004	/* The SIGCHLD handler is SIG_IGN. */

#ifdef _KERNEL

#ifdef COMPAT_43
typedef struct {
	struct osigcontext si_sc;
	int		si_signo;
	int		si_code;
	union sigval	si_value;
} osiginfo_t;

struct osigaction {
	union {
		void    (*__sa_handler)(int);
		void    (*__sa_sigaction)(int, osiginfo_t *, void *);
	} __sigaction_u;		/* signal handler */
	osigset_t	sa_mask;	/* signal mask to apply */
	int		sa_flags;	/* see signal options below */
};

typedef void __osiginfohandler_t(int, osiginfo_t *, void *);
#endif /* COMPAT_43 */

/* additional signal action values, used only temporarily/internally */
#define	SIG_CATCH	((__sighandler_t *)2)
/* #define SIG_HOLD        ((__sighandler_t *)3) See signal.h */

/*
 * get signal action for process and signal; currently only for current process
 */
#define	SIGACTION(p, sig)	(p->p_sigacts->ps_sigact[_SIG_IDX(sig)])

#endif /* _KERNEL */

/*
 * sigset_t manipulation macros.
 */
#define	SIGADDSET(set, signo)						\
	((set).__bits[_SIG_WORD(signo)] |= _SIG_BIT(signo))

#define	SIGDELSET(set, signo)						\
	((set).__bits[_SIG_WORD(signo)] &= ~_SIG_BIT(signo))

#define	SIGEMPTYSET(set)						\
	do {								\
		int __i;						\
		for (__i = 0; __i < _SIG_WORDS; __i++)			\
			(set).__bits[__i] = 0;				\
	} while (0)

#define	SIGFILLSET(set)							\
	do {								\
		int __i;						\
		for (__i = 0; __i < _SIG_WORDS; __i++)			\
			(set).__bits[__i] = ~0U;			\
	} while (0)

#define	SIGISMEMBER(set, signo)						\
	((set).__bits[_SIG_WORD(signo)] & _SIG_BIT(signo))

#define	SIGISEMPTY(set)		(__sigisempty(&(set)))
#define	SIGNOTEMPTY(set)	(!__sigisempty(&(set)))

#define	SIGSETEQ(set1, set2)	(__sigseteq(&(set1), &(set2)))
#define	SIGSETNEQ(set1, set2)	(!__sigseteq(&(set1), &(set2)))

#define	SIGSETOR(set1, set2)						\
	do {								\
		int __i;						\
		for (__i = 0; __i < _SIG_WORDS; __i++)			\
			(set1).__bits[__i] |= (set2).__bits[__i];	\
	} while (0)

#define	SIGSETAND(set1, set2)						\
	do {								\
		int __i;						\
		for (__i = 0; __i < _SIG_WORDS; __i++)			\
			(set1).__bits[__i] &= (set2).__bits[__i];	\
	} while (0)

#define	SIGSETNAND(set1, set2)						\
	do {								\
		int __i;						\
		for (__i = 0; __i < _SIG_WORDS; __i++)			\
			(set1).__bits[__i] &= ~(set2).__bits[__i];	\
	} while (0)

#define	SIGSETLO(set1, set2)	((set1).__bits[0] = (set2).__bits[0])
#define	SIGSETOLD(set, oset)	((set).__bits[0] = (oset))

#define	SIG_CANTMASK(set)						\
	SIGDELSET(set, SIGKILL), SIGDELSET(set, SIGSTOP)

#define	SIG_STOPSIGMASK(set)						\
	SIGDELSET(set, SIGSTOP), SIGDELSET(set, SIGTSTP),		\
	SIGDELSET(set, SIGTTIN), SIGDELSET(set, SIGTTOU)

#define	SIG_CONTSIGMASK(set)						\
	SIGDELSET(set, SIGCONT)

#define	sigcantmask	(sigmask(SIGKILL) | sigmask(SIGSTOP))

#define	SIG2OSIG(sig, osig)	(osig = (sig).__bits[0])
#define	OSIG2SIG(osig, sig)	SIGEMPTYSET(sig); (sig).__bits[0] = osig

static __inline int
__sigisempty(sigset_t *set)
{
	int i;

	for (i = 0; i < _SIG_WORDS; i++) {
		if (set->__bits[i])
			return (0);
	}
	return (1);
}

static __inline int
__sigseteq(sigset_t *set1, sigset_t *set2)
{
	int i;

	for (i = 0; i < _SIG_WORDS; i++) {
		if (set1->__bits[i] != set2->__bits[i])
			return (0);
	}
	return (1);
}

#ifdef COMPAT_FREEBSD6
struct osigevent {
	int	sigev_notify;		/* Notification type */
	union {
		int	__sigev_signo;	/* Signal number */
		int	__sigev_notify_kqueue;
	} __sigev_u;
	union sigval sigev_value;	/* Signal value */
};
#endif

typedef struct ksiginfo {
	TAILQ_ENTRY(ksiginfo)	ksi_link;
	siginfo_t		ksi_info;
	int			ksi_flags;
	struct sigqueue		*ksi_sigq;
} ksiginfo_t;

#define	ksi_signo	ksi_info.si_signo
#define	ksi_errno	ksi_info.si_errno
#define	ksi_code	ksi_info.si_code
#define	ksi_pid		ksi_info.si_pid
#define	ksi_uid		ksi_info.si_uid
#define	ksi_status      ksi_info.si_status
#define	ksi_addr        ksi_info.si_addr
#define	ksi_value	ksi_info.si_value
#define	ksi_band	ksi_info.si_band
#define	ksi_trapno	ksi_info.si_trapno
#define	ksi_overrun	ksi_info.si_overrun
#define	ksi_timerid	ksi_info.si_timerid
#define	ksi_mqd		ksi_info.si_mqd

/* bits for ksi_flags */
#define	KSI_TRAP	0x01	/* Generated by trap. */
#define	KSI_EXT		0x02	/* Externally managed ksi. */
#define	KSI_INS		0x04	/* Directly insert ksi, not the copy */
#define	KSI_SIGQ	0x08	/* Generated by sigqueue, might ret EAGAIN. */
#define	KSI_HEAD	0x10	/* Insert into head, not tail. */
#define	KSI_PTRACE	0x20	/* Generated by ptrace. */
#define	KSI_COPYMASK	(KSI_TRAP | KSI_SIGQ | KSI_PTRACE)

#define	KSI_ONQ(ksi)	((ksi)->ksi_sigq != NULL)

typedef struct sigqueue {
	sigset_t	sq_signals;	/* All pending signals. */
	sigset_t	sq_kill;	/* Legacy depth 1 queue. */
	sigset_t	sq_ptrace;	/* Depth 1 queue for ptrace(2). */
	TAILQ_HEAD(, ksiginfo)	sq_list;/* Queued signal info. */
	struct proc	*sq_proc;
	int		sq_flags;
} sigqueue_t;

/* Flags for ksi_flags */
#define	SQ_INIT	0x01

#ifdef _KERNEL

/* Return nonzero if process p has an unmasked pending signal. */
#define	SIGPENDING(td)							\
	((!SIGISEMPTY((td)->td_siglist) &&				\
	    !sigsetmasked(&(td)->td_siglist, &(td)->td_sigmask)) ||	\
	 (!SIGISEMPTY((td)->td_proc->p_siglist) &&			\
	    !sigsetmasked(&(td)->td_proc->p_siglist, &(td)->td_sigmask)))
/*
 * Return the value of the pseudo-expression ((*set & ~*mask) != 0).  This
 * is an optimized version of SIGISEMPTY() on a temporary variable
 * containing SIGSETNAND(*set, *mask).
 */
static __inline int
sigsetmasked(sigset_t *set, sigset_t *mask)
{
	int i;

	for (i = 0; i < _SIG_WORDS; i++) {
		if (set->__bits[i] & ~mask->__bits[i])
			return (0);
	}
	return (1);
}

#define	ksiginfo_init(ksi)			\
do {						\
	bzero(ksi, sizeof(ksiginfo_t));		\
} while(0)

#define	ksiginfo_init_trap(ksi)			\
do {						\
	ksiginfo_t *kp = ksi;			\
	bzero(kp, sizeof(ksiginfo_t));		\
	kp->ksi_flags |= KSI_TRAP;		\
} while(0)

static __inline void
ksiginfo_copy(ksiginfo_t *src, ksiginfo_t *dst)
{
	(dst)->ksi_info = src->ksi_info;
	(dst)->ksi_flags = (src->ksi_flags & KSI_COPYMASK);
}

static __inline void
ksiginfo_set_sigev(ksiginfo_t *dst, struct sigevent *sigev)
{
	dst->ksi_signo = sigev->sigev_signo;
	dst->ksi_value = sigev->sigev_value;
}

struct pgrp;
struct proc;
struct sigio;
struct thread;

/*
 * Lock the pointers for a sigio object in the underlying objects of
 * a file descriptor.
 */
#define	SIGIO_LOCK()	mtx_lock(&sigio_lock)
#define	SIGIO_TRYLOCK()	mtx_trylock(&sigio_lock)
#define	SIGIO_UNLOCK()	mtx_unlock(&sigio_lock)
#define	SIGIO_LOCKED()	mtx_owned(&sigio_lock)
#define	SIGIO_ASSERT(type)	mtx_assert(&sigio_lock, type)

extern struct mtx	sigio_lock;

/* Flags for kern_sigprocmask(). */
#define	SIGPROCMASK_OLD		0x0001
#define	SIGPROCMASK_PROC_LOCKED	0x0002
#define	SIGPROCMASK_PS_LOCKED	0x0004

/*
 * Modes for sigdeferstop().  Manages behaviour of
 * thread_suspend_check() in the region delimited by
 * sigdeferstop()/sigallowstop().  Must be restored to
 * SIGDEFERSTOP_OFF before returning to userspace.
 */
#define	SIGDEFERSTOP_NOP	0 /* continue doing whatever is done now */
#define	SIGDEFERSTOP_OFF	1 /* stop ignoring STOPs */
#define	SIGDEFERSTOP_SILENT	2 /* silently ignore STOPs */
#define	SIGDEFERSTOP_EINTR	3 /* ignore STOPs, return EINTR */
#define	SIGDEFERSTOP_ERESTART	4 /* ignore STOPs, return ERESTART */

#define	SIGDEFERSTOP_VAL_NCHG	(-1) /* placeholder indicating no state change */
int	sigdeferstop_impl(int mode);
void	sigallowstop_impl(int prev);

static inline int
sigdeferstop(int mode)
{

	if (__predict_true(mode == SIGDEFERSTOP_NOP))
		return (SIGDEFERSTOP_VAL_NCHG);
	return (sigdeferstop_impl(mode));
}

static inline void
sigallowstop(int prev)
{

	if (__predict_true(prev == SIGDEFERSTOP_VAL_NCHG))
		return;
	sigallowstop_impl(prev);
}

int	cursig(struct thread *td);
void	execsigs(struct proc *p);
void	gsignal(int pgid, int sig, ksiginfo_t *ksi);
void	killproc(struct proc *p, char *why);
ksiginfo_t * ksiginfo_alloc(int wait);
void	ksiginfo_free(ksiginfo_t *ksi);
int	pksignal(struct proc *p, int sig, ksiginfo_t *ksi);
void	pgsigio(struct sigio **sigiop, int sig, int checkctty);
void	pgsignal(struct pgrp *pgrp, int sig, int checkctty, ksiginfo_t *ksi);
int	postsig(int sig);
void	kern_psignal(struct proc *p, int sig);
int	ptracestop(struct thread *td, int sig, ksiginfo_t *si);
void	sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *retmask);
struct sigacts *sigacts_alloc(void);
void	sigacts_copy(struct sigacts *dest, struct sigacts *src);
void	sigacts_free(struct sigacts *ps);
struct sigacts *sigacts_hold(struct sigacts *ps);
int	sigacts_shared(struct sigacts *ps);
void	sigexit(struct thread *td, int sig) __dead2;
int	sigev_findtd(struct proc *p, struct sigevent *sigev, struct thread **);
int	sig_ffs(sigset_t *set);
void	siginit(struct proc *p);
void	signotify(struct thread *td);
void	sigqueue_delete(struct sigqueue *queue, int sig);
void	sigqueue_delete_proc(struct proc *p, int sig);
void	sigqueue_flush(struct sigqueue *queue);
void	sigqueue_init(struct sigqueue *queue, struct proc *p);
void	sigqueue_take(ksiginfo_t *ksi);
void	tdksignal(struct thread *td, int sig, ksiginfo_t *ksi);
int	tdsendsignal(struct proc *p, struct thread *td, int sig,
	   ksiginfo_t *ksi);
void	tdsigcleanup(struct thread *td);
void	tdsignal(struct thread *td, int sig);
void	trapsignal(struct thread *td, ksiginfo_t *ksi);

#endif /* _KERNEL */

#endif /* !_SYS_SIGNALVAR_H_ */
