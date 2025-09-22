/*	$OpenBSD: signal.h,v 1.26 2018/05/30 13:20:38 bluhm Exp $	*/
/*	$NetBSD: signal.h,v 1.8 1996/02/29 00:04:57 jtc Exp $	*/

/*-
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
 *	@(#)signal.h	8.3 (Berkeley) 3/30/94
 */

#ifndef _USER_SIGNAL_H
#define _USER_SIGNAL_H

#include <sys/signal.h>

#if __BSD_VISIBLE || __POSIX_VISIBLE || __XPG_VISIBLE
#include <sys/types.h>
#endif

__BEGIN_DECLS
#if __BSD_VISIBLE
extern const char *const sys_signame[_NSIG];
extern const char *const sys_siglist[_NSIG];
#endif

int	raise(int);
#if __BSD_VISIBLE || __POSIX_VISIBLE || __XPG_VISIBLE
#if __BSD_VISIBLE || (__XPG_VISIBLE >= 500 && __XPG_VISIBLE < 700)
void	(*bsd_signal(int, void (*)(int)))(int);
#endif
int	kill(pid_t, int);
int	sigaction(int, const struct sigaction *__restrict,
	    struct sigaction *__restrict);
int	sigaddset(sigset_t *, int);
int	sigdelset(sigset_t *, int);
int	sigemptyset(sigset_t *);
int	sigfillset(sigset_t *);
int	sigismember(const sigset_t *, int);
int	sigpending(sigset_t *);
int	sigprocmask(int, const sigset_t *__restrict, sigset_t *__restrict);
#if __POSIX_VISIBLE >= 199506
int	pthread_sigmask(int, const sigset_t *__restrict, sigset_t *__restrict);
#endif
int	sigsuspend(const sigset_t *);

#if !defined(_ANSI_LIBRARY)

extern int *__errno(void);

__only_inline int sigaddset(sigset_t *__set, int __signo)
{
	if (__signo <= 0 || __signo >= _NSIG) {
		*__errno() = 22;		/* EINVAL */
		return -1;
	}
	*__set |= (1U << ((__signo)-1));	/* sigmask(__signo) */
	return (0);
}

__only_inline int sigdelset(sigset_t *__set, int __signo)
{
	if (__signo <= 0 || __signo >= _NSIG) {
		*__errno() = 22;		/* EINVAL */
		return -1;
	}
	*__set &= ~(1U << ((__signo)-1));	/* sigmask(__signo) */
	return (0);
}

__only_inline int sigismember(const sigset_t *__set, int __signo)
{
	if (__signo <= 0 || __signo >= _NSIG) {
		*__errno() = 22;		/* EINVAL */
		return -1;
	}
	return ((*__set & (1U << ((__signo)-1))) != 0);
}

__only_inline int sigemptyset(sigset_t *__set)
{
	*__set = 0;
	return (0);
}

__only_inline int sigfillset(sigset_t *__set)
{
	*__set = ~(sigset_t)0;
	return (0);
}

#endif /* !_ANSI_LIBRARY */

#if __BSD_VISIBLE || __XPG_VISIBLE >= 420
int	killpg(pid_t, int);
int	siginterrupt(int, int);
int	sigaltstack(const struct sigaltstack *__restrict,
	    struct sigaltstack *__restrict);
#if __BSD_VISIBLE
int	sigblock(int);
/* This is the traditional BSD sigpause() and not the XPG/POSIX sigpause(). */
int	sigpause(int);
int	sigsetmask(int);
int	sigvec(int, struct sigvec *, struct sigvec *);
int	thrkill(pid_t _tid, int _signum, void *_tcb);
#endif
#endif /* __BSD_VISIBLE || __XPG_VISIBLE >= 420 */
#if __BSD_VISIBLE ||  __POSIX_VISIBLE >= 199309 || __XPG_VISIBLE >= 500
int	sigwait(const sigset_t *__restrict, int *__restrict);
#endif
#if __BSD_VISIBLE ||  __POSIX_VISIBLE >= 200809
void	psignal(unsigned int, const char *);
#endif
#endif /* __BSD_VISIBLE || __POSIX_VISIBLE || __XPG_VISIBLE */
__END_DECLS

#endif	/* !_USER_SIGNAL_H */
