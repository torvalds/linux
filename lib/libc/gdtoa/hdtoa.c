/*	$OpenBSD: hdtoa.c,v 1.5 2020/05/31 12:27:19 mortimer Exp $	*/
/*-
 * Copyright (c) 2004, 2005 David Schultz <das@FreeBSD.ORG>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <float.h>
#include <limits.h>
#include <math.h>

#include "gdtoaimp.h"

/* Strings values used by dtoa() */
#define	INFSTR	"Infinity"
#define	NANSTR	"NaN"

#define	DBL_ADJ		(DBL_MAX_EXP - 2 + ((DBL_MANT_DIG - 1) % 4))
#define	LDBL_ADJ	(LDBL_MAX_EXP - 2 + ((LDBL_MANT_DIG - 1) % 4))

/*
 * Round up the given digit string.  If the digit string is fff...f,
 * this procedure sets it to 100...0 and returns 1 to indicate that
 * the exponent needs to be bumped.  Otherwise, 0 is returned.
 */
static int
roundup(char *s0, int ndigits)
{
	char *s;

	for (s = s0 + ndigits - 1; *s == 0xf; s--) {
		if (s == s0) {
			*s = 1;
			return (1);
		}
		*s = 0;
	}
	++*s;
	return (0);
}

/*
 * Round the given digit string to ndigits digits according to the
 * current rounding mode.  Note that this could produce a string whose
 * value is not representable in the corresponding floating-point
 * type.  The exponent pointed to by decpt is adjusted if necessary.
 */
static void
dorounding(char *s0, int ndigits, int sign, int *decpt)
{
	int adjust = 0;	/* do we need to adjust the exponent? */

	switch (FLT_ROUNDS) {
	case 0:		/* toward zero */
	default:	/* implementation-defined */
		break;
	case 1:		/* to nearest, halfway rounds to even */
		if ((s0[ndigits] > 8) ||
		    (s0[ndigits] == 8 && s0[ndigits + 1] & 1))
			adjust = roundup(s0, ndigits);
		break;
	case 2:		/* toward +inf */
		if (sign == 0)
			adjust = roundup(s0, ndigits);
		break;
	case 3:		/* toward -inf */
		if (sign != 0)
			adjust = roundup(s0, ndigits);
		break;
	}

	if (adjust)
		*decpt += 4;
}

/*
 * This procedure converts a double-precision number in IEEE format
 * into a string of hexadecimal digits and an exponent of 2.  Its
 * behavior is bug-for-bug compatible with dtoa() in mode 2, with the
 * following exceptions:
 *
 * - An ndigits < 0 causes it to use as many digits as necessary to
 *   represent the number exactly.
 * - The additional xdigs argument should point to either the string
 *   "0123456789ABCDEF" or the string "0123456789abcdef", depending on
 *   which case is desired.
 * - This routine does not repeat dtoa's mistake of setting decpt
 *   to 9999 in the case of an infinity or NaN.  INT_MAX is used
 *   for this purpose instead.
 *
 * Note that the C99 standard does not specify what the leading digit
 * should be for non-zero numbers.  For instance, 0x1.3p3 is the same
 * as 0x2.6p2 is the same as 0x4.cp1.  This implementation chooses the
 * first digit so that subsequent digits are aligned on nibble
 * boundaries (before rounding).
 *
 * Inputs:	d, xdigs, ndigits
 * Outputs:	decpt, sign, rve
 */
char *
__hdtoa(double d, const char *xdigs, int ndigits, int *decpt, int *sign,
    char **rve)
{
	static const int sigfigs = (DBL_MANT_DIG + 3) / 4;
	struct ieee_double *p = (struct ieee_double *)&d;
	char *s, *s0;
	int bufsize;

	*sign = p->dbl_sign;

	switch (fpclassify(d)) {
	case FP_NORMAL:
		*decpt = p->dbl_exp - DBL_ADJ;
		break;
	case FP_ZERO:
		*decpt = 1;
		return (nrv_alloc("0", rve, 1));
	case FP_SUBNORMAL:
		d *= 0x1p514;
		*decpt = p->dbl_exp - (514 + DBL_ADJ);
		break;
	case FP_INFINITE:
		*decpt = INT_MAX;
		return (nrv_alloc(INFSTR, rve, sizeof(INFSTR) - 1));
	case FP_NAN:
		*decpt = INT_MAX;
		return (nrv_alloc(NANSTR, rve, sizeof(NANSTR) - 1));
	default:
		abort();
	}

	/* FP_NORMAL or FP_SUBNORMAL */

	if (ndigits == 0)		/* dtoa() compatibility */
		ndigits = 1;

	/*
	 * For simplicity, we generate all the digits even if the
	 * caller has requested fewer.
	 */
	bufsize = (sigfigs > ndigits) ? sigfigs : ndigits;
	s0 = rv_alloc(bufsize);
	if (s0 == NULL)
		return (NULL);

	/*
	 * We work from right to left, first adding any requested zero
	 * padding, then the least significant portion of the
	 * mantissa, followed by the most significant.  The buffer is
	 * filled with the byte values 0x0 through 0xf, which are
	 * converted to xdigs[0x0] through xdigs[0xf] after the
	 * rounding phase.
	 */
	for (s = s0 + bufsize - 1; s > s0 + sigfigs - 1; s--)
		*s = 0;
	for (; s > s0 + sigfigs - (DBL_FRACLBITS / 4) - 1 && s > s0; s--) {
		*s = p->dbl_fracl & 0xf;
		p->dbl_fracl >>= 4;
	}
	for (; s > s0; s--) {
		*s = p->dbl_frach & 0xf;
		p->dbl_frach >>= 4;
	}

	/*
	 * At this point, we have snarfed all the bits in the
	 * mantissa, with the possible exception of the highest-order
	 * (partial) nibble, which is dealt with by the next
	 * statement.  We also tack on the implicit normalization bit.
	 */
	*s = p->dbl_frach | (1U << ((DBL_MANT_DIG - 1) % 4));

	/* If ndigits < 0, we are expected to auto-size the precision. */
	if (ndigits < 0) {
		for (ndigits = sigfigs; s0[ndigits - 1] == 0; ndigits--)
			;
	}

	if (sigfigs > ndigits && s0[ndigits] != 0)
		dorounding(s0, ndigits, p->dbl_sign, decpt);

	s = s0 + ndigits;
	if (rve != NULL)
		*rve = s;
	*s-- = '\0';
	for (; s >= s0; s--)
		*s = xdigs[(unsigned int)*s];

	return (s0);
}
DEF_STRONG(__hdtoa);

