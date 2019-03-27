/*	$NetBSD: makecontext.c,v 1.2 2003/01/18 11:06:24 thorpej Exp $	*/

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

#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>
#include <ucontext.h>

#include <stdarg.h>

extern void _ctx_start(void);

void
ctx_done(ucontext_t *ucp)
{       
	        
	if (ucp->uc_link == NULL)
		exit(0);
	else {  
		setcontext((const ucontext_t *)ucp->uc_link);
		abort();
	}                                                      
}   

__weak_reference(__makecontext, makecontext);

void
__makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...)
{
	__greg_t *gr = ucp->uc_mcontext.__gregs;
	int i;
	unsigned int *sp;
	va_list ap;

	/* Compute and align stack pointer. */
	sp = (unsigned int *)
	    (((uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size -
	      sizeof(double)) & ~7);
	/* Allocate necessary stack space for arguments exceeding r0-3. */
	if (argc > 4)
		sp -= argc - 4;
	gr[_REG_SP] = (__greg_t)sp;
	/* Wipe out frame pointer. */
	gr[_REG_FP] = 0;
	/* Arrange for return via the trampoline code. */
	gr[_REG_PC] = (__greg_t)_ctx_start;
	gr[_REG_R4] = (__greg_t)func;
	gr[_REG_R5] = (__greg_t)ucp;

	va_start(ap, argc);
	/* Pass up to four arguments in r0-3. */
	for (i = 0; i < argc && i < 4; i++)
		gr[_REG_R0 + i] = va_arg(ap, int);
	/* Pass any additional arguments on the stack. */
	for (argc -= i; argc > 0; argc--)
		*sp++ = va_arg(ap, int);
	va_end(ap);
}
