/*	$OpenBSD: e_atan2.S,v 1.4 2016/09/12 19:47:02 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY(atan2)
	fldl	 4(%esp)
	fldl	12(%esp)
	fpatan
	ret
END_STD(atan2)
