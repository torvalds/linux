/*	$OpenBSD: siginfo.h,v 1.14 2024/02/21 15:53:07 deraadt Exp $	*/

/*
 * Copyright (c) 1997 Theo de Raadt
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

#ifndef _SYS_SIGINFO_H
#define _SYS_SIGINFO_H

#include <sys/cdefs.h>
 
union sigval {
	int	sival_int;	/* integer value */
	void	*sival_ptr;	/* pointer value */
};
 
/*
 * Negative signal codes are reserved for future use for
 * user generated signals.
 */
#define SI_FROMUSER(sip)	((sip)->si_code <= 0)
#define SI_FROMKERNEL(sip)	((sip)->si_code > 0)
 
#define SI_NOINFO	32767	/* no signal information */
#define SI_USER		0	/* user generated signal via kill() */
#define SI_LWP		(-1)	/* user generated signal via lwp_kill()*/
#define SI_QUEUE	(-2)	/* user generated signal via sigqueue()*/
#define SI_TIMER	(-3)	/* from timer expiration */

#if __POSIX_VISIBLE >= 199309 || __XPG_VISIBLE
/*
 * The machine dependent signal codes (SIGILL, SIGFPE,
 * SIGSEGV, and SIGBUS)
 */
#define ILL_ILLOPC	1	/* illegal opcode */
#define ILL_ILLOPN	2	/* illegal operand */
#define ILL_ILLADR	3	/* illegal addressing mode */
#define ILL_ILLTRP	4	/* illegal trap */
#define ILL_PRVOPC	5	/* privileged opcode */
#define ILL_PRVREG	6	/* privileged register */
#define ILL_COPROC	7	/* co-processor */
#define ILL_BADSTK	8	/* bad stack */
#define ILL_BTCFI	9	/* IBT missing on indirect call */
#define NSIGILL		9

#define EMT_TAGOVF	1	/* tag overflow */
#define NSIGEMT		1

#define FPE_INTDIV	1	/* integer divide by zero */
#define FPE_INTOVF	2	/* integer overflow */
#define FPE_FLTDIV	3	/* floating point divide by zero */
#define FPE_FLTOVF	4	/* floating point overflow */
#define FPE_FLTUND	5	/* floating point underflow */
#define FPE_FLTRES	6	/* floating point inexact result */
#define FPE_FLTINV	7	/* invalid floating point operation */
#define FPE_FLTSUB	8	/* subscript out of range */
#define NSIGFPE		8

#define SEGV_MAPERR	1	/* address not mapped to object */
#define SEGV_ACCERR	2	/* invalid permissions */
#define NSIGSEGV	2

#define BUS_ADRALN	1	/* invalid address alignment */
#define BUS_ADRERR	2	/* non-existent physical address */
#define BUS_OBJERR	3	/* object specific hardware error */
#define NSIGBUS		3

#endif /* __POSIX_VISIBLE >= 199309 || __XPG_VISIBLE */

/*
 * SIGTRAP signal codes
 */
#define TRAP_BRKPT	1	/* breakpoint trap */
#define TRAP_TRACE	2	/* trace trap */
#define NSIGTRAP	2

/*
 * SIGCHLD signal codes
 */
#define CLD_EXITED	1	/* child has exited */
#define CLD_KILLED	2	/* child was killed */
#define CLD_DUMPED	3	/* child has coredumped */
#define CLD_TRAPPED	4	/* traced child has stopped */
#define CLD_STOPPED	5	/* child has stopped on signal */
#define CLD_CONTINUED	6	/* stopped child has continued */
#define NSIGCLD		6

#if 0
/*
 * SIGPOLL signal codes - not supported
 */
#define POLL_IN		1	/* input available */
#define POLL_OUT	2	/* output possible */
#define POLL_MSG	3	/* message available */
#define POLL_ERR	4	/* I/O error */
#define POLL_PRI	5	/* high priority input available */
#define POLL_HUP	6	/* device disconnected */
#define NSIGPOLL	6

/*
 * SIGPROF signal codes - not supported
 */
#define PROF_SIG	1	/* have to set code non-zero */
#define NSIGPROF	1
#endif

#define SI_MAXSZ	128
#define SI_PAD		((SI_MAXSZ / sizeof (int)) - 3)

#include <sys/time.h>

typedef struct {
	int	si_signo;			/* signal from signal.h */
	int	si_code;			/* code from above */
	int	si_errno;			/* error from errno.h */
	union {
		int	_pad[SI_PAD];		/* for future growth */
		struct {			/* kill(), SIGCHLD */
			pid_t	_pid;		/* process ID */
			uid_t	_uid;
			union {
				struct {
					union sigval	_value;
				} _kill;
				struct {
					clock_t	_utime;
					clock_t	_stime;
					int	_status;
				} _cld;
			} _pdata;
		} _proc;
		struct {	/* SIGSEGV, SIGBUS, SIGILL and SIGFPE */
			void	*_addr;		/* faulting address */
			int	_trapno;	/* illegal trap number */
		} _fault;
#if 0
		struct {			/* SIGPOLL, SIGXFSZ */
			/* fd not currently available for SIGPOLL */
			int	_fd;		/* file descriptor */
			long	_band;
		} _file;
		struct {			/* SIGPROF */
			caddr_t _faddr;		/* last fault address */
			timespec _tstamp;	/* real time stamp */
			short	_syscall;	/* current syscall */
			char	_nsysarg;	/* number of arguments */
			char	_fault;		/* last fault type */
			long	_sysarg[8];	/* syscall arguments */
			long	_mstate[17];	/* exactly fills struct*/
		} _prof;
#endif
	} _data;
} siginfo_t;

#define si_pid		_data._proc._pid
#define si_uid		_data._proc._uid

#define si_status	_data._proc._pdata._cld._status
#define si_stime	_data._proc._pdata._cld._stime
#define si_utime	_data._proc._pdata._cld._utime
#define si_value	_data._proc._pdata._kill._value
#define si_addr		_data._fault._addr
#define si_trapno	_data._fault._trapno
#define si_fd		_data._file._fd
#define si_band		_data._file._band

#define si_tstamp	_data._prof._tstamp
#define si_syscall	_data._prof._syscall
#define si_nsysarg	_data._prof._nsysarg
#define si_sysarg	_data._prof._sysarg
#define si_fault	_data._prof._fault
#define si_faddr	_data._prof._faddr
#define si_mstate	_data._prof._mstate

#if defined(_KERNEL)
void	initsiginfo(siginfo_t *, int, u_long, int, union sigval);
#endif

#endif	/* _SYS_SIGINFO_H */
