/*	$OpenBSD: fsr.h,v 1.2 2003/06/02 23:27:56 millert Exp $	*/
/*	$NetBSD: fsr.h,v 1.1.1.1 1998/06/20 04:58:51 eeh Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)fsr.h	8.1 (Berkeley) 6/11/93
 */

#ifndef _MACHINE_FSR_H_
#define	_MACHINE_FSR_H_

/* 
 * Bits in FPRS
 */
#define FPRS_FEF	0x04		/* Enable FP -- must be set to enable FP regs */
#define FPRS_DU		0x02		/* Dirty upper -- upper fp regs are dirty */
#define FPRS_DL		0x01		/* Dirty lower -- lower fp regs are dirty */

/*
 * Bits in FSR.
 */

#define	FSR_RD		0xc0000000	/* rounding direction */
#define	  FSR_RD_RN	0		/* round to nearest */
#define	  FSR_RD_RZ	1		/* round towards 0 */
#define	  FSR_RD_RP	2		/* round towards +inf */
#define	  FSR_RD_RM	3		/* round towards -inf */
#define	FSR_RD_SHIFT	30
#define	FSR_RD_MASK	0x03

#define	FSR_RP		0x30000000	/* extended rounding precision */
#define	  FSR_RP_X	0		/* extended stays extended */
#define	  FSR_RP_S	1		/* extended => single */
#define	  FSR_RP_D	2		/* extended => double */
#define	  FSR_RP_80	3		/* extended => 80-bit */
#define	FSR_RP_SHIFT	28
#define	FSR_RP_MASK	0x03

#define	FSR_TEM		0x0f800000	/* trap enable mask */
#define	FSR_TEM_SHIFT	23
#define	FSR_TEM_MASK	0x1f

#define	FSR_NS		0x00400000	/* ``nonstandard mode'' */
#define	FSR_AU		0x00400000	/* aka abrupt underflow mode */
#define	FSR_MBZ		0x00300000	/* reserved; must be zero */

#define	FSR_VER		0x000e0000	/* version bits */
#define	FSR_VER_SHIFT	17
#define	FSR_VER_MASK	0x07

#define	FSR_FTT		0x0001c000	/* FP trap type */
#define	  FSR_TT_NONE	0		/* no trap */
#define	  FSR_TT_IEEE	1		/* IEEE exception */
#define	  FSR_TT_UNFIN	2		/* unfinished operation */
#define	  FSR_TT_UNIMP	3		/* unimplemented operation */
#define	  FSR_TT_SEQ	4		/* sequence error */
#define	  FSR_TT_HWERR	5		/* hardware error (unrecoverable) */
#define	FSR_FTT_SHIFT	14
#define	FSR_FTT_MASK	0x03

#define	FSR_QNE		0x00002000	/* queue not empty */
#define	FSR_PR		0x00001000	/* partial result */

#define	FSR_FCC		0x00000c00	/* FP condition codes */
#define	  FSR_CC_EQ	0		/* f1 = f2 */
#define	  FSR_CC_LT	1		/* f1 < f2 */
#define	  FSR_CC_GT	2		/* f1 > f2 */
#define	  FSR_CC_UO	3		/* (f1,f2) unordered */
#define	FSR_FCC_SHIFT	10
#define	FSR_FCC_MASK	0x03

#define	FSR_AX	0x000003e0		/* accrued exceptions */
#define	  FSR_AX_SHIFT	5
#define	  FSR_AX_MASK	0x1f
#define	FSR_CX	0x0000001f		/* current exceptions */
#define	  FSR_CX_SHIFT	0
#define	  FSR_CX_MASK	0x1f

/* These are the 3 new v9 fcc's */
#define	FSR_FCC3	0x06000000000	/* FP condition codes */
#define	FSR_FCC3_SHIFT	36

#define	FSR_FCC2	0x0c00000000	/* FP condition codes */
#define	FSR_FCC2_SHIFT	34

#define	FSR_FCC1	0x0600000000	/* FP condition codes */
#define	FSR_FCC1_SHIFT	32


/* The following exceptions apply to TEM, AX, and CX. */
#define	FSR_NV	0x10			/* invalid operand */
#define	FSR_OF	0x08			/* overflow */
#define	FSR_UF	0x04			/* underflow */
#define	FSR_DZ	0x02			/* division by zero */
#define	FSR_NX	0x01			/* inexact result */

#endif /* _MACHINE_FSR_H_ */
