/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993, 1994
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
 *	@(#)wait.h	8.2 (Berkeley) 7/10/94
 * $FreeBSD$
 */

#ifndef _SYS_WAIT_H_
#define _SYS_WAIT_H_

#include <sys/cdefs.h>

/*
 * This file holds definitions relevant to the wait4 system call and the
 * alternate interfaces that use it (wait, wait3, waitpid).
 */

/*
 * Macros to test the exit status returned by wait and extract the relevant
 * values.
 */
#if __BSD_VISIBLE
#define	WCOREFLAG	0200
#endif
#define	_W_INT(i)	(i)

#define	_WSTATUS(x)	(_W_INT(x) & 0177)
#define	_WSTOPPED	0177		/* _WSTATUS if process is stopped */
#define	WIFSTOPPED(x)	(_WSTATUS(x) == _WSTOPPED)
#define	WSTOPSIG(x)	(_W_INT(x) >> 8)
#define	WIFSIGNALED(x)	(_WSTATUS(x) != _WSTOPPED && _WSTATUS(x) != 0 && (x) != 0x13)
#define	WTERMSIG(x)	(_WSTATUS(x))
#define	WIFEXITED(x)	(_WSTATUS(x) == 0)
#define	WEXITSTATUS(x)	(_W_INT(x) >> 8)
#define	WIFCONTINUED(x)	(x == 0x13)	/* 0x13 == SIGCONT */
#if __BSD_VISIBLE
#define	WCOREDUMP(x)	(_W_INT(x) & WCOREFLAG)

#define	W_EXITCODE(ret, sig)	((ret) << 8 | (sig))
#define	W_STOPCODE(sig)		((sig) << 8 | _WSTOPPED)
#endif

/*
 * Option bits for the third argument of wait4.  WNOHANG causes the
 * wait to not hang if there are no stopped or terminated processes, rather
 * returning an error indication in this case (pid==0).  WUNTRACED
 * indicates that the caller should receive status about untraced children
 * which stop due to signals.  If children are stopped and a wait without
 * this option is done, it is as though they were still running... nothing
 * about them is returned. WNOWAIT only request information about zombie,
 * leaving the proc around, available for later waits.
 */
#define	WNOHANG		1	/* Don't hang in wait. */
#define	WUNTRACED	2	/* Tell about stopped, untraced children. */
#define	WSTOPPED	WUNTRACED   /* SUS compatibility */
#define	WCONTINUED	4	/* Report a job control continued process. */
#define	WNOWAIT		8	/* Poll only. Don't delete the proc entry. */
#define	WEXITED		16	/* Wait for exited processes. */
#define	WTRAPPED	32	/* Wait for a process to hit a trap or
				   a breakpoint. */

#if __BSD_VISIBLE
#define	WLINUXCLONE 0x80000000	/* Wait for kthread spawned from linux_clone. */
#endif

#ifndef _IDTYPE_T_DECLARED
typedef enum
#if __BSD_VISIBLE
	idtype		/* pollutes XPG4.2 namespace */
#endif
		{
	/*
	 * These names were mostly lifted from Solaris source code and
	 * still use Solaris style naming to avoid breaking any
	 * OpenSolaris code which has been ported to FreeBSD.  There
	 * is no clear FreeBSD counterpart for all of the names, but
	 * some have a clear correspondence to FreeBSD entities.
	 *
	 * The numerical values are kept synchronized with the Solaris
	 * values.
	 */
	P_PID,			/* A process identifier. */
	P_PPID,			/* A parent process identifier.	*/
	P_PGID,			/* A process group identifier. */
	P_SID,			/* A session identifier. */
	P_CID,			/* A scheduling class identifier. */
	P_UID,			/* A user identifier. */
	P_GID,			/* A group identifier. */
	P_ALL,			/* All processes. */
	P_LWPID,		/* An LWP identifier. */
	P_TASKID,		/* A task identifier. */
	P_PROJID,		/* A project identifier. */
	P_POOLID,		/* A pool identifier. */
	P_JAILID,		/* A zone identifier. */
	P_CTID,			/* A (process) contract identifier. */
	P_CPUID,		/* CPU identifier. */
	P_PSETID		/* Processor set identifier. */
} idtype_t;			/* The type of id_t we are using. */

#if __BSD_VISIBLE
#define	P_ZONEID	P_JAILID
#endif
#define	_IDTYPE_T_DECLARED
#endif

/*
 * Tokens for special values of the "pid" parameter to wait4.
 * Extended struct __wrusage to collect rusage for both the target
 * process and its children within one wait6() call.
 */
#if __BSD_VISIBLE
#define	WAIT_ANY	(-1)	/* any process */
#define	WAIT_MYPGRP	0	/* any process in my process group */
#endif /* __BSD_VISIBLE */

#if defined(_KERNEL) || defined(_WANT_KW_EXITCODE)

/*
 * Clamp the return code to the low 8 bits from full 32 bit value.
 * Should be used in kernel to construct the wait(2)-compatible process
 * status to usermode.
 */
#define	KW_EXITCODE(ret, sig)	W_EXITCODE((ret) & 0xff, (sig))

#endif	/* _KERNEL || _WANT_KW_EXITCODE */

#ifndef _KERNEL

#include <sys/types.h>

__BEGIN_DECLS
struct __siginfo;
pid_t	wait(int *);
pid_t	waitpid(pid_t, int *, int);
#if __POSIX_VISIBLE >= 200112
int	waitid(idtype_t, id_t, struct __siginfo *, int);
#endif
#if __BSD_VISIBLE
struct rusage;
struct __wrusage;
pid_t	wait3(int *, int, struct rusage *);
pid_t	wait4(pid_t, int *, int, struct rusage *);
pid_t	wait6(idtype_t, id_t, int *, int, struct __wrusage *,
	    struct __siginfo *);
#endif
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_SYS_WAIT_H_ */
