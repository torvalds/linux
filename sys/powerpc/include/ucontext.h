/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: signal.h,v 1.4 1998/09/14 02:48:34 thorpej Exp $
 * $FreeBSD$
 */

#ifndef	_MACHINE_UCONTEXT_H_
#define	_MACHINE_UCONTEXT_H_

typedef struct __mcontext {
	int		mc_vers;
	int		mc_flags;
#define _MC_FP_VALID	0x01
#define _MC_AV_VALID	0x02
	int		mc_onstack;	  	/* saved onstack flag */
	int		mc_len;			/* sizeof(__mcontext) */
	__uint64_t	mc_avec[32*2];		/* vector register file */
	__uint32_t	mc_av[2];
	__register_t	mc_frame[42];
	__uint64_t	mc_fpreg[33];
	__uint64_t	mc_vsxfpreg[32];	/* low-order half of VSR0-31 */
} mcontext_t __aligned(16);

#if defined(_KERNEL) && defined(__powerpc64__)
typedef struct __mcontext32 {
	int		mc_vers;
	int		mc_flags;
#define _MC_FP_VALID	0x01
#define _MC_AV_VALID	0x02
	int		mc_onstack;	  	/* saved onstack flag */
	int		mc_len;			/* sizeof(__mcontext) */
	uint64_t	mc_avec[32*2];		/* vector register file */
	uint32_t	mc_av[2];
	uint32_t	mc_frame[42];
	uint64_t	mc_fpreg[33];
	uint64_t	mc_vsxfpreg[32];	/* low-order half of VSR0-31 */
} mcontext32_t __aligned(16);
#endif

/* GPRs and supervisor-level regs */
#define mc_gpr		mc_frame
#define mc_lr		mc_frame[32]
#define mc_cr		mc_frame[33]
#define mc_xer		mc_frame[34]
#define	mc_ctr		mc_frame[35]
#define mc_srr0		mc_frame[36]
#define mc_srr1		mc_frame[37]
#define mc_exc		mc_frame[38]
#define mc_dar		mc_frame[39]
#define mc_dsisr	mc_frame[40]

/* floating-point state */
#define mc_fpscr	mc_fpreg[32]

/* altivec state */
#define mc_vscr		mc_av[0]
#define mc_vrsave	mc_av[1]

#define _MC_VERSION	0x1
#define _MC_VERSION_KSE 0xee	/* partial ucontext for libpthread */

#endif	/* !_MACHINE_UCONTEXT_H_ */
