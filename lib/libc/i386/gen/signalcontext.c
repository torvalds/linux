/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Jonathan Mini <mini@freebsd.org>
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
#include <sys/ucontext.h>
#include <machine/psl.h>
#include <machine/sigframe.h>
#include <signal.h>
#include <strings.h>

__weak_reference(__signalcontext, signalcontext);

extern void _ctx_start(ucontext_t *, int argc, ...);

int
__signalcontext(ucontext_t *ucp, int sig, __sighandler_t *func)
{
	register_t *p;
	struct sigframe *sfp;

	/*-
	 * Set up stack.
	 * (n = sizeof(int))
	 * 2n+sizeof(struct sigframe)	ucp
	 * 2n				struct sigframe
	 * 1n				&func
	 * 0n				&_ctx_start
	 */
	p = (register_t *)(void *)(intptr_t)ucp->uc_mcontext.mc_esp;
	*--p = (register_t)(intptr_t)ucp;
	p = (register_t *)((u_register_t)p & ~0xF);  /* Align to 16 bytes. */
	p = (register_t *)((u_register_t)p - sizeof(struct sigframe));
	sfp = (struct sigframe *)p;
	bzero(sfp, sizeof(struct sigframe));
	sfp->sf_signum = sig;
	sfp->sf_siginfo = (register_t)(intptr_t)&sfp->sf_si;
	sfp->sf_ucontext = (register_t)(intptr_t)&sfp->sf_uc;
	sfp->sf_ahu.sf_action = (__siginfohandler_t *)func;
	bcopy(ucp, &sfp->sf_uc, sizeof(ucontext_t));
	sfp->sf_si.si_signo = sig;
	*--p = (register_t)(intptr_t)func;

	/*
	 * Set up ucontext_t.
	 */
	ucp->uc_mcontext.mc_esi = ucp->uc_mcontext.mc_esp - sizeof(int);
	ucp->uc_mcontext.mc_esp = (register_t)(intptr_t)p;
	ucp->uc_mcontext.mc_eip = (register_t)(intptr_t)_ctx_start;
	ucp->uc_mcontext.mc_eflags &= ~PSL_T;
	ucp->uc_link = &sfp->sf_uc;
	sigdelset(&ucp->uc_sigmask, sig);
	return (0);
}
