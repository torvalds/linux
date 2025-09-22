/*	$OpenBSD: strncmp.S,v 1.7 2022/12/27 17:10:05 jmc Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

/*
 * NOTE: I've unrolled the loop eight times: large enough to make a
 * significant difference, and small enough not to totally trash the
 * cache.
 */

ENTRY(strncmp)
	pushl	%ebx
	movl	8(%esp),%eax
	movl	12(%esp),%ecx
	movl	16(%esp),%edx
	testl	%edx,%edx
	jmp	L2			/* Jump into the loop! */

	.align 2,0x90
L1:	incl	%eax
	incl	%ecx
	decl	%edx
L2:	jz	L4			/* strings are equal */
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	jne	L3

	incl	%eax
	incl	%ecx
	decl	%edx
	jz	L4
	movb	(%eax),%bl
	testb	%bl,%bl
	jz	L3
	cmpb	%bl,(%ecx)
	je	L1

	.align 2,0x90
L3:	movzbl	(%eax),%eax		/* unsigned comparison */
	movzbl	(%ecx),%ecx
	subl	%ecx,%eax
	popl	%ebx
	ret
	.align 2,0xcc
L4:	xorl	%eax,%eax
	popl	%ebx
	ret
END_STRONG(strncmp)
