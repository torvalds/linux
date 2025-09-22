/*	$OpenBSD: e_sqrtf.S,v 1.4 2016/09/12 19:47:02 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY(sqrtf)
	flds	4(%esp)
	fsqrt
	ret
END_STD(sqrtf)
