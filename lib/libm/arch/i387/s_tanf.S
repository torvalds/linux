/*	$OpenBSD: s_tanf.S,v 1.3 2016/09/12 19:47:02 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <machine/asm.h>

/* A float's domain isn't large enough to require argument reduction. */
ENTRY(tanf)
	flds	4(%esp)
	fptan
	fstp	%st(0)
	ret
END(tanf)
