/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)signal.h	8.4 (Berkeley) 5/4/95
 * $FreeBSD$
 */

#ifndef _SYS_SIGNAL_H_
#define	_SYS_SIGNAL_H_

#include <sys/cdefs.h>
#include <sys/_types.h>
#include <sys/_sigset.h>

#include <machine/_limits.h>	/* __MINSIGSTKSZ */
#include <machine/signal.h>	/* sig_atomic_t; trap codes; sigcontext */

#if __POSIX_VISIBLE >= 200809

#include <sys/_pthreadtypes.h>
#include <sys/_timespec.h>

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
typedef	__uid_t		uid_t;
#define	_UID_T_DECLARED
#endif

#endif /* __POSIX_VISIBLE >= 200809 */

/*
 * System defined signals.
 */
#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SIGHUP		1	/* hangup */
#endif
#define	SIGINT		2	/* interrupt */
#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SIGQUIT		3	/* quit */
#endif
#define	SIGILL		4	/* illegal instr. (not reset when caught) */
#if __XSI_VISIBLE
#define	SIGTRAP		5	/* trace trap (not reset when caught) */
#endif
#define	SIGABRT		6	/* abort() */
#if __BSD_VISIBLE
#define	SIGIOT		SIGABRT	/* compatibility */
#define	SIGEMT		7	/* EMT instruction */
#endif
#define	SIGFPE		8	/* floating point exception */
#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SIGKILL		9	/* kill (cannot be caught or ignored) */
#endif
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
#define	SIGBUS		10	/* bus error */
#endif
#define	SIGSEGV		11	/* segmentation violation */
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
#define	SIGSYS		12	/* non-existent system call invoked */
#endif
#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SIGPIPE		13	/* write on a pipe with no one to read it */
#define	SIGALRM		14	/* alarm clock */
#endif
#define	SIGTERM		15	/* software termination signal from kill */
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
#define	SIGURG		16	/* urgent condition on IO channel */
#endif
#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SIGSTOP		17	/* sendable stop signal not from tty */
#define	SIGTSTP		18	/* stop signal from tty */
#define	SIGCONT		19	/* continue a stopped process */
#define	SIGCHLD		20	/* to parent on child stop or exit */
#define	SIGTTIN		21	/* to readers pgrp upon background tty read */
#define	SIGTTOU		22	/* like TTIN if (tp->t_local&LTOSTOP) */
#endif
#if __BSD_VISIBLE
#define	SIGIO		23	/* input/output possible signal */
#endif
#if __XSI_VISIBLE
#define	SIGXCPU		24	/* exceeded CPU time limit */
#define	SIGXFSZ		25	/* exceeded file size limit */
#define	SIGVTALRM	26	/* virtual time alarm */
#define	SIGPROF		27	/* profiling time alarm */
#endif
#if __BSD_VISIBLE
#define	SIGWINCH	28	/* window size changes */
#define	SIGINFO		29	/* information request */
#endif
#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SIGUSR1		30	/* user defined signal 1 */
#define	SIGUSR2		31	/* user defined signal 2 */
#endif
#if __BSD_VISIBLE
#define	SIGTHR		32	/* reserved by thread library. */
#define	SIGLWP		SIGTHR
#define	SIGLIBRT	33	/* reserved by real-time library. */
#endif

#define	SIGRTMIN	65
#define	SIGRTMAX	126

#define	SIG_DFL		((__sighandler_t *)0)
#define	SIG_IGN		((__sighandler_t *)1)
#define	SIG_ERR		((__sighandler_t *)-1)
/* #define	SIG_CATCH	((__sighandler_t *)2) See signalvar.h */
#define SIG_HOLD        ((__sighandler_t *)3)

/*
 * Type of a signal handling function.
 *
 * Language spec sez signal handlers take exactly one arg, even though we
 * actually supply three.  Ugh!
 *
 * We don't try to hide the difference by leaving out the args because
 * that would cause warnings about conformant programs.  Nonconformant
 * programs can avoid the warnings by casting to (__sighandler_t *) or
 * sig_t before calling signal() or assigning to sa_handler or sv_handler.
 *
 * The kernel should reverse the cast before calling the function.  It
 * has no way to do this, but on most machines 1-arg and 3-arg functions
 * have the same calling protocol so there is no problem in practice.
 * A bit in sa_flags could be used to specify the number of args.
 */
typedef	void __sighandler_t(int);