#if (LDBL_MANT_DIG > DBL_MANT_DIG)

/*
 * This is the long double version of __hdtoa().
 */
char *
__hldtoa(long double e, const char *xdigs, int ndigits, int *decpt, int *sign,
    char **rve)
{
	static const int sigfigs = (LDBL_MANT_DIG + 3) / 4;
	struct ieee_ext *p = (struct ieee_ext *)&e;
	char *s, *s0;
	int bufsize;
	int fbits = 0;

	*sign = p->ext_sign;

	switch (fpclassify(e)) {
	case FP_NORMAL:
		*decpt = p->ext_exp - LDBL_ADJ;
		break;
	case FP_ZERO:
		*decpt = 1;
		return (nrv_alloc("0", rve, 1));
	case FP_SUBNORMAL:
		e *= 0x1p514L;
		*decpt = p->ext_exp - (514 + LDBL_ADJ);
		break;
	case FP_INFINITE:
		*decpt = INT_MAX;
		return (nrv_alloc(INFSTR, rve, sizeof(INFSTR) - 1));
	case FP_NAN:
		*decpt = INT_MAX;
		return (nrv_alloc(NANSTR, rve, sizeof(NANSTR) - 1));
	default:
		abort();
	}

	/* FP_NORMAL or FP_SUBNORMAL */

	if (ndigits == 0)		/* dtoa() compatibility */
		ndigits = 1;

	/*
	 * For simplicity, we generate all the digits even if the
	 * caller has requested fewer.
	 */
	bufsize = (sigfigs > ndigits) ? sigfigs : ndigits;
	s0 = rv_alloc(bufsize);
	if (s0 == NULL)
		return (NULL);

	/*
	 * We work from right to left, first adding any requested zero
	 * padding, then the least significant portion of the
	 * mantissa, followed by the most significant.  The buffer is
	 * filled with the byte values 0x0 through 0xf, which are
	 * converted to xdigs[0x0] through xdigs[0xf] after the
	 * rounding phase.
	 */
	for (s = s0 + bufsize - 1; s > s0 + sigfigs - 1; s--)
		*s = 0;

	for (fbits = EXT_FRACLBITS / 4; fbits > 0 && s > s0; s--, fbits--) {
		*s = p->ext_fracl & 0xf;
		p->ext_fracl >>= 4;
	}
#ifdef EXT_FRACLMBITS
	for (fbits = EXT_FRACLMBITS / 4; fbits > 0 && s > s0; s--, fbits--) {
		*s = p->ext_fraclm & 0xf;
		p->ext_fraclm >>= 4;
	}
#endif
#ifdef EXT_FRACHMBITS
	for (fbits = EXT_FRACHMBITS / 4; fbits > 0 && s > s0; s--, fbits--) {
		*s = p->ext_frachm & 0xf;
		p->ext_frachm >>= 4;
	}
#endif
	for (fbits = EXT_FRACHBITS / 4; fbits > 0 && s > s0; s--, fbits--) {
		*s = p->ext_frach & 0xf;
		p->ext_frach >>= 4;
	}

	/*
	 * At this point, we have snarfed all the bits in the
	 * mantissa, with the possible exception of the highest-order
	 * (partial) nibble, which is dealt with by the next
	 * statement.  We also tack on the implicit normalization bit.
	 */
	*s = (p->ext_frach | (1U << ((LDBL_MANT_DIG - 1) % 4))) & 0xf;

	/* If ndigits < 0, we are expected to auto-size the precision. */
	if (ndigits < 0) {
		for (ndigits = sigfigs; s0[ndigits - 1] == 0; ndigits--)
			;
	}

	if (sigfigs > ndigits && s0[ndigits] != 0)
		dorounding(s0, ndigits, p->ext_sign, decpt);

	s = s0 + ndigits;
	if (rve != NULL)
		*rve = s;
	*s-- = '\0';
	for (; s >= s0; s--)
		*s = xdigs[(unsigned int)*s];

	return (s0);
}
DEF_STRONG(__hldtoa);

#else	/* (LDBL_MANT_DIG == DBL_MANT_DIG) */

char *
__hldtoa(long double e, const char *xdigs, int ndigits, int *decpt, int *sign,
    char **rve)
{
	return (__hdtoa((double)e, xdigs, ndigits, decpt, sign, rve));
}
DEF_STRONG(__hldtoa);

#endif	/* (LDBL_MANT_DIG == DBL_MANT_DIG) */
