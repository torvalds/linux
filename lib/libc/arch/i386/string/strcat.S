/*	$OpenBSD: strcat.S,v 1.10 2017/11/28 06:55:49 tb Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <machine/asm.h>

#if defined(APIWARN)
#APP
	.section .gnu.warning.strcat
	.ascii "strcat() is almost always misused, please use strlcat()"
#NO_APP
#endif

/*
 * NOTE: I've unrolled the loop eight times: large enough to make a
 * significant difference, and small enough not to totally trash the
 * cache.
 */

ENTRY(strcat)
	pushl	%edi			/* save edi */
	movl	8(%esp),%edi		/* dst address */
	movl	12(%esp),%edx		/* src address */
	pushl	%edi			/* push destination address */

	cld				/* set search forward */
	xorl	%eax,%eax		/* set search for null terminator */
	movl	$-1,%ecx		/* set search for lots of characters */
	repne				/* search! */
	scasb

	leal	-1(%edi),%ecx		/* correct dst address */

	.align 2,0x90
L1:	movb	(%edx),%al		/* unroll loop, but not too much */
	movb	%al,(%ecx)
	testb	%al,%al
	jz	L2
	movb	1(%edx),%al
	movb	%al,1(%ecx)
	testb	%al,%al
	jz	L2
	movb	2(%edx),%al
	movb	%al,2(%ecx)
	testb	%al,%al
	jz	L2
	movb	3(%edx),%al
	movb	%al,3(%ecx)
	testb	%al,%al
	jz	L2
	movb	4(%edx),%al
	movb	%al,4(%ecx)
	testb	%al,%al
	jz	L2
	movb	5(%edx),%al
	movb	%al,5(%ecx)
	testb	%al,%al
	jz	L2
	movb	6(%edx),%al
	movb	%al,6(%ecx)
	testb	%al,%al
	jz	L2
	movb	7(%edx),%al
	movb	%al,7(%ecx)
	addl	$8,%edx
	addl	$8,%ecx
	testb	%al,%al
	jnz	L1
L2:	popl	%eax			/* pop destination address */
	popl	%edi			/* restore edi */
	ret
END(strcat)
