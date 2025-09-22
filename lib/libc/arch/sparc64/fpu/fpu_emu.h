/*	$OpenBSD: fpu_emu.h,v 1.1 2003/07/21 18:41:30 jason Exp $	*/

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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)fpu_emu.h	8.1 (Berkeley) 6/11/93
 *	$NetBSD: fpu_emu.h,v 1.4 2000/08/03 18:32:07 eeh Exp $
 * $FreeBSD: src/lib/libc/sparc64/fpu/fpu_emu.h,v 1.4 2002/03/22 23:41:59 obrien Exp $
 */

/*
 * Floating point emulator (tailored for SPARC, but structurally
 * machine-independent).
 *
 * Floating point numbers are carried around internally in an `expanded'
 * or `unpacked' form consisting of:
 *	- sign
 *	- unbiased exponent
 *	- mantissa (`1.' + 112-bit fraction + guard + round)
 *	- sticky bit
 * Any implied `1' bit is inserted, giving a 113-bit mantissa that is
 * always nonzero.  Additional low-order `guard' and `round' bits are
 * scrunched in, making the entire mantissa 115 bits long.  This is divided
 * into four 32-bit words, with `spare' bits left over in the upper part
 * of the top word (the high bits of fp_mant[0]).  An internal `exploded'
 * number is thus kept within the half-open interval [1.0,2.0) (but see
 * the `number classes' below).  This holds even for denormalized numbers:
 * when we explode an external denorm, we normalize it, introducing low-order
 * zero bits, so that the rest of the code always sees normalized values.
 *
 * Note that a number of our algorithms use the `spare' bits at the top.
 * The most demanding algorithm---the one for sqrt---depends on two such
 * bits, so that it can represent values up to (but not including) 8.0,
 * and then it needs a carry on top of that, so that we need three `spares'.
 *
 * The sticky-word is 32 bits so that we can use `OR' operators to goosh
 * whole words from the mantissa into it.
 *
 * All operations are done in this internal extended precision.  According
 * to Hennesey & Patterson, Appendix A, rounding can be repeated---that is,
 * it is OK to do a+b in extended precision and then round the result to
 * single precision---provided single, double, and extended precisions are
 * `far enough apart' (they always are), but we will try to avoid any such
 * extra work where possible.
 */

#ifndef _SPARC64_FPU_FPU_EMU_H_
#define _SPARC64_FPU_FPU_EMU_H_

#include "fpu_reg.h"

struct fpn {
	int	fp_class;		/* see below */
	int	fp_sign;		/* 0 => positive, 1 => negative */
	int	fp_exp;			/* exponent (unbiased) */
	int	fp_sticky;		/* nonzero bits lost at right end */
	u_int	fp_mant[4];		/* 115-bit mantissa */
};

#define	FP_NMANT	115		/* total bits in mantissa (incl g,r) */
#define	FP_NG		2		/* number of low-order guard bits */
#define	FP_LG		((FP_NMANT - 1) & 31)	/* log2(1.0) for fp_mant[0] */
#define	FP_LG2		((FP_NMANT - 1) & 63)	/* log2(1.0) for fp_mant[0] and fp_mant[1] */
#define	FP_QUIETBIT	(1 << (FP_LG - 1))	/* Quiet bit in NaNs (0.5) */
#define	FP_1		(1 << FP_LG)		/* 1.0 in fp_mant[0] */
#define	FP_2		(1 << (FP_LG + 1))	/* 2.0 in fp_mant[0] */

/*
 * Number classes.  Since zero, Inf, and NaN cannot be represented using
 * the above layout, we distinguish these from other numbers via a class.
 * In addition, to make computation easier and to follow Appendix N of
 * the SPARC Version 8 standard, we give each kind of NaN a separate class.
 */
#define	FPC_SNAN	-2		/* signalling NaN (sign irrelevant) */
#define	FPC_QNAN	-1		/* quiet NaN (sign irrelevant) */
#define	FPC_ZERO	0		/* zero (sign matters) */
#define	FPC_NUM		1		/* number (sign matters) */
#define	FPC_INF		2		/* infinity (sign matters) */

#define	ISNAN(fp)	((fp)->fp_class < 0)
#define	ISZERO(fp)	((fp)->fp_class == 0)
#define	ISINF(fp)	((fp)->fp_class == FPC_INF)

/*
 * ORDER(x,y) `sorts' a pair of `fpn *'s so that the right operand (y) points
 * to the `more significant' operand for our purposes.  Appendix N says that
 * the result of a computation involving two numbers are:
 *
 *	If both are SNaN: operand 2, converted to Quiet
 *	If only one is SNaN: the SNaN operand, converted to Quiet
 *	If both are QNaN: operand 2
 *	If only one is QNaN: the QNaN operand
 *
 * In addition, in operations with an Inf operand, the result is usually
 * Inf.  The class numbers are carefully arranged so that if
 *	(unsigned)class(op1) > (unsigned)class(op2)
 * then op1 is the one we want; otherwise op2 is the one we want.
 */
#define	ORDER(x, y) { \
	if ((u_int)(x)->fp_class > (u_int)(y)->fp_class) \
		SWAP(x, y); \
}
#define	SWAP(x, y) { \
	register struct fpn *swap; \
	swap = (x), (x) = (y), (y) = swap; \
}

/*
 * Emulator state.
 */
struct fpemu {
	u_long	fe_fsr;			/* fsr copy (modified during op) */
	int	fe_cx;			/* exceptions */
	struct	fpn fe_f1;		/* operand 1 */
	struct	fpn fe_f2;		/* operand 2, if required */
	struct	fpn fe_f3;		/* available storage for result */
};

/*
 * Arithmetic functions.
 * Each of these may modify its inputs (f1,f2) and/or the temporary.
 * Each returns a pointer to the result and/or sets exceptions.
 */
#define	__fpu_sub(fe) ((fe)->fe_f2.fp_sign ^= 1, __fpu_add(fe))

#ifdef FPU_DEBUG
#define	FPE_INSN	0x1
#define	FPE_REG		0x2
extern int __fpe_debug;
void	__fpu_dumpfpn(struct fpn *);
#define	DPRINTF(x, y)	if (__fpe_debug & (x)) printf y
#define DUMPFPN(x, f)	if (__fpe_debug & (x)) __fpu_dumpfpn((f))
#else
#define	DPRINTF(x, y)
#define DUMPFPN(x, f)
#endif

#define FSR_GET_RD(fsr)		(((fsr) >> FSR_RD_SHIFT) & FSR_RD_MASK)
#endif /* !_SPARC64_FPU_FPU_EXTERN_H_ */
