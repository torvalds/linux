/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <machine/asm.h>

__FBSDID("$FreeBSD$");
/* RCSID("$NetBSD: s_significandf.S,v 1.3 1995/05/09 00:24:07 jtc Exp $") */

ENTRY(significandf)
	flds	4(%esp)
	fxtract
	fstp	%st(1)
	ret
END(significandf)

	.section .note.GNU-stack,"",%progbits
