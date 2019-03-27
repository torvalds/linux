/*-
 * Copyright (c) 2005-2008 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Based on public-domain remainder routine by J.T. Conklin <jtc@NetBSD.org>.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$"); 
	
ENTRY(remquol)
	fldt	16(%esp)
	fldt	4(%esp)
1:	fprem1
	fstsw	%ax
	sahf
	jp	1b
	fstp	%st(1)
/* Extract the three low-order bits of the quotient from C0,C3,C1. */
	shrl	$6,%eax
	movl	%eax,%ecx
	andl	$0x108,%eax
	rorl	$7,%eax
	orl	%eax,%ecx
	roll	$4,%eax
	orl	%ecx,%eax
	andl	$7,%eax
/* Negate the quotient bits if x*y<0.  Avoid using an unpredictable branch. */
	movl	24(%esp),%ecx
	xorl	12(%esp),%ecx
	movsx	%cx,%ecx
	sarl	$16,%ecx
	sarl	$16,%ecx
	xorl	%ecx,%eax
	andl	$1,%ecx
	addl	%ecx,%eax
/* Store the quotient and return. */
	movl	28(%esp),%ecx
	movl	%eax,(%ecx)
	ret

	.section .note.GNU-stack,"",%progbits
