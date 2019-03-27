/*	$NetBSD: makecontext.c,v 1.5 2009/12/14 01:07:42 matt Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Klaus Klein.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: makecontext.c,v 1.5 2009/12/14 01:07:42 matt Exp $");
#endif

#include <sys/param.h>
#include <machine/abi.h>
#include <machine/regnum.h>

#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ucontext.h>

__weak_reference(__makecontext, makecontext);

void _ctx_done(ucontext_t *);
void _ctx_start(void);

void
__makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...)
{
	mcontext_t *mc;
	register_t *sp;
	int i;
	va_list ap;

	/*
	 * XXX/juli
	 * We need an mc_len or mc_flags like other architectures
	 * so that we can mark a context as invalid.  Store it in
	 * mc->mc_regs[ZERO] perhaps?
	 */
	if (argc < 0 || ucp == NULL ||
	    ucp->uc_stack.ss_sp == NULL ||
	    ucp->uc_stack.ss_size < MINSIGSTKSZ)
		return;
	mc = &ucp->uc_mcontext;

	sp  = (register_t *)
	    ((uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size);
#if defined(__mips_o32) || defined(__mips_o64)
	sp -= (argc >= 4 ? argc : 4);	/* Make room for >=4 arguments. */
#elif defined(__mips_n32) || defined(__mips_n64)
	sp -= (argc > 8 ? argc - 8 : 0); /* Make room for > 8 arguments. */
#endif
	sp  = (register_t *)((uintptr_t)sp & ~(STACK_ALIGN - 1));

	mc->mc_regs[SP] = (intptr_t)sp;
	mc->mc_regs[S0] = (intptr_t)ucp;
	mc->mc_regs[T9] = (intptr_t)func;
	mc->mc_pc = (intptr_t)_ctx_start;

	/* Construct argument list. */
	va_start(ap, argc);
#if defined(__mips_o32) || defined(__mips_o64)
	/* Up to the first four arguments are passed in $a0-3. */
	for (i = 0; i < argc && i < 4; i++)
		/* LINTED register_t is safe */
		mc->mc_regs[A0 + i] = va_arg(ap, register_t);
	/* Skip over the $a0-3 gap. */
	sp += 4;
#endif
#if defined(__mips_n32) || defined(__mips_n64)
	/* Up to the first 8 arguments are passed in $a0-7. */
	for (i = 0; i < argc && i < 8; i++)
		/* LINTED register_t is safe */
		mc->mc_regs[A0 + i] = va_arg(ap, register_t);
#endif
	/* Pass remaining arguments on the stack. */
	for (; i < argc; i++)
		/* LINTED register_t is safe */
		*sp++ = va_arg(ap, register_t);
	va_end(ap);
}

void
_ctx_done(ucontext_t *ucp)
{

	if (ucp->uc_link == NULL)
		exit(0);
	else {
		setcontext((const ucontext_t *)ucp->uc_link);
		abort();
	}
}
