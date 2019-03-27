/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
 *	@(#)ucontext.h	8.1 (Berkeley) 6/10/93
 *	JNPR: ucontext.h,v 1.2 2007/08/09 11:23:32 katta
 * $FreeBSD$
 */

#ifndef _MACHINE_UCONTEXT_H_
#define	_MACHINE_UCONTEXT_H_

#ifndef _LOCORE

typedef struct	__mcontext {
	/*
	 * These fields must match the corresponding fields in struct 
	 * sigcontext which follow 'sc_mask'. That way we can support
	 * struct sigcontext and ucontext_t at the same time.
	 */
	int		mc_onstack;	/* sigstack state to restore */
	__register_t	mc_pc;		/* pc at time of signal */
	__register_t	mc_regs[32];	/* processor regs 0 to 31 */
	__register_t	sr;		/* status register */
	__register_t	mullo, mulhi;	/* mullo and mulhi registers... */
	int		mc_fpused;	/* fp has been used */
	f_register_t	mc_fpregs[33];	/* fp regs 0 to 31 and csr */
	__register_t	mc_fpc_eir;	/* fp exception instruction reg */
	void		*mc_tls;	/* pointer to TLS area */
	int		__spare__[8];	/* XXX reserved */ 
} mcontext_t;

#if (defined(__mips_n32) || defined(__mips_n64)) && defined(COMPAT_FREEBSD32)
#include <compat/freebsd32/freebsd32_signal.h>

typedef struct __mcontext32 {
	int		mc_onstack;
	int32_t		mc_pc;
	int32_t		mc_regs[32];
	int32_t		sr;
	int32_t		mullo, mulhi;
	int		mc_fpused;
	int32_t		mc_fpregs[33];
	int32_t		mc_fpc_eir;
	int32_t		mc_tls;
	int		__spare__[8];
} mcontext32_t;

typedef struct __ucontext32 {
	sigset_t		uc_sigmask;
	mcontext32_t		uc_mcontext;
	uint32_t		uc_link;
	struct sigaltstack32    uc_stack;
	uint32_t		uc_flags;
	uint32_t		__spare__[4];
} ucontext32_t;
#endif
#endif

#ifndef SZREG
#if defined(__mips_o32)
#define	SZREG	4
#else
#define	SZREG	8
#endif
#endif

/* offsets into mcontext_t */
#define	UCTX_REG(x)	(4 + SZREG + (x)*SZREG)

#define	UCR_ZERO	UCTX_REG(0)
#define	UCR_AT		UCTX_REG(1)
#define	UCR_V0		UCTX_REG(2)
#define	UCR_V1		UCTX_REG(3)
#define	UCR_A0		UCTX_REG(4)
#define	UCR_A1		UCTX_REG(5)
#define	UCR_A2		UCTX_REG(6)
#define	UCR_A3		UCTX_REG(7)
#define	UCR_T0		UCTX_REG(8)
#define	UCR_T1		UCTX_REG(9)
#define	UCR_T2		UCTX_REG(10)
#define	UCR_T3		UCTX_REG(11)
#define	UCR_T4		UCTX_REG(12)
#define	UCR_T5		UCTX_REG(13)
#define	UCR_T6		UCTX_REG(14)
#define	UCR_T7		UCTX_REG(15)
#define	UCR_S0		UCTX_REG(16)
#define	UCR_S1		UCTX_REG(17)
#define	UCR_S2		UCTX_REG(18)
#define	UCR_S3		UCTX_REG(19)
#define	UCR_S4		UCTX_REG(20)
#define	UCR_S5		UCTX_REG(21)
#define	UCR_S6		UCTX_REG(22)
#define	UCR_S7		UCTX_REG(23)
#define	UCR_T8		UCTX_REG(24)
#define	UCR_T9		UCTX_REG(25)
#define	UCR_K0		UCTX_REG(26)
#define	UCR_K1		UCTX_REG(27)
#define	UCR_GP		UCTX_REG(28)
#define	UCR_SP		UCTX_REG(29)
#define	UCR_S8		UCTX_REG(30)
#define	UCR_RA		UCTX_REG(31)
#define	UCR_SR		UCTX_REG(32)
#define	UCR_MDLO	UCTX_REG(33)
#define	UCR_MDHI	UCTX_REG(34)

#endif	/* !_MACHINE_UCONTEXT_H_ */
