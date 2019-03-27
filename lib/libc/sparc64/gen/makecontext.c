/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Jake Burkholder.
 * All rights reserved.
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
#include <sys/signal.h>
#include <sys/ucontext.h>

#include <machine/frame.h>
#include <machine/tstate.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

__weak_reference(__makecontext, makecontext);

void _ctx_done(ucontext_t *ucp);
void _ctx_start(void);

void
__makecontext(ucontext_t *ucp, void (*start)(void), int argc, ...)
{
	mcontext_t *mc;
	uint64_t sp;
	va_list ap;
	int i;

	mc = &ucp->uc_mcontext;
	if (ucp == NULL ||
	    (mc->_mc_flags & ((1L << _MC_VERSION_BITS) - 1)) != _MC_VERSION)
		return;
	if ((argc < 0) || (argc > 6) ||
	    (ucp->uc_stack.ss_sp == NULL) ||
	    (ucp->uc_stack.ss_size < MINSIGSTKSZ)) {
		mc->_mc_flags = 0;
		return;
	}
	mc = &ucp->uc_mcontext;
	sp = (uint64_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;
	va_start(ap, argc);
	for (i = 0; i < argc; i++)
		mc->mc_out[i] = va_arg(ap, uint64_t);
	va_end(ap);
	mc->mc_global[1] = (uint64_t)start;
	mc->mc_global[2] = (uint64_t)ucp;
	mc->mc_out[6] = sp - SPOFF - sizeof(struct frame);
	mc->_mc_tnpc = (uint64_t)_ctx_start + 4;
	mc->_mc_tpc = (uint64_t)_ctx_start;
}

void
_ctx_done(ucontext_t *ucp)
{

	if (ucp->uc_link == NULL)
		exit(0);
	else {
		ucp->uc_mcontext._mc_flags = 0;
		setcontext((const ucontext_t *)ucp->uc_link);
		abort();
	}
}
