/*	$OpenBSD: fpu.c,v 1.4 2021/09/24 14:37:56 aoyama Exp $	*/

/*
 * Copyright (c) 2007, 2014, Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Common bits between the 88100 and the 88110 floating point completion
 * code.
 */

#include <sys/param.h>
#include <sys/proc.h>

#include <machine/fpu.h>
#include <machine/frame.h>
#include <machine/ieeefp.h>

#include <lib/libkern/softfloat.h>

#include <m88k/m88k/fpu.h>

/*
 * Values for individual bits in fcmp results.
 */
#define	CC_UN	0x00000001	/* unordered */
#define	CC_LEG	0x00000002	/* less than, equal or greater than */
#define	CC_EQ	0x00000004	/* equal */
#define	CC_NE	0x00000008	/* not equal */
#define	CC_GT	0x00000010	/* greater than */
#define	CC_LE	0x00000020	/* less than or equal */
#define	CC_LT	0x00000040	/* less than */
#define	CC_GE	0x00000080	/* greater than or equal */
#define	CC_OU	0x00000100	/* out of range */
#define	CC_IB	0x00000200	/* in range or on boundary */
#define	CC_IN	0x00000400	/* in range */
#define	CC_OB	0x00000800	/* out of range or on boundary */
/* the following only on 88110 */
#define	CC_UE	0x00001000	/* unordered or equal */
#define	CC_LG	0x00002000	/* less than or greater than */
#define	CC_UG	0x00004000	/* unordered or greater than */
#define	CC_ULE	0x00008000	/* unordered or less than or equal */
#define	CC_UL	0x00010000	/* unordered or less than */
#define	CC_UGE	0x00020000	/* unordered or greater than or equal */

/*
 * Inlines from softfloat-specialize.h which are not made public, needed
 * for fpu_compare.
 */
#define	float32_is_nan(a) \
	(0xff000000 < (a << 1))
#define	float32_is_signaling_nan(a) \
	((((a >> 22) & 0x1ff) == 0x1fe) && (a & 0x003fffff))

/*
 * Store a floating-point result, converting it to the required format if it
 * is of smaller precision.
 *
 * This assumes the original format (orig_width) is not FTYPE_INT, and the
 * final format (width) <= orig_width.
 */
void
fpu_store(struct trapframe *frame, u_int regno, u_int orig_width, u_int width,
    fparg *src)
{
	u_int32_t tmp;
	u_int rd;

	switch (width) {
	case FTYPE_INT:
		rd = float_get_round(frame->tf_fpcr);
		switch (orig_width) {
		case FTYPE_SNG:
			if (rd == FP_RZ)
				tmp = float32_to_int32_round_to_zero(src->sng);
			else
				tmp = float32_to_int32(src->sng);
			break;
		case FTYPE_DBL:
			if (rd == FP_RZ)
				tmp = float64_to_int32_round_to_zero(src->dbl);
			else
				tmp = float64_to_int32(src->dbl);
			break;
		}
		if (regno != 0)
			frame->tf_r[regno] = tmp;
		break;
	case FTYPE_SNG:
		switch (orig_width) {
		case FTYPE_SNG:
			tmp = src->sng;
			break;
		case FTYPE_DBL:
			tmp = float64_to_float32(src->dbl);
			break;
		}
		if (regno != 0)
			frame->tf_r[regno] = tmp;
		break;
	case FTYPE_DBL:
		switch (orig_width) {
		case FTYPE_DBL:
			tmp = (u_int32_t)(src->dbl >> 32);
			if (regno != 0)
				frame->tf_r[regno] = tmp;
			tmp = (u_int32_t)src->dbl;
			if (regno != 31)
				frame->tf_r[regno + 1] = tmp;
			break;
		}
		break;
	}
}

/*
 * Return the largest precision of all precision inputs.
 *
 * This assumes none of the inputs is FTYPE_INT.
 */
u_int
fpu_precision(u_int ts1, u_int ts2, u_int td)
{
	return max(td, max(ts1, ts2));
}

/*
 * Perform a compare instruction (fcmp, fcmpu).
 *
 * If either operand is NaN, the result is unordered.  This causes an
 * reserved operand exception (except for nonsignalling NaNs for fcmpu).
 */