#if __POSIX_VISIBLE || __XSI_VISIBLE
#ifndef _SIGSET_T_DECLARED
#define	_SIGSET_T_DECLARED
typedef	__sigset_t	sigset_t;
#endif
#endif

#if __POSIX_VISIBLE >= 199309 || __XSI_VISIBLE >= 500
union sigval {
	/* Members as suggested by Annex C of POSIX 1003.1b. */
	int	sival_int;
	void	*sival_ptr;
	/* 6.0 compatibility */
	int     sigval_int;
	void    *sigval_ptr;
};

#if defined(_WANT_LWPINFO32) || (defined(_KERNEL) && defined(__LP64__))
union sigval32 {
	int	sival_int;
	uint32_t sival_ptr;
	/* 6.0 compatibility */
	int	sigval_int;
	uint32_t sigval_ptr;
};
#endif
#endif

#if __POSIX_VISIBLE >= 199309

struct pthread_attr;

struct sigevent {
	int	sigev_notify;		/* Notification type */
	int	sigev_signo;		/* Signal number */
	union sigval sigev_value;	/* Signal value */
	union {
		__lwpid_t	_threadid;
		struct {
			void (*_function)(union sigval);
			struct pthread_attr **_attribute;
		} _sigev_thread;
		unsigned short _kevent_flags;
		long __spare__[8];
	} _sigev_un;
};

#if __BSD_VISIBLE
#define	sigev_notify_kqueue		sigev_signo
#define	sigev_notify_kevent_flags	_sigev_un._kevent_flags
#define	sigev_notify_thread_id		_sigev_un._threadid
#endif
#define	sigev_notify_function		_sigev_un._sigev_thread._function
#define	sigev_notify_attributes		_sigev_un._sigev_thread._attribute

#define	SIGEV_NONE	0		/* No async notification. */
#define	SIGEV_SIGNAL	1		/* Generate a queued signal. */
#define	SIGEV_THREAD	2		/* Call back from another pthread. */
#if __BSD_VISIBLE
#define	SIGEV_KEVENT	3		/* Generate a kevent. */
#define	SIGEV_THREAD_ID	4		/* Send signal to a kernel thread. */
#endif

#endif /* __POSIX_VISIBLE >= 199309 */

#if __POSIX_VISIBLE >= 199309 || __XSI_VISIBLE
typedef	struct __siginfo {
	int	si_signo;		/* signal number */
	int	si_errno;		/* errno association */
	/*
	 * Cause of signal, one of the SI_ macros or signal-specific
	 * values, i.e. one of the FPE_... values for SIGFPE.  This
	 * value is equivalent to the second argument to an old-style
	 * FreeBSD signal handler.
	 */
	int	si_code;		/* signal code */
	__pid_t	si_pid;			/* sending process */
	__uid_t	si_uid;			/* sender's ruid */
	int	si_status;		/* exit value */
	void	*si_addr;		/* faulting instruction */
	union sigval si_value;		/* signal value */
	union	{
		struct {
			int	_trapno;/* machine specific trap code */
		} _fault;
		struct {
			int	_timerid;
			int	_overrun;
		} _timer;
		struct {
			int	_mqd;
		} _mesgq;
		struct {
			long	_band;		/* band event for SIGPOLL */
		} _poll;			/* was this ever used ? */
		struct {
			long	__spare1__;
			int	__spare2__[7];
		} __spare__;
	} _reason;
} siginfo_t;

#define si_trapno	_reason._fault._trapno
#define si_timerid	_reason._timer._timerid
#define si_overrun	_reason._timer._overrun
#define si_mqd		_reason._mesgq._mqd
#define si_band		_reason._poll._band

#if defined(_WANT_LWPINFO32) || (defined(_KERNEL) && defined(__LP64__))
struct siginfo32 {
	int	si_signo;		/* signal number */
	int	si_errno;		/* errno association */
	int	si_code;		/* signal code */
	__pid_t	si_pid;			/* sending process */
	__uid_t	si_uid;			/* sender's ruid */
	int	si_status;		/* exit value */
	uint32_t si_addr;		/* faulting instruction */
	union sigval32 si_value;	/* signal value */
	union	{
		struct {
			int	_trapno;/* machine specific trap code */
		} _fault;
		struct {
			int	_timerid;
			int	_overrun;
		} _timer;
		struct {
			int	_mqd;
		} _mesgq;
		struct {
			int32_t	_band;		/* band event for SIGPOLL */
		} _poll;			/* was this ever used ? */
		struct {
			int32_t	__spare1__;
			int	__spare2__[7];
		} __spare__;
	} _reason;
};
#endif

