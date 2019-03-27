/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Daniel M. Eischen <deischen@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
#include <sys/signal.h>
#include <sys/ucontext.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

/* Prototypes */
extern void _ctx_start(ucontext_t *, int argc, ...);


__weak_reference(__makecontext, makecontext);

void
_ctx_done (ucontext_t *ucp)
{
	if (ucp->uc_link == NULL)
		exit(0);
	else {
		/*
		 * Since this context has finished, don't allow it
		 * to be restarted without being reinitialized (via
		 * setcontext or swapcontext).
		 */
		ucp->uc_mcontext.mc_len = 0;

		/* Set context to next one in link */
		/* XXX - what to do for error, abort? */
		setcontext((const ucontext_t *)ucp->uc_link);
		abort();	/* should never get here */
	}
}

void
__makecontext(ucontext_t *ucp, void (*start)(void), int argc, ...)
{
	va_list		ap;
	char		*stack_top;
	intptr_t	*argp;
	int		i;

	if (ucp == NULL)
		return;
	else if ((ucp->uc_stack.ss_sp == NULL) ||
	    (ucp->uc_stack.ss_size < MINSIGSTKSZ)) {
		/*
		 * This should really return -1 with errno set to ENOMEM
		 * or something, but the spec says that makecontext is
		 * a void function.   At least make sure that the context
		 * isn't valid so it can't be used without an error.
		 */
		ucp->uc_mcontext.mc_len = 0;
	}
	/* XXX - Do we want to sanity check argc? */
	else if (argc < 0) {
		ucp->uc_mcontext.mc_len = 0;
	}
	/* Make sure the context is valid. */
	else if (ucp->uc_mcontext.mc_len == sizeof(mcontext_t)) {
		/*
		 * Arrange the stack as follows:
		 *
		 *	_ctx_start()	- context start wrapper
		 *	start()		- user start routine
		 * 	arg1            - first argument, aligned(16)
		 *	...
		 *	argn
		 *	ucp		- this context, %ebp points here
		 *
		 * When the context is started, control will return to
		 * the context start wrapper which will pop the user
		 * start routine from the top of the stack.  After that,
		 * the top of the stack will be setup with all arguments
		 * necessary for calling the start routine.  When the
		 * start routine returns, the context wrapper then sets
		 * the stack pointer to %ebp which was setup to point to
		 * the base of the stack (and where ucp is stored).  It
		 * will then call _ctx_done() to swap in the next context
		 * (uc_link != 0) or exit the program (uc_link == 0).
		 */
		stack_top = (char *)(ucp->uc_stack.ss_sp +
		    ucp->uc_stack.ss_size - sizeof(intptr_t));

		/*
		 * Adjust top of stack to allow for 3 pointers (return
		 * address, _ctx_start, and ucp) and argc arguments.
		 * We allow the arguments to be pointers also.  The first
		 * argument to the user function must be properly aligned.
		 */
		stack_top = stack_top - (sizeof(intptr_t) * (1 + argc));
		stack_top = (char *)((unsigned)stack_top & ~15);
		stack_top = stack_top - (2 * sizeof(intptr_t));
		argp = (intptr_t *)stack_top;

		/*
		 * Setup the top of the stack with the user start routine
		 * followed by all of its aguments and the pointer to the
		 * ucontext.  We need to leave a spare spot at the top of
		 * the stack because setcontext will move eip to the top
		 * of the stack before returning.
		 */
		*argp = (intptr_t)_ctx_start;  /* overwritten with same value */
		argp++;
		*argp = (intptr_t)start;
		argp++;

		/* Add all the arguments: */
		va_start(ap, argc);
		for (i = 0; i < argc; i++) {
			*argp = va_arg(ap, intptr_t);
			argp++;
		}
		va_end(ap);

		/* The ucontext is placed at the bottom of the stack. */
		*argp = (intptr_t)ucp;

		/*
		 * Set the machine context to point to the top of the
		 * stack and the program counter to the context start
		 * wrapper.  Note that setcontext() pushes the return
		 * address onto the top of the stack, so allow for this
		 * by adjusting the stack downward 1 slot.  Also set
		 * %esi to point to the base of the stack where ucp
		 * is stored.
		 */
		ucp->uc_mcontext.mc_esi = (int)argp;
		ucp->uc_mcontext.mc_ebp = 0;
		ucp->uc_mcontext.mc_esp = (int)stack_top + sizeof(caddr_t);
		ucp->uc_mcontext.mc_eip = (int)_ctx_start;
	}
}
