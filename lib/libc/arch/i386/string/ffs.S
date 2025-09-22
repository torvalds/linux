/*	$OpenBSD: ffs.S,v 1.7 2018/01/18 08:23:44 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY(ffs)
	bsfl	4(%esp),%eax
	jz	L1	 		/* ZF is set if all bits are 0 */
	incl	%eax			/* bits numbered from 1, not 0 */
	ret

	.align 2,0xcc
L1:	xorl	%eax,%eax		/* clear result */
	ret
END(ffs)
.protected ffs
