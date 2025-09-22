/*	$OpenBSD: wait.h,v 1.20 2022/12/19 00:22:11 guenther Exp $	*/
/*	$NetBSD: wait.h,v 1.11 1996/04/09 20:55:51 cgd Exp $	*/

/*
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
 */

#ifndef _SYS_WAIT_H_
#define _SYS_WAIT_H_

#include <sys/cdefs.h>
#include <sys/siginfo.h>

/*
 * This file holds definitions relevant to the wait4 system call
 * and the alternate interfaces that use it (wait, wait3, waitpid).
 */

/*
 * Macros to test the exit status returned by wait
 * and extract the relevant values.
 */
#define	_WSTATUS(x)	((x) & 0177)
#define	_WSTOPPED	0177		/* _WSTATUS if process is stopped */
#define	_WCONTINUED	0177777		/* process has continued */
#define WIFSTOPPED(x)	(((x) & 0xff) == _WSTOPPED)
#define WSTOPSIG(x)	(int)(((unsigned)(x) >> 8) & 0xff)
#define WIFSIGNALED(x)	(_WSTATUS(x) != _WSTOPPED && _WSTATUS(x) != 0)
#define WTERMSIG(x)	(_WSTATUS(x))
#define WIFEXITED(x)	(_WSTATUS(x) == 0)
#define WEXITSTATUS(x)	(int)(((unsigned)(x) >> 8) & 0xff)
#define WIFCONTINUED(x)	(((x) & _WCONTINUED) == _WCONTINUED)
#if __XPG_VISIBLE
#define	WCOREFLAG	0200
#define WCOREDUMP(x)	((x) & WCOREFLAG)

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
 * about them is returned. WNOWAIT only requests information about zombie,
 * leaving the proc around, available for later waits.
 */
#define WNOHANG		0x01	/* don't hang in wait */
#define WUNTRACED	0x02	/* report stopped-by-signal processes */
#define WCONTINUED	0x08	/* report job control continued processes */
#if __POSIX_VISIBLE >= 200809 || __XPG_VISIBLE >= 420
#define WEXITED		0x04	/* report exited processes */
#define WSTOPPED	WUNTRACED
#define WNOWAIT		0x10	/* poll only */
#define WTRAPPED	0x20	/* report stopped-by-tracing processes */

typedef enum {
	P_ALL,
	P_PGID,
	P_PID
} idtype_t;
#endif

#if __BSD_VISIBLE
/*
 * Tokens for special values of the "pid" parameter to wait4.
 */
#define	WAIT_ANY	(-1)	/* any process */
#define	WAIT_MYPGRP	0	/* any process in my process group */
#endif /* __BSD_VISIBLE */

#ifndef _KERNEL
#include <sys/types.h>

__BEGIN_DECLS
struct rusage;	/* forward declaration */

pid_t	wait(int *);
pid_t	waitpid(pid_t, int *, int);
#if __POSIX_VISIBLE >= 200809 || __XPG_VISIBLE >= 420
int	waitid(idtype_t, id_t, siginfo_t *, int);
#endif
#if __BSD_VISIBLE
pid_t	wait3(int *, int, struct rusage *);
pid_t	wait4(pid_t, int *, int, struct rusage *);
#endif /* __BSD_VISIBLE */
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_SYS_WAIT_H_ */
