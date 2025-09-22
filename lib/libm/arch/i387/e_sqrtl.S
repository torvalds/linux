/*	$OpenBSD: e_sqrtl.S,v 1.2 2016/09/12 19:47:02 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY(sqrtl)
	fldt	4(%esp)
	fsqrt
	ret
END_STD(sqrtl)
