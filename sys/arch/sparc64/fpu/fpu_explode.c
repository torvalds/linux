/*	$OpenBSD: fpu_explode.c,v 1.8 2024/03/29 21:08:10 miod Exp $	*/
/*	$NetBSD: fpu_explode.c,v 1.5 2000/08/03 18:32:08 eeh Exp $ */

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
 *	@(#)fpu_explode.c	8.1 (Berkeley) 6/11/93
 */

/*
 * FPU subroutines: `explode' the machine's `packed binary' format numbers
 * into our internal format.
 */

#include <sys/types.h>
#include <sys/systm.h>

#include <machine/fsr.h>
#include <machine/ieee.h>
#include <machine/instr.h>
#include <machine/reg.h>

#include <sparc64/fpu/fpu_arith.h>
#include <sparc64/fpu/fpu_emu.h>
#include <sparc64/fpu/fpu_extern.h>

/*
 * N.B.: in all of the following, we assume the FP format is
 *
 *	---------------------------
 *	| s | exponent | fraction |
 *	---------------------------
 *
 * (which represents -1**s * 1.fraction * 2**exponent), so that the
 * sign bit is way at the top (bit 31), the exponent is next, and
 * then the remaining bits mark the fraction.  A zero exponent means
 * zero or denormalized (0.fraction rather than 1.fraction), and the
 * maximum possible exponent, 2bias+1, signals inf (fraction==0) or NaN.
 *
 * Since the sign bit is always the topmost bit---this holds even for
 * integers---we set that outside all the *tof functions.  Each function
 * returns the class code for the new number (but note that we use
 * FPC_QNAN for all NaNs; fpu_explode will fix this if appropriate).
 */

/*
 * int -> fpn.
 */
int
fpu_itof(struct fpn *fp, u_int i)
{

	if (i == 0)
		return (FPC_ZERO);
	/*
	 * The value FP_1 represents 2^FP_LG, so set the exponent
	 * there and let normalization fix it up.  Convert negative
	 * numbers to sign-and-magnitude.  Note that this relies on
	 * fpu_norm()'s handling of `supernormals'; see fpu_subr.c.
	 */
	fp->fp_exp = FP_LG;
	fp->fp_mant[0] = (fp->fp_sign && (int)i < 0) ? -i : i;
	fp->fp_mant[1] = 0;
	fp->fp_mant[2] = 0;
	fp->fp_mant[3] = 0;
	fpu_norm(fp);
	return (FPC_NUM);
}

/*
 * 64-bit int -> fpn.
 */
int
fpu_xtof(struct fpn *fp, u_int64_t i)
{
	if (i == 0)
		return (FPC_ZERO);

	/*
	 * The value FP_1 represents 2^FP_LG, so set the exponent
	 * there and let normalization fix it up.  Convert negative
	 * numbers to sign-and-magnitude.  Note that this relies on
	 * fpu_norm()'s handling of `supernormals'; see fpu_subr.c.
	 */
	fp->fp_exp = FP_LG2;
	i = (fp->fp_sign && (int64_t)i < 0) ? -i : i;
	fp->fp_mant[0] = (i >> 32) & 0xffffffff;
	fp->fp_mant[1] = (i >> 0)  & 0xffffffff;
	fp->fp_mant[2] = 0;
	fp->fp_mant[3] = 0;
	fpu_norm(fp);
	return (FPC_NUM);
}

#define	mask(nbits) ((1L << (nbits)) - 1)

/*
 * All external floating formats convert to internal in the same manner,
 * as defined here.  Note that only normals get an implied 1.0 inserted.
 */
#define	FP_TOF(exp, expbias, allfrac, f0, f1, f2, f3) \
	if (exp == 0) { \
		if (allfrac == 0) \
			return (FPC_ZERO); \
		fp->fp_exp = 1 - expbias; \
		fp->fp_mant[0] = f0; \
		fp->fp_mant[1] = f1; \
		fp->fp_mant[2] = f2; \
		fp->fp_mant[3] = f3; \
		fpu_norm(fp); \
		return (FPC_NUM); \
	} \
	if (exp == (2 * expbias + 1)) { \
		if (allfrac == 0) \
			return (FPC_INF); \
		fp->fp_mant[0] = f0; \
		fp->fp_mant[1] = f1; \
		fp->fp_mant[2] = f2; \
		fp->fp_mant[3] = f3; \
		return (FPC_QNAN); \
	} \
	fp->fp_exp = exp - expbias; \
	fp->fp_mant[0] = FP_1 | f0; \
	fp->fp_mant[1] = f1; \
	fp->fp_mant[2] = f2; \
	fp->fp_mant[3] = f3; \
	return (FPC_NUM)

/*
 * 32-bit single precision -> fpn.
 * We assume a single occupies at most (64-FP_LG) bits in the internal
 * format: i.e., needs at most fp_mant[0] and fp_mant[1].
 */
int
fpu_stof(struct fpn *fp, u_int i)
{
	int exp;
	u_int frac, f0, f1;
#define SNG_SHIFT (SNG_FRACBITS - FP_LG)

	exp = (i >> (32 - 1 - SNG_EXPBITS)) & mask(SNG_EXPBITS);
	frac = i & mask(SNG_FRACBITS);
	f0 = frac >> SNG_SHIFT;
	f1 = frac << (32 - SNG_SHIFT);
	FP_TOF(exp, SNG_EXP_BIAS, frac, f0, f1, 0, 0);
}

/*
 * 64-bit double -> fpn.
 * We assume this uses at most (96-FP_LG) bits.
 */
int
fpu_dtof(struct fpn *fp, u_int i, u_int j)
{
	int exp;
	u_int frac, f0, f1, f2;
#define DBL_SHIFT (DBL_FRACBITS - 32 - FP_LG)

	exp = (i >> (32 - 1 - DBL_EXPBITS)) & mask(DBL_EXPBITS);
	frac = i & mask(DBL_FRACBITS - 32);
	f0 = frac >> DBL_SHIFT;
	f1 = (frac << (32 - DBL_SHIFT)) | (j >> DBL_SHIFT);
	f2 = j << (32 - DBL_SHIFT);
	frac |= j;
	FP_TOF(exp, DBL_EXP_BIAS, frac, f0, f1, f2, 0);
}

/*
 * 128-bit extended -> fpn.
 */
int
fpu_qtof(struct fpn *fp, u_int i, u_int j, u_int k, u_int l)
{
	int exp;
	u_int frac, f0, f1, f2, f3;
#define EXT_SHIFT (-(EXT_FRACBITS - 3 * 32 - FP_LG))	/* left shift! */

	/*
	 * Note that ext and fpn `line up', hence no shifting needed.
	 */
	exp = (i >> (32 - 1 - EXT_EXPBITS)) & mask(EXT_EXPBITS);
	frac = i & mask(EXT_FRACBITS - 3 * 32);
	f0 = (frac << EXT_SHIFT) | (j >> (32 - EXT_SHIFT));
	f1 = (j << EXT_SHIFT) | (k >> (32 - EXT_SHIFT));
	f2 = (k << EXT_SHIFT) | (l >> (32 - EXT_SHIFT));
	f3 = l << EXT_SHIFT;
	frac |= j | k | l;
	FP_TOF(exp, EXT_EXP_BIAS, frac, f0, f1, f2, f3);
}

/*
 * Explode the contents of a register / regpair / regquad.
 * If the input is a signalling NaN, an NV (invalid) exception
 * will be set.  (Note that nothing but NV can occur until ALU
 * operations are performed.)
 */
void
fpu_explode(struct fpemu *fe, struct fpn *fp, int type, int reg)
{
	u_int s, *space;
	u_int64_t l, *xspace;

	xspace = (u_int64_t *)&fe->fe_fpstate->fs_regs[reg & ~1];
	l = xspace[0];
	space = &fe->fe_fpstate->fs_regs[reg];
	s = space[0];
	fp->fp_sign = (type == FTYPE_LNG) ? l >> 63 : s >> 31;
	fp->fp_sticky = 0;
	DPRINTF(FPE_INSN, ("fpu_explode: "));
	switch (type) {
	case FTYPE_LNG:
		DPRINTF(FPE_INSN, ("LNG: %llx", l));
		s = fpu_xtof(fp, l);
		break;

	case FTYPE_INT:
		DPRINTF(FPE_INSN, ("INT: %x", s));
		s = fpu_itof(fp, s);
		break;

	case FTYPE_SNG:
		DPRINTF(FPE_INSN, ("SNG: %x", s));
		s = fpu_stof(fp, s);
		break;

	case FTYPE_DBL:
		DPRINTF(FPE_INSN, ("DBL: %x %x", s, space[1]));
		s = fpu_dtof(fp, s, space[1]);
		break;

	case FTYPE_EXT:
		DPRINTF(FPE_INSN, ("EXT: %x %x %x %x", s, space[1],
		    space[2], space[3]));
		s = fpu_qtof(fp, s, space[1], space[2], space[3]);
		break;

	default:
		panic("fpu_explode");
	}
	DPRINTF(FPE_INSN, ("\n"));

	if (s == FPC_QNAN && (fp->fp_mant[0] & FP_QUIETBIT) == 0) {
		/*
		 * Input is a signalling NaN.  All operations that return
		 * an input NaN operand put it through a ``NaN conversion'',
		 * which basically just means ``turn on the quiet bit''.
		 * We do this here so that all NaNs internally look quiet
		 * (we can tell signalling ones by their class).
		 */
		fp->fp_mant[0] |= FP_QUIETBIT;
		fe->fe_cx = FSR_NV;	/* assert invalid operand */
		s = FPC_SNAN;
	}
	fp->fp_class = s;
	DPRINTF(FPE_REG, ("fpu_explode: %%%c%d => ", (type == FTYPE_LNG) ? 'x' :
	    ((type == FTYPE_INT) ? 'i' : 
		((type == FTYPE_SNG) ? 's' :
		    ((type == FTYPE_DBL) ? 'd' :
			((type == FTYPE_EXT) ? 'q' : '?')))), 
	    reg));
	DUMPFPN(FPE_REG, fp);
	DPRINTF(FPE_REG, ("\n"));
}
