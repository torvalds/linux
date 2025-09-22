/*	$OpenBSD: signal.h,v 1.15 2019/01/12 00:16:03 jca Exp $	*/
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LIBC_SIGNAL_H
#define _LIBC_SIGNAL_H

/* Rename __errno() before it's used in the inline functions in <signal.h> */
#include <errno.h>

/* sigh: predeclare and rename the functions which we'll declare inline */
#include <sys/signal.h>

__only_inline int	sigaddset(sigset_t *__set, int __signo);
__only_inline int	sigdelset(sigset_t *__set, int __signo);
__only_inline int	sigemptyset(sigset_t *__set);
__only_inline int	sigfillset(sigset_t *__set);
__only_inline int	sigismember(const sigset_t *__set, int __signo);
PROTO_NORMAL(sigaddset);
PROTO_NORMAL(sigdelset);
PROTO_NORMAL(sigemptyset);
PROTO_NORMAL(sigfillset);
PROTO_NORMAL(sigismember);

#include_next <signal.h>

__BEGIN_HIDDEN_DECLS
extern sigset_t __sigintr;
__END_HIDDEN_DECLS

#if 0
extern PROTO_NORMAL(sys_siglist);
extern PROTO_NORMAL(sys_signame);
#endif

/* prototyped for and used by the inline functions */
PROTO_NORMAL(__errno);

PROTO_DEPRECATED(bsd_signal);
PROTO_NORMAL(kill);             /* wrap to ban SIGTHR? */
PROTO_DEPRECATED(killpg);
PROTO_DEPRECATED(psignal);
PROTO_DEPRECATED(pthread_sigmask);
PROTO_NORMAL(raise);
PROTO_WRAP(sigaction);
PROTO_NORMAL(sigaltstack);
PROTO_DEPRECATED(sigblock);
PROTO_DEPRECATED(siginterrupt);
PROTO_STD_DEPRECATED(signal);
PROTO_DEPRECATED(sigpause);
PROTO_NORMAL(sigpending);
PROTO_WRAP(sigprocmask);
PROTO_DEPRECATED(sigsetmask);
PROTO_CANCEL(sigsuspend);
PROTO_DEPRECATED(sigvec);
PROTO_DEPRECATED(sigwait);
PROTO_NORMAL(thrkill);

#endif	/* !_LIBC_SIGNAL_H */
