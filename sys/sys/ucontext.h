/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_UCONTEXT_H_
#define	_SYS_UCONTEXT_H_

#include <sys/signal.h>
#include <machine/ucontext.h>
#include <sys/_ucontext.h>

#define	UCF_SWAPPED	0x00000001	/* Used by swapcontext(3). */

#if defined(_KERNEL) && defined(COMPAT_FREEBSD4)
#if defined(__i386__)
struct ucontext4 {
	sigset_t	uc_sigmask;
	struct mcontext4 uc_mcontext;
	struct ucontext4 *uc_link;
	stack_t		uc_stack;
	int		__spare__[8];
};
#else	/* __i386__ */
#define ucontext4 ucontext
#endif	/* __i386__ */
#endif	/* _KERNEL */

#ifndef _KERNEL

__BEGIN_DECLS

int	getcontext(ucontext_t *) __returns_twice;
ucontext_t *getcontextx(void);
int	setcontext(const ucontext_t *);
void	makecontext(ucontext_t *, void (*)(void), int, ...);
int	signalcontext(ucontext_t *, int, __sighandler_t *);
int	swapcontext(ucontext_t *, const ucontext_t *);

#if __BSD_VISIBLE
int __getcontextx_size(void);
int __fillcontextx(char *ctx) __returns_twice;
int __fillcontextx2(char *ctx);
#endif

__END_DECLS

#else /* _KERNEL */

struct thread;

/*
 * Flags for get_mcontext().  The low order 4 bits (i.e a mask of 0x0f) are
 * reserved for use by machine independent code.  All other bits are for use
 * by machine dependent code.
 */
#define	GET_MC_CLEAR_RET	1

/* Machine-dependent functions: */
int	get_mcontext(struct thread *, mcontext_t *, int);
int	set_mcontext(struct thread *, mcontext_t *);

#endif /* !_KERNEL */

#endif /* !_SYS_UCONTEXT_H_ */
