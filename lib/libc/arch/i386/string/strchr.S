/*	$OpenBSD: strchr.S,v 1.8 2017/11/29 05:13:57 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

WEAK_ALIAS(index, strchr)

ENTRY(strchr)
	movl	4(%esp),%eax
	movb	8(%esp),%cl
	.align 2,0x90
L1:
	movb	(%eax),%dl
	cmpb	%dl,%cl			/* found char??? */
	je 	L2
	incl	%eax
	testb	%dl,%dl			/* null terminator??? */
	jnz	L1
	xorl	%eax,%eax
L2:
	ret
END_STRONG(strchr)
