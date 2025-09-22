/*	$OpenBSD: signal.h,v 1.9 2016/05/10 18:39:42 deraadt Exp $	*/
/*	$NetBSD: signal.h,v 1.2 2003/04/28 23:16:17 bjh21 Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1991 Regents of the University of California.
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
 *	@(#)signal.h	7.16 (Berkeley) 3/17/91
 */

#ifndef _MACHINE_SIGNAL_H_
#define _MACHINE_SIGNAL_H_

#include <sys/cdefs.h>

typedef int sig_atomic_t;

#ifdef _KERNEL
#include <machine/trap.h>
#endif

#if __BSD_VISIBLE || __XPG_VISIBLE >= 420
/*
 * Information pushed on stack when a signal is delivered.
 * This is used by the kernel to restore state following
 * execution of the signal handler.  It is also made available
 * to the handler to allow it to restore state properly if
 * a non-standard exit is performed.
 */
struct sigcontext {
	/* plain match trapframe */
	long	sc_rdi;
	long	sc_rsi;
	long	sc_rdx;
	long	sc_rcx;
	long	sc_r8;
	long	sc_r9;
	long	sc_r10;
	long	sc_r11;
	long	sc_r12;
	long	sc_r13;
	long	sc_r14;
	long	sc_r15;
	long	sc_rbp;
	long	sc_rbx;
	long	sc_rax;
	long	sc_gs;
	long	sc_fs;
	long	sc_es;
	long	sc_ds;
	long	sc_trapno;
	long	sc_err;
	long	sc_rip;
	long	sc_cs;
	long	sc_rflags;
	long	sc_rsp;
	long	sc_ss;

	struct fxsave64 *sc_fpstate;
	int	__sc_unused;
	int	sc_mask;
	long	sc_cookie;
};
#endif /* __BSD_VISIBLE || __XPG_VISIBLE >= 420 */
#endif	/* !_MACHINE_SIGNAL_H_ */
