/*	$OpenBSD: fpclassifyl.c,v 1.2 2015/10/27 05:54:49 guenther Exp $	*/
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
#include <math.h>

int
__fpclassifyl(long double e)
{
	struct ieee_ext *p = (struct ieee_ext *)&e;

	if (p->ext_exp == 0) {
		if (p->ext_frach == 0 && p->ext_frachm == 0 &&
		    p->ext_fraclm == 0 && p->ext_fracl == 0)
			return FP_ZERO;
		else
			return FP_SUBNORMAL;
	}

	if (p->ext_exp == EXT_EXP_INFNAN) {
		if (p->ext_frach == 0 && p->ext_frachm == 0 &&
		    p->ext_fraclm == 0 && p->ext_fracl == 0)
			return FP_INFINITE;
		else
			return FP_NAN;
	}

	return FP_NORMAL;
}
DEF_STRONG(__fpclassifyl);