/** si_code **/
/* codes for SIGILL */
#define ILL_ILLOPC 	1	/* Illegal opcode.			*/
#define ILL_ILLOPN 	2	/* Illegal operand.			*/
#define ILL_ILLADR 	3	/* Illegal addressing mode.		*/
#define ILL_ILLTRP 	4	/* Illegal trap.			*/
#define ILL_PRVOPC 	5	/* Privileged opcode.			*/
#define ILL_PRVREG 	6	/* Privileged register.			*/
#define ILL_COPROC 	7	/* Coprocessor error.			*/
#define ILL_BADSTK 	8	/* Internal stack error.		*/

/* codes for SIGBUS */
#define BUS_ADRALN	1	/* Invalid address alignment.		*/
#define BUS_ADRERR	2	/* Nonexistent physical address.	*/
#define BUS_OBJERR	3	/* Object-specific hardware error.	*/

/* codes for SIGSEGV */
#define SEGV_MAPERR	1	/* Address not mapped to object.	*/
#define SEGV_ACCERR	2	/* Invalid permissions for mapped	*/
				/* object.				*/

/* codes for SIGFPE */
#define FPE_INTOVF	1	/* Integer overflow.			*/
#define FPE_INTDIV	2	/* Integer divide by zero.		*/
#define FPE_FLTDIV	3	/* Floating point divide by zero.	*/
#define FPE_FLTOVF	4	/* Floating point overflow.		*/
#define FPE_FLTUND	5	/* Floating point underflow.		*/
#define FPE_FLTRES	6	/* Floating point inexact result.	*/
#define FPE_FLTINV	7	/* Invalid floating point operation.	*/
#define FPE_FLTSUB	8	/* Subscript out of range.		*/

/* codes for SIGTRAP */
#define TRAP_BRKPT	1	/* Process breakpoint.			*/
#define TRAP_TRACE	2	/* Process trace trap.			*/
#define	TRAP_DTRACE	3	/* DTrace induced trap.			*/
#define	TRAP_CAP	4	/* Capabilities protective trap.	*/

/* codes for SIGCHLD */
#define CLD_EXITED	1	/* Child has exited			*/
#define CLD_KILLED	2	/* Child has terminated abnormally but	*/
				/* did not create a core file		*/
#define CLD_DUMPED	3	/* Child has terminated abnormally and	*/
				/* created a core file			*/
#define CLD_TRAPPED	4	/* Traced child has trapped		*/
#define CLD_STOPPED	5	/* Child has stopped			*/
#define CLD_CONTINUED	6	/* Stopped child has continued		*/

/* codes for SIGPOLL */
#define POLL_IN		1	/* Data input available			*/
#define POLL_OUT	2	/* Output buffers available		*/
#define POLL_MSG	3	/* Input message available		*/
#define POLL_ERR	4	/* I/O Error				*/
#define POLL_PRI	5	/* High priority input available	*/
#define POLL_HUP	6	/* Device disconnected			*/

#endif

#if __POSIX_VISIBLE || __XSI_VISIBLE
struct __siginfo;

/*
 * Signal vector "template" used in sigaction call.
 */
struct sigaction {
	union {
		void    (*__sa_handler)(int);
		void    (*__sa_sigaction)(int, struct __siginfo *, void *);
	} __sigaction_u;		/* signal handler */
	int	sa_flags;		/* see signal options below */
	sigset_t sa_mask;		/* signal mask to apply */
};

#define	sa_handler	__sigaction_u.__sa_handler
#endif

#if __XSI_VISIBLE
/* If SA_SIGINFO is set, sa_sigaction must be used instead of sa_handler. */
#define	sa_sigaction	__sigaction_u.__sa_sigaction
#endif

#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SA_NOCLDSTOP	0x0008	/* do not generate SIGCHLD on child stop */
#endif /* __POSIX_VISIBLE || __XSI_VISIBLE */

#if __XSI_VISIBLE
#define	SA_ONSTACK	0x0001	/* take signal on signal stack */
#define	SA_RESTART	0x0002	/* restart system call on signal return */
#define	SA_RESETHAND	0x0004	/* reset to SIG_DFL when taking signal */
#define	SA_NODEFER	0x0010	/* don't mask the signal we're delivering */
#define	SA_NOCLDWAIT	0x0020	/* don't keep zombies around */
#define	SA_SIGINFO	0x0040	/* signal handler with SA_SIGINFO args */
#endif

