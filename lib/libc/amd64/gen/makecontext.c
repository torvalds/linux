/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ucontext.h>
#include <stdarg.h>
#include <stdlib.h>

typedef void (*func_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
    uint64_t);

/* Prototypes */
static void makectx_wrapper(ucontext_t *ucp, func_t func, uint64_t *args);

__weak_reference(__makecontext, makecontext);

void
__makecontext(ucontext_t *ucp, void (*start)(void), int argc, ...)
{
	uint64_t *args;
	uint64_t *sp;
	va_list ap;
	int i;

	/* A valid context is required. */
	if ((ucp == NULL) || (ucp->uc_mcontext.mc_len != sizeof(mcontext_t)))
		return;
	else if ((argc < 0) || (argc > 6) || (ucp->uc_stack.ss_sp == NULL) ||
	    (ucp->uc_stack.ss_size < MINSIGSTKSZ)) {
		/*
		 * This should really return -1 with errno set to ENOMEM
		 * or something, but the spec says that makecontext is
		 * a void function.   At least make sure that the context
		 * isn't valid so it can't be used without an error.
		 */
		ucp->uc_mcontext.mc_len = 0;
		return;
	}

	/* Align the stack to 16 bytes. */
	sp = (uint64_t *)(ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size);
	sp = (uint64_t *)((uint64_t)sp & ~15UL);

	/* Allocate space for a maximum of 6 arguments on the stack. */
	args = sp - 6;

	/*
	 * Account for arguments on stack and do the funky C entry alignment.
	 * This means that we need an 8-byte-odd alignment since the ABI expects
	 * the return address to be pushed, thus breaking the 16 byte alignment.
	 */
	sp -= 7;

	/* Add the arguments: */
	va_start(ap, argc);
	for (i = 0; i < argc; i++)
		args[i] = va_arg(ap, uint64_t);
	va_end(ap);
	for (i = argc; i < 6; i++)
		args[i] = 0;

	ucp->uc_mcontext.mc_rdi = (register_t)ucp;
	ucp->uc_mcontext.mc_rsi = (register_t)start;
	ucp->uc_mcontext.mc_rdx = (register_t)args;
	ucp->uc_mcontext.mc_rbp = 0;
	ucp->uc_mcontext.mc_rbx = (register_t)sp;
	ucp->uc_mcontext.mc_rsp = (register_t)sp;
	ucp->uc_mcontext.mc_rip = (register_t)makectx_wrapper;
}

static void
makectx_wrapper(ucontext_t *ucp, func_t func, uint64_t *args)
{
	(*func)(args[0], args[1], args[2], args[3], args[4], args[5]);
	if (ucp->uc_link == NULL)
		exit(0);
	setcontext((const ucontext_t *)ucp->uc_link);
	/* should never get here */
	abort();
	/* NOTREACHED */
}
