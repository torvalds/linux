/* $OpenBSD: fpsetsticky.S,v 1.3 2005/08/07 11:30:38 espie Exp $ */
/*
 * Written by Charles M. Hannum, Apr 9, 1995
 * Public domain.
 */

#include <machine/asm.h>

ENTRY(fpsetsticky)
	subl $28,%esp

	fnstenv (%esp)
	movl 4(%esp),%eax
	movl %eax,%edx

	andl $63,%eax

	subl %eax,%edx
	movl 32(%esp),%ecx
	andl $63,%ecx
	addl %ecx,%edx
	movl %edx,4(%esp)
	fldenv (%esp)

	addl $28,%esp
	ret
