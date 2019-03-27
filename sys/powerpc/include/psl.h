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
 *	$NetBSD: psl.h,v 1.5 2000/11/19 19:52:37 matt Exp $
 * $FreeBSD$
 */

#ifndef	_MACHINE_PSL_H_
#define	_MACHINE_PSL_H_

/*
 * Machine State Register (MSR) - All cores
 */
#define	PSL_VEC		0x02000000UL	/* AltiVec/SPE vector unit available */
#define	PSL_VSX		0x00800000UL	/* Vector-Scalar unit available */
#define	PSL_EE		0x00008000UL	/* external interrupt enable */
#define	PSL_PR		0x00004000UL	/* privilege mode (1 == user) */
#define	PSL_FP		0x00002000UL	/* floating point enable */
#define	PSL_ME		0x00001000UL	/* machine check enable */
#define	PSL_FE0		0x00000800UL	/* floating point interrupt mode 0 */
#define	PSL_FE1		0x00000100UL	/* floating point interrupt mode 1 */
#define	PSL_PMM		0x00000004UL	/* performance monitor mark */
#define	PSL_RI		0x00000002UL	/* recoverable interrupt */

/* Machine State Register - Book-E cores */
#ifdef __powerpc64__
#define	PSL_CM		0x80000000UL	/* Computation Mode (64-bit) */
#endif

#define PSL_GS		0x10000000UL	/* Guest state */
#define PSL_UCLE	0x04000000UL	/* User mode cache lock enable */
#define PSL_WE		0x00040000UL	/* Wait state enable */
#define PSL_CE		0x00020000UL	/* Critical interrupt enable */
#define PSL_UBLE	0x00000400UL	/* BTB lock enable - e500 only */
#define PSL_DWE		0x00000400UL	/* Debug Wait Enable - 440 only*/
#define PSL_DE		0x00000200UL	/* Debug interrupt enable */
#define PSL_IS		0x00000020UL	/* Instruction address space */
#define PSL_DS		0x00000010UL	/* Data address space */

/* Machine State Register (MSR) - AIM cores */
#ifdef __powerpc64__
#define PSL_SF		0x8000000000000000UL	/* 64-bit addressing */
#define PSL_HV		0x1000000000000000UL	/* hyper-privileged mode */
#endif

#define	PSL_POW		0x00040000UL	/* power management */
#define	PSL_ILE		0x00010000UL	/* interrupt endian mode (1 == le) */
#define	PSL_SE		0x00000400UL	/* single-step trace enable */
#define	PSL_BE		0x00000200UL	/* branch trace enable */
#define	PSL_IP		0x00000040UL	/* interrupt prefix - 601 only */
#define	PSL_IR		0x00000020UL	/* instruction address relocation */
#define	PSL_DR		0x00000010UL	/* data address relocation */
#define	PSL_LE		0x00000001UL	/* endian mode (1 == le) */

/*
 * Floating-point exception modes:
 */
#define	PSL_FE_DIS	0		/* none */
#define	PSL_FE_NONREC	PSL_FE1		/* imprecise non-recoverable */
#define	PSL_FE_REC	PSL_FE0		/* imprecise recoverable */
#define	PSL_FE_PREC	(PSL_FE0 | PSL_FE1) /* precise */
#define	PSL_FE_DFLT	PSL_FE_DIS	/* default == none */

#ifndef LOCORE
extern register_t psl_kernset;		/* Default MSR values for kernel */
extern register_t psl_userset;		/* Default MSR values for userland */
#ifdef __powerpc64__
extern register_t psl_userset32;	/* Default user MSR values for 32-bit */
#endif
extern register_t psl_userstatic;	/* Bits of SRR1 userland may not set */
#endif

#endif	/* _MACHINE_PSL_H_ */