#if __BSD_VISIBLE
#define	NSIG		32	/* number of old signals (counting 0) */
#endif

#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SI_NOINFO	0		/* No signal info besides si_signo. */
#define	SI_USER		0x10001		/* Signal sent by kill(). */
#define	SI_QUEUE	0x10002		/* Signal sent by the sigqueue(). */
#define	SI_TIMER	0x10003		/* Signal generated by expiration of */
					/* a timer set by timer_settime(). */
#define	SI_ASYNCIO	0x10004		/* Signal generated by completion of */
					/* an asynchronous I/O request.*/
#define	SI_MESGQ	0x10005		/* Signal generated by arrival of a */
					/* message on an empty message queue. */
#define	SI_KERNEL	0x10006
#define	SI_LWP		0x10007		/* Signal sent by thr_kill */
#endif
#if __BSD_VISIBLE
#define	SI_UNDEFINED	0
#endif

#if __BSD_VISIBLE
typedef	__sighandler_t	*sig_t;	/* type of pointer to a signal function */
typedef	void __siginfohandler_t(int, struct __siginfo *, void *);
#endif

#if __XSI_VISIBLE
#if __BSD_VISIBLE
#define	__stack_t sigaltstack
#endif
typedef	struct __stack_t stack_t;

#define	SS_ONSTACK	0x0001	/* take signal on alternate stack */
#define	SS_DISABLE	0x0004	/* disable taking signals on alternate stack */
#define	MINSIGSTKSZ	__MINSIGSTKSZ		/* minimum stack size */
#define	SIGSTKSZ	(MINSIGSTKSZ + 32768)	/* recommended stack size */
#endif

/*
 * Structure used in sigaltstack call.  Its definition is always
 * needed for __ucontext.  If __BSD_VISIBLE is defined, the structure
 * tag is actually sigaltstack.
 */
struct __stack_t {
	void	*ss_sp;			/* signal stack base */
	__size_t ss_size;		/* signal stack length */
	int	ss_flags;		/* SS_DISABLE and/or SS_ONSTACK */
};

#if __BSD_VISIBLE
/*
 * 4.3 compatibility:
 * Signal vector "template" used in sigvec call.
 */
struct sigvec {
	__sighandler_t *sv_handler;	/* signal handler */
	int	sv_mask;		/* signal mask to apply */
	int	sv_flags;		/* see signal options below */
};

#define	SV_ONSTACK	SA_ONSTACK
#define	SV_INTERRUPT	SA_RESTART	/* same bit, opposite sense */
#define	SV_RESETHAND	SA_RESETHAND
#define	SV_NODEFER	SA_NODEFER
#define	SV_NOCLDSTOP	SA_NOCLDSTOP
#define	SV_SIGINFO	SA_SIGINFO
#define	sv_onstack	sv_flags	/* isn't compatibility wonderful! */
#endif

/* Keep this in one place only */
#if defined(_KERNEL) && defined(COMPAT_43) && \
    !defined(__i386__)
struct osigcontext {
	int _not_used;
};
#endif

#if __XSI_VISIBLE
/*
 * Structure used in sigstack call.
 */
struct sigstack {
	void	*ss_sp;			/* signal stack pointer */
	int	ss_onstack;		/* current status */
};
#endif

#if __BSD_VISIBLE || __POSIX_VISIBLE > 0 && __POSIX_VISIBLE <= 200112
/*
 * Macro for converting signal number to a mask suitable for
 * sigblock().
 */
#define	sigmask(m)	(1 << ((m)-1))
#endif

#if __BSD_VISIBLE
#define	BADSIG		SIG_ERR
#endif

#if __POSIX_VISIBLE || __XSI_VISIBLE
/*
 * Flags for sigprocmask:
 */
#define	SIG_BLOCK	1	/* block specified signal set */
#define	SIG_UNBLOCK	2	/* unblock specified signal set */
#define	SIG_SETMASK	3	/* set specified signal set */
#endif

/*
 * For historical reasons; programs expect signal's return value to be
 * defined by <sys/signal.h>.
 */
__BEGIN_DECLS
__sighandler_t *signal(int, __sighandler_t *);
__END_DECLS

#endif /* !_SYS_SIGNAL_H_ */
