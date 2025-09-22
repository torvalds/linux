/*	$OpenBSD: s_floorf.S,v 1.5 2022/12/27 17:10:07 jmc Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY(floorf)
	subl	$8,%esp

	fstcw	4(%esp)		/* store fpu control word */
	movw	4(%esp),%dx
	orw	$0x0400,%dx		/* round towards -oo */
	andw	$0xf7ff,%dx
	movw	%dx,(%esp)
	fldcw	(%esp)		/* load modified control word */

	flds	12(%esp);		/* round */
	frndint

	fldcw	4(%esp)		/* restore original control word */

	addl	$8,%esp
	ret
END_STD(floorf)
