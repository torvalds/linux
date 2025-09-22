/*	$OpenBSD: s_atan.S,v 1.3 2016/09/12 19:47:02 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <machine/asm.h>

ENTRY(atan)
	fldl	4(%esp)
	fld1
	fpatan
	ret
END(atan)
