/*	$OpenBSD: memchr.S,v 1.6 2017/11/29 05:13:57 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY(memchr)
	pushl	%edi
	movl	8(%esp),%edi		/* string address */
	movl	12(%esp),%eax		/* set character to search for */
	movl	16(%esp),%ecx		/* set length of search */
	testl	%ecx,%ecx		/* test for len == 0 */
	jz	L1
	cld				/* set search forward */
	repne				/* search! */
	scasb
	jne	L1			/* scan failed, return null */
	leal	-1(%edi),%eax		/* adjust result of scan */
	popl	%edi
	ret
	.align 2,0xcc
L1:	xorl	%eax,%eax
	popl	%edi
	ret
END_STRONG(memchr)
