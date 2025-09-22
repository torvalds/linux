/*	$OpenBSD: isinf.c,v 1.10 2013/11/12 06:09:48 deraadt Exp $	*/
/*
 * Copyright (c) 2008 Martynas Venckus <martynas@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <float.h>
#include <math.h>

int
__isinf(double d)
{
	struct ieee_double *p = (struct ieee_double *)&d;

	return (p->dbl_exp == DBL_EXP_INFNAN &&
	    p->dbl_frach == 0 && p->dbl_fracl == 0);
}

int
__isinff(float f)
{
	struct ieee_single *p = (struct ieee_single *)&f;
 
	return (p->sng_exp == SNG_EXP_INFNAN && p->sng_frac == 0);
}

#if	LDBL_MANT_DIG == DBL_MANT_DIG
__strong_alias(__isinfl, __isinf);
#endif	/* LDBL_MANT_DIG == DBL_MANT_DIG */

/*
 * 3BSD compatibility aliases.
 */
__strong_alias(isinf, __isinf);
__strong_alias(isinff, __isinff);
