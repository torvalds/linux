/*	$OpenBSD: fpu_implode.c,v 1.8 2024/03/29 21:02:11 miod Exp $	*/

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
 *	@(#)fpu_implode.c	8.1 (Berkeley) 6/11/93
 *	$NetBSD: fpu_implode.c,v 1.8 2001/08/26 05:44:46 eeh Exp $
 */

/*
 * FPU subroutines: `implode' internal format numbers into the machine's
 * `packed binary' format.
 */

#include <sys/types.h>

#include <machine/fsr.h>
#include <machine/ieee.h>
#include <machine/instr.h>

#include "fpu_arith.h"
#include "fpu_emu.h"
#include "fpu_extern.h"

static int fpround(struct fpemu *, struct fpn *);
static int toinf(struct fpemu *, int);

#define	FSR_GET_RD(fsr)		(((fsr) >> FSR_RD_SHIFT) & FSR_RD_MASK)

/*
 * Round a number (algorithm from Motorola MC68882 manual, modified for
 * our internal format).  Set inexact exception if rounding is required.
 * Return true iff we rounded up.
 *
 * After rounding, we discard the guard and round bits by shifting right
 * 2 bits (a la fpu_shr(), but we do not bother with fp->fp_sticky).
 * This saves effort later.
 *
 * Note that we may leave the value 2.0 in fp->fp_mant; it is the caller's
 * responsibility to fix this if necessary.
 */
static int
fpround(struct fpemu *fe, struct fpn *fp)
{
	u_int m0, m1, m2, m3;
	int gr, s;

	m0 = fp->fp_mant[0];
	m1 = fp->fp_mant[1];
	m2 = fp->fp_mant[2];
	m3 = fp->fp_mant[3];
	gr = m3 & 3;
	s = fp->fp_sticky;

	/* mant >>= FP_NG */
	m3 = (m3 >> FP_NG) | (m2 << (32 - FP_NG));
	m2 = (m2 >> FP_NG) | (m1 << (32 - FP_NG));
	m1 = (m1 >> FP_NG) | (m0 << (32 - FP_NG));
	m0 >>= FP_NG;

	if ((gr | s) == 0)	/* result is exact: no rounding needed */
		goto rounddown;

	fe->fe_cx |= FSR_NX;	/* inexact */

	/* Go to rounddown to round down; break to round up. */
	switch (FSR_GET_RD(fe->fe_fsr)) {
	case FSR_RD_RN:
	default:
		/*
		 * Round only if guard is set (gr & 2).  If guard is set,
		 * but round & sticky both clear, then we want to round
		 * but have a tie, so round to even, i.e., add 1 iff odd.
		 */
		if ((gr & 2) == 0)
			goto rounddown;
		if ((gr & 1) || fp->fp_sticky || (m3 & 1))
			break;
		goto rounddown;

	case FSR_RD_RZ:
		/* Round towards zero, i.e., down. */
		goto rounddown;

	case FSR_RD_RM:
		/* Round towards -Inf: up if negative, down if positive. */
		if (fp->fp_sign)
			break;
		goto rounddown;

	case FSR_RD_RP:
		/* Round towards +Inf: up if positive, down otherwise. */
		if (!fp->fp_sign)
			break;
		goto rounddown;
	}

	/* Bump low bit of mantissa, with carry. */
	FPU_ADDS(m3, m3, 1);
	FPU_ADDCS(m2, m2, 0);
	FPU_ADDCS(m1, m1, 0);
	FPU_ADDC(m0, m0, 0);
	fp->fp_mant[0] = m0;
	fp->fp_mant[1] = m1;
	fp->fp_mant[2] = m2;
	fp->fp_mant[3] = m3;
	return (1);

rounddown:
	fp->fp_mant[0] = m0;
	fp->fp_mant[1] = m1;
	fp->fp_mant[2] = m2;
	fp->fp_mant[3] = m3;
	return (0);
}

/*
 * For overflow: return true if overflow is to go to +/-Inf, according
 * to the sign of the overflowing result.  If false, overflow is to go
 * to the largest magnitude value instead.
 */
static int
toinf(struct fpemu *fe, int sign)
{
	int inf;

	/* look at rounding direction */
	switch (FSR_GET_RD(fe->fe_fsr)) {
	default:
	case FSR_RD_RN:		/* the nearest value is always Inf */
		inf = 1;
		break;

	case FSR_RD_RZ:		/* toward 0 => never towards Inf */
		inf = 0;
		break;

	case FSR_RD_RP:	/* toward +Inf iff positive */
		inf = sign == 0;
		break;

	case FSR_RD_RM:	/* toward -Inf iff negative */
		inf = sign;
		break;
	}
	return (inf);
}

/*
 * fpn -> int (int value returned as return value).
 *
 * N.B.: this conversion always rounds towards zero (this is a peculiarity
 * of the SPARC instruction set).
 */
u_int
__fpu_ftoi(fe, fp)
	struct fpemu *fe;
	struct fpn *fp;
{
	u_int i;
	int sign, exp;

	sign = fp->fp_sign;
	switch (fp->fp_class) {

	case FPC_ZERO:
		return (0);

	case FPC_NUM:
		/*
		 * If exp >= 2^32, overflow.  Otherwise shift value right
		 * into last mantissa word (this will not exceed 0xffffffff),
		 * shifting any guard and round bits out into the sticky
		 * bit.  Then ``round'' towards zero, i.e., just set an
		 * inexact exception if sticky is set (see fpround()).
		 * If the result is > 0x80000000, or is positive and equals
		 * 0x80000000, overflow; otherwise the last fraction word
		 * is the result.
		 */
		if ((exp = fp->fp_exp) >= 32)
			break;
		/* NB: the following includes exp < 0 cases */
		if (__fpu_shr(fp, FP_NMANT - 1 - exp) != 0)
			fe->fe_cx |= FSR_NX;
		i = fp->fp_mant[3];
		if (i >= ((u_int)0x80000000 + sign))
			break;
		return (sign ? -i : i);

	default:		/* Inf, qNaN, sNaN */
		break;
	}
	/* overflow: replace any inexact exception with invalid */
	fe->fe_cx = (fe->fe_cx & ~FSR_NX) | FSR_NV;
	return (0x7fffffff + sign);
}

/*
 * fpn -> extended int (high bits of int value returned as return value).
 *
 * N.B.: this conversion always rounds towards zero (this is a peculiarity
 * of the SPARC instruction set).
 */
u_int
__fpu_ftox(fe, fp, res)
	struct fpemu *fe;
	struct fpn *fp;
	u_int *res;
{
	u_int64_t i;
	int sign, exp;

	sign = fp->fp_sign;
	switch (fp->fp_class) {

	case FPC_ZERO:
		res[1] = 0;
		return (0);

	case FPC_NUM:
		/*
		 * If exp >= 2^64, overflow.  Otherwise shift value right
		 * into last mantissa word (this will not exceed
		 * 0xffffffffffffffff), shifting any guard and round bits out
		 * into the sticky bit.  Then ``round'' towards zero, i.e.,
		 * just set an inexact exception if sticky is set (see
		 * fpround()).  If the result is > 0x8000000000000000, or is
		 * positive and equals 0x8000000000000000, overflow;
		 * otherwise the last fraction word is the result.
		 */
		if ((exp = fp->fp_exp) >= 64)
			break;
		/* NB: the following includes exp < 0 cases */
		if (__fpu_shr(fp, FP_NMANT - 1 - exp) != 0)
			fe->fe_cx |= FSR_NX;
		i = ((u_int64_t)fp->fp_mant[2]<<32)|fp->fp_mant[3];
		if (i >= ((u_int64_t)0x8000000000000000LL + sign))
			break;
		if (sign)
			i = -i;
		res[1] = (int)i;
		return (i >> 32);

	default:		/* Inf, qNaN, sNaN */
		break;
	}
	/* overflow: replace any inexact exception with invalid */
	fe->fe_cx = (fe->fe_cx & ~FSR_NX) | FSR_NV;
	return (0x7fffffffffffffffLL + sign);
}

/*
 * fpn -> single (32 bit single returned as return value).
 * We assume <= 29 bits in a single-precision fraction (1.f part).
 */
u_int
__fpu_ftos(fe, fp)
	struct fpemu *fe;
	struct fpn *fp;
{
	u_int sign = fp->fp_sign << 31;
	int exp;

#define	SNG_EXP(e)	((e) << SNG_FRACBITS)	/* makes e an exponent */
#define	SNG_MASK	(SNG_EXP(1) - 1)	/* mask for fraction */

	/* Take care of non-numbers first. */
	if (ISNAN(fp)) {
		/*
		 * Preserve upper bits of NaN, per SPARC V8 appendix N.
		 * Note that fp->fp_mant[0] has the quiet bit set,
		 * even if it is classified as a signalling NaN.
		 */
		(void) __fpu_shr(fp, FP_NMANT - 1 - SNG_FRACBITS);
		exp = SNG_EXP_INFNAN;
		goto done;
	}
	if (ISINF(fp))
		return (sign | SNG_EXP(SNG_EXP_INFNAN));
	if (ISZERO(fp))
		return (sign);

	/*
	 * Normals (including subnormals).  Drop all the fraction bits
	 * (including the explicit ``implied'' 1 bit) down into the
	 * single-precision range.  If the number is subnormal, move
	 * the ``implied'' 1 into the explicit range as well, and shift
	 * right to introduce leading zeroes.  Rounding then acts
	 * differently for normals and subnormals: the largest subnormal
	 * may round to the smallest normal (1.0 x 2^minexp), or may
	 * remain subnormal.  In the latter case, signal an underflow
	 * if the result was inexact or if underflow traps are enabled.
	 *
	 * Rounding a normal, on the other hand, always produces another
	 * normal (although either way the result might be too big for
	 * single precision, and cause an overflow).  If rounding a
	 * normal produces 2.0 in the fraction, we need not adjust that
	 * fraction at all, since both 1.0 and 2.0 are zero under the
	 * fraction mask.
	 *
	 * Note that the guard and round bits vanish from the number after
	 * rounding.
	 */
	if ((exp = fp->fp_exp + SNG_EXP_BIAS) <= 0) {	/* subnormal */
		/* -NG for g,r; -SNG_FRACBITS-exp for fraction */
		(void) __fpu_shr(fp, FP_NMANT - FP_NG - SNG_FRACBITS - exp);
		if (fpround(fe, fp) && fp->fp_mant[3] == SNG_EXP(1))
			return (sign | SNG_EXP(1) | 0);
		if ((fe->fe_cx & FSR_NX) ||
		    (fe->fe_fsr & (FSR_UF << FSR_TEM_SHIFT)))
			fe->fe_cx |= FSR_UF;
		return (sign | SNG_EXP(0) | fp->fp_mant[3]);
	}
	/* -FP_NG for g,r; -1 for implied 1; -SNG_FRACBITS for fraction */
	(void) __fpu_shr(fp, FP_NMANT - FP_NG - 1 - SNG_FRACBITS);
#ifdef DIAGNOSTIC
	if ((fp->fp_mant[3] & SNG_EXP(1 << FP_NG)) == 0)
		__utrap_panic("fpu_ftos");
#endif
	if (fpround(fe, fp) && fp->fp_mant[3] == SNG_EXP(2))
		exp++;
	if (exp >= SNG_EXP_INFNAN) {
		/* overflow to inf or to max single */
		fe->fe_cx |= FSR_OF | FSR_NX;
		if (toinf(fe, sign))
			return (sign | SNG_EXP(SNG_EXP_INFNAN));
		return (sign | SNG_EXP(SNG_EXP_INFNAN - 1) | SNG_MASK);
	}
done:
	/* phew, made it */
	return (sign | SNG_EXP(exp) | (fp->fp_mant[3] & SNG_MASK));
}

/*
 * fpn -> double (32 bit high-order result returned; 32-bit low order result
 * left in res[1]).  Assumes <= 61 bits in double precision fraction.
 *
 * This code mimics fpu_ftos; see it for comments.
 */
u_int
__fpu_ftod(fe, fp, res)
	struct fpemu *fe;
	struct fpn *fp;
	u_int *res;
{
	u_int sign = fp->fp_sign << 31;
	int exp;

#define	DBL_EXP(e)	((e) << (DBL_FRACBITS & 31))
#define	DBL_MASK	(DBL_EXP(1) - 1)

	if (ISNAN(fp)) {
		(void) __fpu_shr(fp, FP_NMANT - 1 - DBL_FRACBITS);
		exp = DBL_EXP_INFNAN;
		goto done;
	}
	if (ISINF(fp)) {
		sign |= DBL_EXP(DBL_EXP_INFNAN);
		goto zero;
	}
	if (ISZERO(fp)) {
zero:		res[1] = 0;
		return (sign);
	}

	if ((exp = fp->fp_exp + DBL_EXP_BIAS) <= 0) {
		(void) __fpu_shr(fp, FP_NMANT - FP_NG - DBL_FRACBITS - exp);
		if (fpround(fe, fp) && fp->fp_mant[2] == DBL_EXP(1)) {
			res[1] = 0;
			return (sign | DBL_EXP(1) | 0);
		}
		if ((fe->fe_cx & FSR_NX) ||
		    (fe->fe_fsr & (FSR_UF << FSR_TEM_SHIFT)))
			fe->fe_cx |= FSR_UF;
		exp = 0;
		goto done;
	}
	(void) __fpu_shr(fp, FP_NMANT - FP_NG - 1 - DBL_FRACBITS);
	if (fpround(fe, fp) && fp->fp_mant[2] == DBL_EXP(2))
		exp++;
	if (exp >= DBL_EXP_INFNAN) {
		fe->fe_cx |= FSR_OF | FSR_NX;
		if (toinf(fe, sign)) {
			res[1] = 0;
			return (sign | DBL_EXP(DBL_EXP_INFNAN) | 0);
		}
		res[1] = ~0;
		return (sign | DBL_EXP(DBL_EXP_INFNAN) | DBL_MASK);
	}
done:
	res[1] = fp->fp_mant[3];
	return (sign | DBL_EXP(exp) | (fp->fp_mant[2] & DBL_MASK));
}

/*
 * fpn -> extended (32 bit high-order result returned; low-order fraction
 * words left in res[1]..res[3]).  Like ftod, which is like ftos ... but
 * our internal format *is* extended precision, plus 2 bits for guard/round,
 * so we can avoid a small bit of work.
 */
u_int
__fpu_ftoq(fe, fp, res)
	struct fpemu *fe;
	struct fpn *fp;
	u_int *res;
{
	u_int sign = fp->fp_sign << 31;
	int exp;

#define	EXT_EXP(e)	((e) << (EXT_FRACBITS & 31))
#define	EXT_MASK	(EXT_EXP(1) - 1)

	if (ISNAN(fp)) {
		(void) __fpu_shr(fp, 2);	/* since we are not rounding */
		exp = EXT_EXP_INFNAN;
		goto done;
	}
	if (ISINF(fp)) {
		sign |= EXT_EXP(EXT_EXP_INFNAN);
		goto zero;
	}
	if (ISZERO(fp)) {
zero:		res[1] = res[2] = res[3] = 0;
		return (sign);
	}

	if ((exp = fp->fp_exp + EXT_EXP_BIAS) <= 0) {
		(void) __fpu_shr(fp, FP_NMANT - FP_NG - EXT_FRACBITS - exp);
		if (fpround(fe, fp) && fp->fp_mant[0] == EXT_EXP(1)) {
			res[1] = res[2] = res[3] = 0;
			return (sign | EXT_EXP(1) | 0);
		}
		if ((fe->fe_cx & FSR_NX) ||
		    (fe->fe_fsr & (FSR_UF << FSR_TEM_SHIFT)))
			fe->fe_cx |= FSR_UF;
		exp = 0;
		goto done;
	}
	/* Since internal == extended, no need to shift here. */
	if (fpround(fe, fp) && fp->fp_mant[0] == EXT_EXP(2))
		exp++;
	if (exp >= EXT_EXP_INFNAN) {
		fe->fe_cx |= FSR_OF | FSR_NX;
		if (toinf(fe, sign)) {
			res[1] = res[2] = res[3] = 0;
			return (sign | EXT_EXP(EXT_EXP_INFNAN) | 0);
		}
		res[1] = res[2] = res[3] = ~0;
		return (sign | EXT_EXP(EXT_EXP_INFNAN) | EXT_MASK);
	}
done:
	res[1] = fp->fp_mant[1];
	res[2] = fp->fp_mant[2];
	res[3] = fp->fp_mant[3];
	return (sign | EXT_EXP(exp) | (fp->fp_mant[0] & EXT_MASK));
}

/*
 * Implode an fpn, writing the result into the given space.
 */
void
__fpu_implode(fe, fp, type, space)
	struct fpemu *fe;
	struct fpn *fp;
	int type;
	u_int *space;
{

	switch (type) {

	case FTYPE_LNG:
		space[0] = __fpu_ftox(fe, fp, space);
		break;

	case FTYPE_INT:
		space[0] = __fpu_ftoi(fe, fp);
		break;

	case FTYPE_SNG:
		space[0] = __fpu_ftos(fe, fp);
		break;

	case FTYPE_DBL:
		space[0] = __fpu_ftod(fe, fp, space);
		break;

	case FTYPE_EXT:
		/* funky rounding precision options ?? */
		space[0] = __fpu_ftoq(fe, fp, space);
		break;

#ifdef DIAGNOSTIC
	default:
		__utrap_panic("fpu_implode");
#endif
	}
	DPRINTF(FPE_REG, ("fpu_implode: %x %x %x %x\n",
		space[0], space[1], space[2], space[3]));
}
