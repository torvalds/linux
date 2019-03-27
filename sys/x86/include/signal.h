/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2003 Peter Wemm.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)signal.h	8.1 (Berkeley) 6/11/93
 * $FreeBSD$
 */

#ifndef _X86_SIGNAL_H
#define	_X86_SIGNAL_H 1

/*
 * Machine-dependent signal definitions
 */

#include <sys/cdefs.h>
#include <sys/_sigset.h>

#ifdef __i386__
typedef int sig_atomic_t;

#if __BSD_VISIBLE
struct sigcontext {
	struct __sigset sc_mask;	/* signal mask to restore */
	int	sc_onstack;		/* sigstack state to restore */
	int	sc_gs;			/* machine state (struct trapframe) */
	int	sc_fs;
	int	sc_es;
	int	sc_ds;
	int	sc_edi;
	int	sc_esi;
	int	sc_ebp;
	int	sc_isp;
	int	sc_ebx;
	int	sc_edx;
	int	sc_ecx;
	int	sc_eax;
	int	sc_trapno;
	int	sc_err;
	int	sc_eip;
	int	sc_cs;
	int	sc_efl;
	int	sc_esp;
	int	sc_ss;
	int	sc_len;			/* sizeof(mcontext_t) */
	/*
	 * See <machine/ucontext.h> and <machine/npx.h> for
	 * the following fields.
	 */
	int	sc_fpformat;
	int	sc_ownedfp;
	int	sc_flags;
	int	sc_fpstate[128] __aligned(16);

	int	sc_fsbase;
	int	sc_gsbase;

	int	sc_xfpustate;
	int	sc_xfpustate_len;

	int	sc_spare2[4];
};

#define	sc_sp		sc_esp
#define	sc_fp		sc_ebp
#define	sc_pc		sc_eip
#define	sc_ps		sc_efl
#define	sc_eflags	sc_efl

#endif /* __BSD_VISIBLE */
#endif /* __i386__ */

#ifdef __amd64__
typedef long sig_atomic_t;

#if __BSD_VISIBLE
/*
 * Information pushed on stack when a signal is delivered.
 * This is used by the kernel to restore state following
 * execution of the signal handler.  It is also made available
 * to the handler to allow it to restore state properly if
 * a non-standard exit is performed.
 *
 * The sequence of the fields/registers after sc_mask in struct
 * sigcontext must match those in mcontext_t and struct trapframe.
 */
struct sigcontext {
	struct __sigset sc_mask;	/* signal mask to restore */
	long	sc_onstack;		/* sigstack state to restore */
	long	sc_rdi;		/* machine state (struct trapframe) */
	long	sc_rsi;
	long	sc_rdx;
	long	sc_rcx;
	long	sc_r8;
	long	sc_r9;
	long	sc_rax;
	long	sc_rbx;
	long	sc_rbp;
	long	sc_r10;
	long	sc_r11;
	long	sc_r12;
	long	sc_r13;
	long	sc_r14;
	long	sc_r15;
	int	sc_trapno;
	short	sc_fs;
	short	sc_gs;
	long	sc_addr;
	int	sc_flags;
	short	sc_es;
	short	sc_ds;
	long	sc_err;
	long	sc_rip;
	long	sc_cs;
	long	sc_rflags;
	long	sc_rsp;
	long	sc_ss;
	long	sc_len;			/* sizeof(mcontext_t) */
	/*
	 * See <machine/ucontext.h> and <machine/fpu.h> for the following
	 * fields.
	 */
	long	sc_fpformat;
	long	sc_ownedfp;
	long	sc_fpstate[64] __aligned(16);

	long	sc_fsbase;
	long	sc_gsbase;

	long	sc_xfpustate;
	long	sc_xfpustate_len;

	long	sc_spare[4];
};
#endif /* __BSD_VISIBLE */
#endif /* __amd64__ */

#endif
