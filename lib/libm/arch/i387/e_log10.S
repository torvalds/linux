/*	$OpenBSD: e_log10.S,v 1.4 2016/09/12 19:47:02 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <machine/asm.h>

ENTRY(log10)
	fldlg2
	fldl	4(%esp)
	fyl2x
	ret
END(log10)