void
fpu_compare(struct trapframe *frame, fparg *s1, fparg *s2, u_int width,
    u_int rd, u_int fcmpu)
{
	u_int32_t cc;
	int zero, s1positive, s2positive;

	/*
	 * Handle NaNs first, and raise invalid if fcmp or signaling NaN.
	 */
	switch (width) {
	case FTYPE_SNG:
		if (float32_is_nan(s1->sng)) {
			if (!fcmpu || float32_is_signaling_nan(s1->sng))
				float_set_invalid();
			cc = CC_UN;
			goto done;
		}
		if (float32_is_nan(s2->sng)) {
			if (!fcmpu || float32_is_signaling_nan(s2->sng))
				float_set_invalid();
			cc = CC_UN;
			goto done;
		}
		break;
	case FTYPE_DBL:
		if (float64_is_nan(s1->dbl)) {
			if (!fcmpu || float64_is_signaling_nan(s1->dbl))
				float_set_invalid();
			cc = CC_UN;
			goto done;
		}
		if (float64_is_nan(s2->dbl)) {
			if (!fcmpu || float64_is_signaling_nan(s2->dbl))
				float_set_invalid();
			cc = CC_UN;
			goto done;
		}
		break;
	}

	/*
	 * Now order the two numbers.
	 */
	switch (width) {
	case FTYPE_SNG:
		if (float32_eq(s1->sng, s2->sng))
			cc = CC_EQ;
		else if (float32_lt(s1->sng, s2->sng))
			cc = CC_LT | CC_NE;
		else
			cc = CC_GT | CC_NE;
		break;
	case FTYPE_DBL:
		if (float64_eq(s1->dbl, s2->dbl))
			cc = CC_EQ;
		else if (float64_lt(s1->dbl, s2->dbl))
			cc = CC_LT | CC_NE;
		else
			cc = CC_GT | CC_NE;
		break;
	}

done:

	/*
	 * Complete condition code mask.
	 */

	if (cc & CC_UN)
		cc |= CC_NE | CC_UE | CC_UG | CC_ULE | CC_UL | CC_UGE;
	if (cc & CC_EQ)
		cc |= CC_LE | CC_GE | CC_UE;
	if (cc & CC_GT)
		cc |= CC_GE;
	if (cc & CC_LT)
		cc |= CC_LE;
	if (cc & (CC_LT | CC_GT))
		cc |= CC_LG;
	if (cc & (CC_LT | CC_GT | CC_EQ))
		cc |= CC_LEG;
	if (cc & CC_GT)
		cc |= CC_UG;
	if (cc & CC_LE)
		cc |= CC_ULE;
	if (cc & CC_LT)
		cc |= CC_UL;
	if (cc & CC_GE)
		cc |= CC_UGE;

#ifdef M88100
	if (CPU_IS88100) {
		cc &= ~(CC_UE | CC_LG | CC_UG | CC_ULE | CC_UL | CC_UGE);
	}
#endif

	/*
	 * Fill the interval bits.
	 * s1 is compared to the interval [0, s2] unless s2 is negative.
	 */
	if (!(cc & CC_UN)) {
		switch (width) {
		case FTYPE_SNG:
			s2positive = s2->sng >> 31 == 0;
			break;
		case FTYPE_DBL:
			s2positive = s2->dbl >> 63 == 0;
			break;
		}
		if (!s2positive)
			goto completed;

		if (cc & CC_EQ) {
			/* if s1 and s2 are equal, s1 is on boundary */
			cc |= CC_IB | CC_OB;
			goto completed;
		}

		/* s1 and s2 are either Zero, numbers or Inf */
		switch (width) {
		case FTYPE_SNG:
			zero = float32_eq(s1->sng, 0);
			break;
		case FTYPE_DBL:
			zero = float64_eq(s1->dbl, 0LL);
			break;
		}
		if (zero) {
			/* if s1 is zero, it is on boundary */
			cc |= CC_IB | CC_OB;
			goto completed;
		}

		if (cc & CC_GT) {
			/* 0 <= s2 < s1 -> out of interval */
			cc |= CC_OU | CC_OB;
		} else {
			switch (width) {
			case FTYPE_SNG:
				s1positive = s1->sng >> 31 == 0;
				break;
			case FTYPE_DBL:
				s1positive = s1->dbl >> 63 == 0;
				break;
			}
			if (s1positive) {
				/* 0 < s1 < s2 -> in interval */
				cc |= CC_IB | CC_IN;
			} else {
				/* s1 < 0 <= s2 */
				cc |= CC_OU | CC_OB;
			}
		}
	}

completed:
	if (rd != 0)
		frame->tf_r[rd] = cc;
}
