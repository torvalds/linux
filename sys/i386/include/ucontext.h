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

#ifndef _MACHINE_UCONTEXT_H_
#define	_MACHINE_UCONTEXT_H_

#if defined(_KERNEL) && defined(COMPAT_FREEBSD4)
struct mcontext4 {
	__register_t	mc_onstack;	/* XXX - sigcontext compat. */
	__register_t	mc_gs;		/* machine state (struct trapframe) */
	__register_t	mc_fs;
	__register_t	mc_es;
	__register_t	mc_ds;
	__register_t	mc_edi;
	__register_t	mc_esi;
	__register_t	mc_ebp;
	__register_t	mc_isp;
	__register_t	mc_ebx;
	__register_t	mc_edx;
	__register_t	mc_ecx;
	__register_t	mc_eax;
	__register_t	mc_trapno;
	__register_t	mc_err;
	__register_t	mc_eip;
	__register_t	mc_cs;
	__register_t	mc_eflags;
	__register_t	mc_esp;		/* machine state */
	__register_t	mc_ss;
	__register_t	mc_fpregs[28];	/* env87 + fpacc87 + u_long */
	__register_t	__spare__[17];
};
#endif

#include <x86/ucontext.h>

#endif /* !_MACHINE_UCONTEXT_H_ */
