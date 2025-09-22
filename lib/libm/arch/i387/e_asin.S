/*	$OpenBSD: e_asin.S,v 1.4 2016/09/12 19:47:02 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

/* asin = atan (x / sqrt(1 - x^2)) */
ENTRY(asin)
	fldl	4(%esp)			/* x */
	fst	%st(1)
	fmul	%st(0)			/* x^2 */
	fld1
	fsubp				/* 1 - x^2 */
	fsqrt				/* sqrt (1 - x^2) */
	fpatan
	ret
END_STD(asin)
