/*	$OpenBSD: s_significand.S,v 1.6 2022/08/27 08:30:18 tb Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <machine/asm.h>

ENTRY_NB(significand)
	fldl	4(%esp)
	fxtract
	fstp	%st(1)
	ret
END(significand)
	.weak	significand
