/*	$OpenBSD: s_significandf.S,v 1.6 2022/08/27 08:30:18 tb Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <machine/asm.h>

ENTRY_NB(significandf)
	flds	4(%esp)
	fxtract
	fstp	%st(1)
	ret
END(significandf)
	.weak	significandf
