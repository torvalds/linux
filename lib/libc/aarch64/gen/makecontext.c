/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <machine/armreg.h>

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ucontext.h>

void _ctx_start(void);

void
ctx_done(ucontext_t *ucp)
{       
	        
	if (ucp->uc_link == NULL) {
		exit(0);
	} else {  
		setcontext((const ucontext_t *)ucp->uc_link);
		abort();
	}                                                      
}
   
__weak_reference(__makecontext, makecontext);

void
__makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...)
{
	struct gpregs *gp;
	va_list ap;
	int i;

	/* A valid context is required. */
	if (ucp == NULL)
		return;

	if ((argc < 0) || (argc > 8))
		return;

	gp = &ucp->uc_mcontext.mc_gpregs;

	va_start(ap, argc);
	/* Pass up to eight arguments in x0-7. */
	for (i = 0; i < argc && i < 8; i++)
		gp->gp_x[i] = va_arg(ap, uint64_t);
	va_end(ap);

	/* Set the stack */
	gp->gp_sp = STACKALIGN(ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size);
	/* Arrange for return via the trampoline code. */
	gp->gp_elr = (__register_t)_ctx_start;
	gp->gp_x[19] = (__register_t)func;
	gp->gp_x[20] = (__register_t)ucp;
}
