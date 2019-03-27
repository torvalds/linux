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
 *	@(#)signal.h	8.3 (Berkeley) 3/30/94
 * $FreeBSD$
 */

#ifndef _SIGNAL_H_
#define	_SIGNAL_H_

#include <sys/cdefs.h>
#include <sys/_types.h>
#include <sys/signal.h>
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
#include <machine/ucontext.h>
#include <sys/_ucontext.h>
#endif

__NULLABILITY_PRAGMA_PUSH

#if __BSD_VISIBLE
/*
 * XXX should enlarge these, if only to give empty names instead of bounds
 * errors for large signal numbers.
 */
extern const char * const sys_signame[NSIG];
extern const char * const sys_siglist[NSIG];
extern const int sys_nsig;
#endif

#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif
#endif

#if __POSIX_VISIBLE || __XSI_VISIBLE
struct pthread;		/* XXX */
typedef struct pthread *__pthread_t;
#if !defined(_PTHREAD_T_DECLARED) && __POSIX_VISIBLE >= 200809
typedef __pthread_t pthread_t;
#define	_PTHREAD_T_DECLARED
#endif
#endif /* __POSIX_VISIBLE || __XSI_VISIBLE */

__BEGIN_DECLS
int	raise(int);

#if __POSIX_VISIBLE || __XSI_VISIBLE
int	kill(__pid_t, int);
int	pthread_kill(__pthread_t, int);
int	pthread_sigmask(int, const __sigset_t * __restrict,
	    __sigset_t * __restrict);
int	sigaction(int, const struct sigaction * __restrict,
	    struct sigaction * __restrict);
int	sigaddset(sigset_t *, int);
int	sigdelset(sigset_t *, int);
int	sigemptyset(sigset_t *);
int	sigfillset(sigset_t *);
int	sigismember(const sigset_t *, int);
int	sigpending(sigset_t * _Nonnull);
int	sigprocmask(int, const sigset_t * __restrict, sigset_t * __restrict);
int	sigsuspend(const sigset_t * _Nonnull);
int	sigwait(const sigset_t * _Nonnull __restrict,
	    int * _Nonnull __restrict);
#endif

#if __POSIX_VISIBLE >= 199506 || __XSI_VISIBLE >= 600
int	sigqueue(__pid_t, int, const union sigval);

struct timespec;
int	sigtimedwait(const sigset_t * __restrict, siginfo_t * __restrict,
	    const struct timespec * __restrict);
int	sigwaitinfo(const sigset_t * __restrict, siginfo_t * __restrict);
#endif

#if __XSI_VISIBLE
int	killpg(__pid_t, int);
int	sigaltstack(const stack_t * __restrict, stack_t * __restrict); 
int	sighold(int);
int	sigignore(int);
int	sigpause(int);
int	sigrelse(int);
void	(* _Nullable sigset(int, void (* _Nullable)(int)))(int);
int	xsi_sigpause(int);
#endif

#if __XSI_VISIBLE >= 600
int	siginterrupt(int, int);
#endif

#if __POSIX_VISIBLE >= 200809
void	psignal(int, const char *);
#endif

#if __BSD_VISIBLE
int	sigblock(int);
int	sigreturn(const struct __ucontext *);
int	sigsetmask(int);
int	sigstack(const struct sigstack *, struct sigstack *);
int	sigvec(int, struct sigvec *, struct sigvec *);
#endif
__END_DECLS
__NULLABILITY_PRAGMA_POP

#endif /* !_SIGNAL_H_ */
