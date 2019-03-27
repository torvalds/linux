/*-
 * Copyright (c) 2003 Tim J. Robbins.
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

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * size_t
 * wcslen(const wchar_t *s) --
 *	Find the length of a wide character string.
 */
ENTRY(wcslen)
	movl	4(%esp),%ecx		/* String */
	pushl	%ebx
	xorl	%ebx,%ebx
	xorl	%eax,%eax
.p2align 4,0x90
L1:	cmpl	%ebx,(%ecx)
	jz	found0
	cmpl	%ebx,4(%ecx)
	jz	found1
	cmpl	%ebx,8(%ecx)
	jz	found2
	cmpl	%ebx,12(%ecx)
	jz	found3
	cmpl	%ebx,16(%ecx)
	jz	found4
	cmpl	%ebx,20(%ecx)
	jz	found5
	cmpl	%ebx,24(%ecx)
	jz	found6
	cmpl	%ebx,28(%ecx)
	jz	found7
	leal	32(%ecx),%ecx
	addl	$8,%eax
	jmp	L1
found7:	incl	%eax
found6:	incl	%eax
found5:	incl	%eax
found4:	incl	%eax
found3:	incl	%eax
found2:	incl	%eax
found1:	incl	%eax
found0:	popl	%ebx
	ret
END(wcslen)

	.section .note.GNU-stack,"",%progbits
