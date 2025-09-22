/* $OpenBSD: fpgetsticky.S,v 1.3 2005/08/07 11:30:38 espie Exp $ */
/*
 * Written by J.T. Conklin, Apr 4, 1995
 * Public domain.
 */

#include <machine/asm.h>

ENTRY(fpgetsticky)
	subl $4,%esp
	fnstsw (%esp)
	movl (%esp),%eax
	andl $63,%eax
	addl $4,%esp
	ret
