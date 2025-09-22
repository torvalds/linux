/*	$OpenBSD: s_atanf.S,v 1.3 2016/09/12 19:47:02 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY(atanf)
	flds	4(%esp)
	fld1
	fpatan
	ret
END_STD(atanf)
