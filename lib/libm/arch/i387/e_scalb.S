/*	$OpenBSD: e_scalb.S,v 1.6 2022/08/27 08:30:18 tb Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY_NB(scalb)
	fldl	12(%esp)
	fldl	4(%esp)
	fscale
	fstp	%st(1)			/* bug fix for fp stack overflow */
	ret
END_NONSTD(scalb)
