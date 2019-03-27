/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2001 by Thomas Moestl <tmm@FreeBSD.org>.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_FSR_H_
#define	_MACHINE_FSR_H_

#define	FPRS_DL		(1 << 0)
#define	FPRS_DU		(1 << 1)
#define	FPRS_FEF	(1 << 2)

#define	VIS_BLOCKSIZE	64

#ifndef LOCORE

#define	FSR_EXC_BITS	5
#define	FSR_EXC_MASK	((1UL << FSR_EXC_BITS) - 1)
#define	FSR_CEXC_SHIFT	0
#define	FSR_CEXC_MASK	(FSR_EXC_MASK << FSR_CEXC_SHIFT)
#define	FSR_CEXC(b)	((unsigned long)(b) << FSR_CEXC_SHIFT)
#define	FSR_GET_CEXC(x)	(((x) & FSR_CEXC_MASK) >> FSR_CEXC_SHIFT)
#define	FSR_AEXC_SHIFT	5
#define	FSR_AEXC_MASK	(FSR_EXC_MASK << FSR_AEXC_SHIFT)
#define	FSR_AEXC(b)	((unsigned long)(b) << FSR_AEXC_SHIFT)
#define	FSR_GET_AEXC(x)	(((x) & FSR_AEXC_MASK) >> FSR_AEXC_SHIFT)
#define	FSR_QNE		(1UL << 13)
#define	FSR_NS		(1UL << 22)
#define	FSR_TEM_SHIFT	23
#define	FSR_TEM_MASK	(FSR_EXC_MASK << FSR_TEM_SHIFT)
#define	FSR_TEM(b)	((unsigned long)(b) << FSR_TEM_SHIFT)
#define	FSR_GET_TEM(x)	(((x) & FSR_TEM_MASK) >> FSR_TEM_SHIFT)
#define	FSR_FCC0_SHIFT	10
#define	FSR_FCC0_BITS	2
#define	FSR_FCC0_MASK	(((1UL << FSR_FCC0_BITS) - 1) << FSR_FCC0_SHIFT)
#define	FSR_FCC0(x)	((unsigned long)(x) << FSR_FCC0_SHIFT)
#define	FSR_GET_FCC0(x)	(((x) & FSR_FCC0_MASK) >> FSR_FCC0_SHIFT)
#define	FSR_FTT_SHIFT	14
#define	FSR_FTT_BITS	3
#define	FSR_FTT_MASK	(((1UL << FSR_FTT_BITS) - 1) << FSR_FTT_SHIFT)
#define	FSR_FTT(x)	((unsigned long)(x) << FSR_FTT_SHIFT)
#define	FSR_GET_FTT(x)	(((x) & FSR_FTT_MASK) >> FSR_FTT_SHIFT)
#define	FSR_VER_SHIFT	17
#define	FSR_GET_VER(x)	(((x) >> FSR_VER_SHIFT) & 7)
#define	FSR_RD_SHIFT	30
#define	FSR_RD_BITS	2
#define	FSR_RD_MASK	(((1UL << FSR_RD_BITS) - 1) << FSR_RD_SHIFT)
#define	FSR_RD(x)	((unsigned long)(x) << FSR_RD_SHIFT)
#define	FSR_GET_RD(x)	(((x) & FSR_RD_MASK) >> FSR_RD_SHIFT)
#define	FSR_FCC1_SHIFT	32
#define	FSR_FCC1_BITS	2
#define	FSR_FCC1_MASK	(((1UL << FSR_FCC1_BITS) - 1) << FSR_FCC1_SHIFT)
#define	FSR_FCC1(x)	((unsigned long)(x) << FSR_FCC1_SHIFT)
#define	FSR_GET_FCC1(x)	(((x) & FSR_FCC1_MASK) >> FSR_FCC1_SHIFT)
#define	FSR_FCC2_SHIFT	34
#define	FSR_FCC2_BITS	2
#define	FSR_FCC2_MASK	(((1UL << FSR_FCC2_BITS) - 1) << FSR_FCC2_SHIFT)
#define	FSR_FCC2(x)	((unsigned long)(x) << FSR_FCC2_SHIFT)
#define	FSR_GET_FCC2(x)	(((x) & FSR_FCC2_MASK) >> FSR_FCC2_SHIFT)
#define	FSR_FCC3_SHIFT	36
#define	FSR_FCC3_BITS	2
#define	FSR_FCC3_MASK	(((1UL << FSR_FCC3_BITS) - 1) << FSR_FCC3_SHIFT)
#define	FSR_FCC3(x)	((unsigned long)(x) << FSR_FCC3_SHIFT)
#define	FSR_GET_FCC3(x)	(((x) & FSR_FCC3_MASK) >> FSR_FCC3_SHIFT)

/* CEXC/AEXC/TEM exception values */
#define	FSR_NX		(1 << 0)
#define	FSR_DZ		(1 << 1)
#define	FSR_UF		(1 << 2)
#define	FSR_OF		(1 << 3)
#define	FSR_NV		(1 << 4)
/* FTT values. */
#define	FSR_FTT_NONE	0
#define	FSR_FTT_IEEE	1
#define	FSR_FTT_UNFIN	2
#define	FSR_FTT_UNIMP	3
#define	FSR_FTT_SEQERR	4
#define	FSR_FTT_HWERR	5
#define	FSR_FTT_INVREG	6
/* RD values */
#define	FSR_RD_N	0		/* nearest */
#define	FSR_RD_Z	1		/* zero */
#define	FSR_RD_PINF	2		/* +infinity */
#define	FSR_RD_NINF	3		/* -infinity */
/* condition codes */
#define	FSR_CC_EQ	0	/* a = b */
#define	FSR_CC_LT	1	/* a < b */
#define	FSR_CC_GT	2	/* a > b */
#define	FSR_CC_UO	3	/* unordered */

#endif /* !LOCORE */

#endif /* !_MACHINE_FSR_H_ */
